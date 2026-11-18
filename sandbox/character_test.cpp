// Bounded decoding of a character package and the vertex declaration it asks for.
// "--inspect <file>" reads a historical offline package and prints what it holds.
// "--derived <package> <fragment.spv> <slot:kind:set:binding>..." prepares that fragment module with
// those rules as the backend does for a shader without a profile, and prints what each draw gets and
// the DX11 registers the rule of D-076 gives the module's resources.
#include "offline/character_package.h"
#include "offline/character_materials.h"
#include "../frontend/xxmi/runtime.h"
#include "../layer/src/shader_hooks.h"
#include "../layer/src/texture_identity.h"
#include "../frontend/gpu/source_registers.h"
#include "../frontend/gpu/texture_roles.h"
#include "../layer/src/json.h"
#include "variant_fixture.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

using namespace efvk;
// A path argument as the process received it: argv holds the ANSI code page, which cannot carry
// every folder name a Mod has (a full-width colon, D-116's non-ASCII paths).
static std::filesystem::path path_argument(int index)
{
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    std::filesystem::path out = args && index < count ? std::filesystem::path(args[index]) : std::filesystem::path();
    LocalFree(args);
    return out;
}
static int passed = 0, failed = 0;
static void check(bool ok, const char* text) { if (ok) ++passed; else { ++failed; printf("FAIL: %s\n", text); } }
// The game's common character vertex shader by the register rule (D-079): constant buffers at
// vs-cb0 to vs-cb3 with the instance window at vs-cb2 (set 2, binding 0), the rolling bones at vs-t0.
static std::vector<efmi::gpu::SourceRegister> common_vertex_registers()
{
    using efmi::gpu::SourceRegister;
    auto cb = [](uint32_t index, uint32_t bytes, uint32_t set, uint32_t binding) {
        SourceRegister r; r.kind = SourceRegister::ConstantBuffer; r.letter = 'b'; r.index = index; r.bytes = bytes; r.set = set; r.binding = binding; return r;
    };
    SourceRegister bones; bones.kind = SourceRegister::BufferSrv; bones.letter = 't'; bones.index = 0; bones.stride = 16; bones.binding = 17;
    return {cb(0, 1312, 0, 11), cb(1, 3328, 0, 15), cb(2, 65536, 2, 0), cb(3, 448, 1, 0), bones};
}
// RuntimeDraw::source_srv over a stage's registers, as the backend answers it (D-124).
static bool srv_of(const std::vector<efmi::gpu::SourceRegister>& registers, unsigned slot, uint32_t& type, uint32_t& dimension, uint32_t& stride,
                   std::string& why)
{
    efmi::gpu::SourceSrvDeclaration declaration;
    if (!efmi::gpu::source_srv_declaration(registers, slot, declaration, why)) return false;
    if (declaration.words) { why = "Raw and structured uint declarations need original bytecode"; return false; }
    type = declaration.type;
    dimension = declaration.dimension; stride = declaration.stride;
    return true;
}
template <class T> static void put(std::vector<uint8_t>& data, T value)
{
    const auto* p = reinterpret_cast<const uint8_t*>(&value);
    data.insert(data.end(), p, p + sizeof(value));
}
// A refusal that is this offline stand-in's limit, not the Mod's: what it answers with "offline ...",
// and the services it has none of (the owned GPU runtime, the game's instance windows, the draw's
// render targets), which the layer has in the game.
static bool offline_limit(const std::string& why)
{
    for (const char* marker : {"offline ", "needs the owned GPU domain", "Original buffer window unavailable", "render targets are not known"})
        if (why.find(marker) != std::string::npos) return true;
    return false;
}
template <class T> static void poke(std::vector<uint8_t>& data, size_t offset, T value) { std::memcpy(data.data() + offset, &value, sizeof(value)); }

// Two components (bones 0-1 and 2-4), the first with a LOD 1 remap. One draw list of two draws on
// the first component, whose second draw reads a bone of the second component; the second is hidden.
struct Model {
    std::vector<uint8_t> payload, tables;
    uint32_t components = 2, streams = 0, sections = 2, lists = 4, draws = 2, edits = 1, bones = 5;
    size_t stream_table = 0, draw_table = 0, component_table = 0, list_table = 0, section_table = 0, edit_table = 0;
    size_t blend_a = 0, blend_b = 0, index_data = 0, remap_data = 0;
};
// `extra`: that many more components of one bone each after the two, the second draw reading the last of
// them instead of bone 3 (D-141: more components than a 32-bit mask holds).
static std::vector<uint8_t> package(Model* out = nullptr, uint32_t extra = 0)
{
    Model m;
    m.components += extra; m.lists += 2 * extra; m.bones += extra;
    struct Entry { uint32_t kind, stride; uint64_t offset, size; uint32_t index_offset, weight_offset; };
    std::vector<Entry> table;
    auto add = [&](uint32_t kind, uint32_t stride, const std::vector<uint8_t>& bytes, uint32_t io = kCharacterNone, uint32_t wo = kCharacterNone) {
        while (m.payload.size() % 16) m.payload.push_back(0);
        table.push_back({kind, stride, m.payload.size(), bytes.size(), io, wo});
        m.payload.insert(m.payload.end(), bytes.begin(), bytes.end());
        return static_cast<uint32_t>(table.size() - 1);
    };
    std::vector<uint8_t> position(3 * 16, 0), texcoord(3 * 8, 0), index, remap, blend_a, blend_b;
    for (uint32_t v = 0; v < 3; ++v) {
        for (uint16_t value : {uint16_t(65535), uint16_t(0), uint16_t(0), uint16_t(0), uint16_t(1), uint16_t(0), uint16_t(0), uint16_t(0)}) put(blend_a, value);
        const uint16_t second = extra ? uint16_t(4 + extra) : uint16_t(3);
        for (uint16_t value : {uint16_t(40000), uint16_t(25535), uint16_t(0), uint16_t(0), uint16_t(0), second, uint16_t(0), uint16_t(0)}) put(blend_b, value);
        put(index, v);
    }
    put(remap, uint16_t(1)); put(remap, uint16_t(0));
    const uint32_t s_remap = add(CharacterStream::Remap, 2, remap);
    const uint32_t s_index = add(CharacterStream::Index, 4, index);
    const uint32_t s_position = add(CharacterStream::Vertex, 16, position);
    const uint32_t s_texcoord = add(CharacterStream::Vertex, 8, texcoord);
    const uint32_t s_blend_a = add(CharacterStream::Vertex, 16, blend_a, 8, 0);
    const uint32_t s_blend_b = add(CharacterStream::Vertex, 16, blend_b, 8, 0);
    m.remap_data = static_cast<size_t>(table[s_remap].offset); m.index_data = static_cast<size_t>(table[s_index].offset);
    m.blend_a = static_cast<size_t>(table[s_blend_a].offset); m.blend_b = static_cast<size_t>(table[s_blend_b].offset);
    m.streams = static_cast<uint32_t>(table.size());
    auto& t = m.tables;
    m.component_table = t.size();
    for (uint32_t value : {0u, 2u, s_remap, 0u, 1u, 0u}) put(t, value);
    for (uint32_t value : {2u, 3u, kCharacterNone, 2u, 3u, 0u}) put(t, value);
    for (uint32_t i = 0; i < extra; ++i)
        for (uint32_t value : {5u + i, 1u, kCharacterNone, 4u + 2 * i, 5u + 2 * i, 0u}) put(t, value);
    m.stream_table = t.size();
    for (const auto& e : table) { put(t, e.kind); put(t, e.stride); put(t, e.offset); put(t, e.size); put(t, e.index_offset); put(t, e.weight_offset); }
    m.section_table = t.size();
    for (uint32_t value : {0x11111111u, 3u, 0u, 0u, 0u}) put(t, value);
    for (uint32_t value : {0x22222222u, 12u, kCharacterNone, 1u, kCharacterNone}) put(t, value);
    m.list_table = t.size();
    for (uint32_t value : {0u, 2u, 0u, 2u, 2u, 0u, 2u, 0u}) put(t, value);
    for (uint32_t i = 0; i < 2 * extra; ++i) for (uint32_t value : {2u, 0u}) put(t, value);
    m.draw_table = t.size();
    for (uint32_t blend : {s_blend_a, s_blend_b}) {
        for (uint32_t value : {s_index, 3u, 0u}) put(t, value);
        put(t, int32_t(0));
        for (uint32_t value : {s_position, s_texcoord, blend, s_position}) put(t, value);
    }
    m.edit_table = t.size();
    for (uint32_t value : {2u, uint32_t(VK_FORMAT_R8G8B8A8_UINT), uint32_t(VK_FORMAT_R16G16B16A16_UINT)}) put(t, value);
    std::vector<uint8_t> data(8);
    std::memcpy(data.data(), "EFMICH01", 8);
    for (uint32_t value : {m.bones, m.components, m.streams, m.sections, m.lists, m.draws, m.edits, 0u}) put(data, value);
    put(data, uint64_t(m.payload.size()));
    data.insert(data.end(), t.begin(), t.end());
    data.insert(data.end(), m.payload.begin(), m.payload.end());
    poke(data, 36, crc32c(0, data.data() + 48, data.size() - 48));
    if (out) *out = m;
    return data;
}
static void seal(std::vector<uint8_t>& data) { poke(data, 36, crc32c(0, data.data() + 48, data.size() - 48)); }
// A package file of the test's own, in the temporary folder.
static std::filesystem::path package_file(const char* name, const std::vector<uint8_t>& bytes)
{
    const auto path = std::filesystem::temp_directory_path() / (std::string("efvk-") + name + "-" + std::to_string(GetCurrentProcessId()) + ".bin");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

// Runtime validation keeps source bytes and non-file metadata in their existing generation.
static void memory_validation_tests(const std::vector<uint8_t>& valid, const Model& m)
{
    CharacterPackage seed;std::string why;check(seed.decode(valid,why),"memory validator starts from independently decoded fixture bytes");
    seed.images={"runtime.dds"};seed.image_flags={7};seed.sections[0].stride_slot=2;seed.sections[0].stride=16;seed.sections[0].computed=true;
    seed.sources.push_back({"runtime.buf",12,34});seed.draws[0].instances=3;
    auto p=seed;const auto* bytes=p.payload.data();const auto* images=p.images.data();
    p.lists[0].required=UINT64_MAX;p.streams[0].spans=UINT64_MAX;p.components[0].remap_lod1={999};
    check(p.validate(why) && p.payload.data()==bytes && p.images.data()==images && p.payload==seed.payload,
          "runtime validation borrows the original payload and retains material storage without a serialized copy");
    check(p.image_flags==seed.image_flags && p.sources.size()==1 && p.sources[0].offset==12 && p.draws[0].instances==3 &&
          p.sections[0].computed && p.sections[0].stride_slot==2 && p.sections[0].stride==16,
          "runtime-only loader flags, sources, draw instances and section predicates survive validation");
    check(p.lists[0].required==seed.lists[0].required && p.streams[0].spans==seed.streams[0].spans &&
          p.components[0].remap_lod1==seed.components[0].remap_lod1,"validation refreshes derived masks and remaps from the current bytes");
    check(p.validate(why) && p.payload.data()==bytes && p.lists[0].required==seed.lists[0].required,"repeated validation is stable without reallocating mesh bytes");
    auto reject=[&](auto change,const char* message){
        auto candidate=seed;change(candidate);candidate.lists[0].required=12345;
        const auto* storage=candidate.payload.data();const auto old_mask=candidate.streams[0].spans;
        check(!candidate.validate(why) && !why.empty() && candidate.payload.data()==storage &&
              candidate.lists[0].required==12345 && candidate.streams[0].spans==old_mask && candidate.image_flags==seed.image_flags,message);
    };
    reject([](auto& q){q.streams[0].offset=UINT64_MAX;},"in-memory overflowing stream is rejected before any read or derived-state publication");
    reject([](auto& q){q.lists[0].first=UINT32_MAX;},"in-memory draw-list bounds preserve the previous package on rejection");
    reject([&](auto& q){poke(q.payload,m.index_data+8,uint32_t(3));},"in-memory out-of-range indices are checked against actual vertex bytes");
    reject([&](auto& q){poke(q.payload,m.blend_a+8,uint16_t(5));},"in-memory weighted bones retain the decoder's full bounds check");
    reject([&](auto& q){poke(q.payload,m.remap_data,uint16_t(2000));},"in-memory remap source bounds are validated without a package checksum");
    reject([](auto& q){for(auto& stream:q.streams)if(stream.bone_index_offset!=kCharacterNone){stream.bone_index_bytes=3;break;}},
           "unsupported runtime bone widths are rejected atomically");
    auto compact=seed;
    for(auto& stream:compact.streams)if(stream.bone_index_offset!=kCharacterNone){
        for(uint64_t at=stream.offset;at<stream.offset+stream.size;at+=stream.stride){
            uint8_t indices[4];for(size_t k=0;k<4;++k){uint16_t bone;std::memcpy(&bone,compact.payload.data()+at+stream.bone_index_offset+2*k,2);indices[k]=uint8_t(bone);}
            std::memcpy(compact.payload.data()+at+stream.bone_index_offset,indices,4);
        }
        stream.bone_index_bytes=1;
    }
    check(compact.validate(why) && compact.lists[0].required==seed.lists[0].required,
          "validation preserves known runtime bone widths instead of resetting them to file-format defaults");
    CharacterPackage plain;plain.payload.resize(16);CharacterStream vertex;vertex.size=vertex.stride=16;plain.streams={vertex};
    CharacterSection section;section.match_count=3;section.component=section.lod=kCharacterNone;plain.sections={section};
    const auto* plain_bytes=plain.payload.data();
    check(plain.validate(why) && plain.payload.data()==plain_bytes && plain.payload_bytes==16,"bone-free runtime meshes use the same borrowed-payload entry");
    plain.streams[0].bone_index_offset=0;
    check(!plain.validate(why) && plain.payload.data()==plain_bytes,"bone-free runtime meshes cannot invent a skeleton reference");
}

// ISSUE-011: 3DMigoto frees source bytes after creating a buffer. Released package payloads
// retain source locations and checksums so the upload can read and verify those bytes again.
static void payload_tests(const std::vector<uint8_t>& valid, const Model& m)
{
    const auto path = package_file("payload", valid);
    CharacterPackage p;
    std::string why;
    check(p.load(path.u8string(), why) && p.payload.empty() && p.payload_bytes == m.payload.size() && p.sources.size() == p.streams.size(),
          "a loaded package holds no payload, only where each stream is in its file");
    std::vector<uint8_t> read(m.payload.size() + 1, 0xCD);
    check(p.read_payload(read.data(), why) && std::equal(m.payload.begin(), m.payload.end(), read.begin()) && read.back() == 0xCD,
          "the payload read again from the file is the one validated, zero between streams");
    // The texcoord stream (3) carries no bone indices in the file; a live plan that binds it under the
    // blend-index edit adopts it, its bytes read from the file.
    check(p.adopt_bones(3, 0, kCharacterNone, why) && p.streams[3].spans == 1, "a stream adopted for bones is read from its file");
    // The position stream (2) changed in the file since the load.
    auto changed = valid;
    changed[48 + m.tables.size() + static_cast<size_t>(p.streams[2].offset) + 2] ^= 1;
    package_file("payload", changed);
    check(!p.read_payload(read.data(), why) && why.find("changed") != std::string::npos,
          ("a stream whose file changed since the load refuses: " + why).c_str());
    check(!p.adopt_bones(2, 0, kCharacterNone, why) && why.find("changed") != std::string::npos && p.streams[2].bone_index_offset == kCharacterNone,
          "a stream adopted for bones whose file changed refuses and stays as it was");
    std::filesystem::remove(path);
    check(!p.read_payload(read.data(), why) && !why.empty(), "a stream whose file is gone refuses");
    CharacterPackage held;
    check(held.decode(valid, why) && held.payload == m.payload && held.payload_bytes == m.payload.size() && held.read_payload(read.data(), why) &&
          std::equal(m.payload.begin(), m.payload.end(), read.begin()), "a decoded package holds its payload until released");
}

static void material_tests(const std::vector<uint8_t>& mesh)
{
    const auto code = variant_fixture::shader(false, true);
    const std::string path = "F:/fixture.dds";
    std::vector<uint8_t> data{'E','F','M','I','C','H','0','2'};
    put(data, uint64_t(mesh.size())); put(data, uint64_t(0)); put(data, uint32_t(0)); put(data, uint32_t(0));
    data.insert(data.end(), mesh.begin(), mesh.end());
    const size_t table = data.size();
    for (uint32_t word : {1u, 1u, 2u, 0u, 0u, 0u}) put(data, word);
    put(data, uint32_t(path.size())); data.insert(data.end(), path.begin(), path.end());
    const size_t profile = data.size();
    put(data, uint32_t(code.size())); put(data, uint32_t(1));
    for (uint32_t word : code) put(data, word);
    const size_t rule = data.size();
    for (uint32_t word : {15u, 2u, 1u, 6u}) put(data, word);
    const size_t pixels = data.size();
    for (uint32_t word : {1u, 15u, 0u, 1u, 15u, kCharacterNone}) put(data, word);
    poke(data, 16, uint64_t(data.size() - table));
    auto reseal = [](std::vector<uint8_t>& bytes) { poke(bytes, 24, crc32c(0, bytes.data() + 32, bytes.size() - 32)); };
    reseal(data);
    CharacterPackage package;
    std::string reason;
    check(package.decode(data, reason) && package.materials && package.images == std::vector<std::string>{path} &&
          package.profiles[0].code == code && package.pixels[0][0].asset == 0 && package.pixels[1][0].asset == kCharacterNone,
          "material envelope preserves exact code, per-draw file/null and mesh bytes");
    {
        // The envelope's mesh streams are read again from the envelope's file (ISSUE-011).
        const auto file = package_file("envelope", data);
        CharacterPackage loaded;
        std::vector<uint8_t> read(package.payload.size());
        check(loaded.load(file.u8string(), reason) && loaded.payload.empty() && loaded.read_payload(read.data(), reason) && read == package.payload,
              "a material envelope's mesh is read again from its file");
        std::filesystem::remove(file);
    }
    {
        // A package without profiles: every fragment shader's rules come from its module (D-076).
        std::vector<uint8_t> bare(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(profile));
        bare.insert(bare.end(), data.begin() + static_cast<std::ptrdiff_t>(pixels), data.end());
        poke(bare, table + 4, uint32_t(0)); poke(bare, 16, uint64_t(bare.size() - table)); reseal(bare);
        CharacterPackage plain;
        check(plain.decode(bare, reason) && plain.materials && plain.profiles.empty() && plain.pixels.size() == 2 &&
              plain.pixels[0][0].slot == 15 && plain.pixels[0][0].asset == 0, "a material envelope without profiles keeps its per-draw bindings");
    }
    std::vector<std::vector<CharacterMaterial>> plans;
    std::set<uint32_t> counts;
    check(prepare_character_materials(package, plans, counts, reason) && plans[0][0].supported && plans[0][1].supported &&
          plans[0][0].assets == std::vector<uint32_t>{0} && plans[0][0].patch.images.size()==1 &&
          plans[0][1].patch.images.empty() && plans[0][1].patch.nulls.size()==1 && counts==std::set<uint32_t>{0,1},
          "production material preparation preserves per-draw file and explicit null");
    auto unsupported=package;unsupported.pixels[1].push_back({42,0});
    check(prepare_character_materials(unsupported,plans,counts,reason) && plans[0][0].supported && !plans[0][1].supported &&
          plans[0][1].patch.images.empty() && plans[0][1].patch.nulls.empty() && !plans[0][1].reason.empty(),
          "unknown late pixel slot has an explicit refusal and no partial contract");
    auto invalid=package;invalid.profiles[0].rules.push_back(invalid.profiles[0].rules[0]);
    check(!prepare_character_materials(invalid,plans,counts,reason) && plans[0][0].supported && !plans[0][1].supported,
          "invalid profile preserves all previously prepared contracts");
    auto reject = [&](std::vector<uint8_t> bytes, const char* text, bool seal = true) {
        if (seal) reseal(bytes);
        check(!package.decode(bytes, reason) && !reason.empty() && package.materials && package.images[0] == path &&
              package.profiles[0].code == code, text);
    };
    {auto b=data;poke(b,8,UINT64_MAX);reject(b,"material mesh length overflow refused");}
    {auto b=data;poke(b,16,uint64_t(4u<<20)+1);reject(b,"material byte budget enforced");}
    {auto b=data;b.back()^=1;reject(b,"material checksum covers bindings",false);}
    {auto b=data;b[28]=1;reject(b,"material reserved flags refused");}
    {auto b=data;b[39]='2';reject(b,"nested material envelope refused");}
    {auto b=data;poke(b,table,uint32_t(65));reject(b,"too many images refused");}
    {auto b=data;poke(b,table+8,uint32_t(1));reject(b,"material and mesh draw counts must agree");}
    {auto b=data;poke(b,table+24,uint32_t(4097));reject(b,"image path byte budget enforced");}
    {auto b=data;b[table+28]=0;reject(b,"embedded path NUL refused");}
    {auto b=data;b[table+29]='x';reject(b,"relative image path refused");}
    {auto b=data;b[table+28]=0xff;reject(b,"invalid UTF-8 path refused");}
    {auto b=data;poke(b,profile,uint32_t(1u<<22));reject(b,"shader byte budget enforced");}
    {auto b=data;poke(b,profile+8,uint32_t(0));reject(b,"shader magic checked");}
    {auto b=data;poke(b,rule,uint32_t(128));reject(b,"logical rule slot bound checked");}
    {auto b=data;poke(b,rule+4,uint32_t(0));reject(b,"unknown rule kind refused");}
    {auto b=data;poke(b,rule+8,uint32_t(3));reject(b,"profile cannot address private set");}
    {auto b=data;poke(b,rule+4,uint32_t(3));reject(b,"dimension zero cannot be implicit");}
    {auto b=data;poke(b,table+12,uint32_t(1));reject(b,"device restriction requires a policy");}
    {auto b=data;poke(b,pixels,uint32_t(129));reject(b,"pixel count bounded");}
    {auto b=data;poke(b,pixels+8,uint32_t(1));reject(b,"missing image asset refused");}
    {auto b=data;b.pop_back();poke(b,16,uint64_t(b.size()-table));reject(b,"truncated pixel binding refused");}
    {auto b=data;b.push_back(0);poke(b,16,uint64_t(b.size()-table));reject(b,"trailing material bytes refused");}
    check(package.decode(mesh, reason) && !package.materials && package.images.empty() && package.pixels.empty(),
          "loading a legacy package clears previous material state");
}

static void bone_layout_tests()
{
    CharacterPackage p; p.bones = 4;
    CharacterComponent a, b; a.first = 0; a.count = 2; b.first = 2; b.count = 2;
    p.components = {a, b}; std::string why; p.make_spans(why);
    CharacterStream stream; stream.stride = 64; stream.size = 128;
    p.streams = {stream}; p.payload.resize(128);
    for (size_t v = 0; v < 128; v += 64) {
        poke(p.payload, v, uint16_t(65535)); poke(p.payload, v + 8, uint16_t(1));
        poke(p.payload, v + 16, 1.0f); poke(p.payload, v + 32, uint32_t(3));
        poke(p.payload, v + 48, uint16_t(65535)); p.payload[v + 56] = 2;
    }
    uint64_t mask = 99;
    check(p.bone_spans(0, 8, 0, mask, why) && mask == 1, "16-bit layout selects only its weighted bone span");
    check(p.bone_spans(0, 32, 16, mask, why, 4, 4) && mask == 2, "32-bit indices and float weights have an independent mask in the same stream");
    check(p.bone_spans(0, 56, 48, mask, why, 1) && mask == 2, "8-bit interpretation of the same stream remains independent");
    check(p.streams[0].bone_index_offset == kCharacterNone && p.streams[0].spans == 0,
          "successful layout analysis never adopts a mutable stream default");
    check(p.adopt_bones(0, 8, 0, why), "legacy adoption keeps its existing interface");
    check(p.bone_spans(0, 32, 16, mask, why, 4, 4) && mask == 2 && p.streams[0].spans == 1 &&
          p.streams[0].bone_index_offset == 8 && p.streams[0].bone_index_bytes == 2,
          "a new declaration cannot overwrite an adopted default mask or width");
    mask = 99;
    check(!p.bone_spans(0, 8, 8, mask, why) && mask == 99, "overlapping layout refuses without publishing a mask");
    check(!p.bone_spans(0, 60, 48, mask, why) && mask == 99, "out-of-stride layout preserves the caller's mask");
    check(!p.bone_spans(0, 8, 0, mask, why, 3) && mask == 99, "unsupported index width preserves the caller's mask");
    check(!p.bone_spans(0, 8, 0, mask, why, 2, 1) && mask == 99, "unsupported weight width preserves the caller's mask");
    check(!p.bone_spans(1, 8, 0, mask, why) && mask == 99, "a missing stream cannot supply a layout mask");
    auto bad = p; poke(bad.payload, 32 + 4, uint32_t(4));
    check(!bad.bone_spans(0, 32, 16, mask, why, 4, 4) && mask == 99 && why == "bone index outside the skeleton",
          "unweighted out-of-range indices still refuse per layout");
    check(bad.bone_spans(0, 8, 0, mask, why) && mask == 1 && bad.streams[0].spans == 1,
          "one invalid interpretation cannot poison another declaration of the same bytes");
    bad = p; poke(bad.payload, 16, 0.5f); mask = 99;
    check(!bad.bone_spans(0, 32, 16, mask, why, 4, 4) && mask == 99,
          "unnormalized weights refuse without changing prior stream metadata");
    check(p.bone_spans(0, 8, 0, mask, why) && mask == 1, "returning to the first declaration reproduces its original mask");
}

static void declaration_tests(const CharacterPackage& p)
{
    VertexInputRecipe source, result;
    std::string why;
    source.bindings = {{0, 16, VK_VERTEX_INPUT_RATE_VERTEX}, {1, 8, VK_VERTEX_INPUT_RATE_VERTEX}, {2, 12, VK_VERTEX_INPUT_RATE_VERTEX},
                       {3, 16, VK_VERTEX_INPUT_RATE_VERTEX}, {4, 0, VK_VERTEX_INPUT_RATE_VERTEX}};
    source.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 1, VK_FORMAT_R32G32_SFLOAT, 0}, {2, 2, VK_FORMAT_R16G16B16A16_UNORM, 0},
                         {3, 2, VK_FORMAT_R8G8B8A8_UINT, 8}, {4, 4, VK_FORMAT_R8G8B8A8_UNORM, 12}};
    {
        auto layout=std::make_shared<CharacterDraw::InputLayout>();layout->source=source;
        check(p.vertex_input(p.draws[0],source,layout->replacement,why),"semantic fixture starts from a proven stream layout");
        layout->replacement.attributes[0].offset=4;
        auto draw=p.draws[0];draw.input_layout=layout;auto retained=draw;layout.reset();draw.input_layout.reset();
        check(p.vertex_input(retained,source,result,why) && result.attributes[0].offset==4,
              "copied runtime draws retain their immutable resolved layout");
        auto changed=source;changed.bindings[0].stride+=4;
        check(!p.vertex_input(retained,changed,result,why) && result.attributes[0].offset==4,
              "another source stride cannot reuse a resolved layout or partially publish");
        changed=source;changed.attributes[0].location=99;
        check(!p.vertex_input(retained,changed,result,why) && result.attributes[0].offset==4,
              "another shader location cannot reuse a resolved source declaration");
    }
    check(p.vertex_input(p.draws[0], source, result, why) && result.bindings[2].stride == 16 && result.bindings[1].stride == 8 &&
          result.bindings[4].stride == 0 && result.attributes[3].format == VK_FORMAT_R16G16B16A16_UINT && result.attributes[3].offset == 8 &&
          result.attributes[2].format == VK_FORMAT_R16G16B16A16_UNORM && result.attributes[4].binding == 4,
          "12-byte blend stream widened to 16; other streams and the default attribute untouched");
    {
        // An element offset edit (D-085): the attribute at the game's offset reads the Mod's stream at
        // the new one; an edit that names no attribute of the pipeline changes nothing.
        auto moved = p.draws[0];
        moved.offsets = {{2, 8, 12}, {3, 12, 40}};
        auto wide = p;
        wide.streams[wide.draws[0].vb[2]].stride = 16;
        check(wide.vertex_input(moved, source, result, why) && result.attributes[3].offset == 12 && result.attributes[2].offset == 0 &&
              result.attributes[0].offset == 0, "an offset edit moves the attribute at the game's offset only");
        moved.offsets = {{2, 8, 100}};
        check(!wide.vertex_input(moved, source, result, why), "an offset edit far past the Mod's stride refuses");
    }
    auto narrow = source;
    narrow.bindings[2].stride = 4; narrow.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {3, 2, VK_FORMAT_R8G8B8A8_UINT, 0}};
    auto small = p;
    small.streams[small.draws[0].vb[2]].stride = 8;
    check(small.vertex_input(small.draws[0], narrow, result, why) && result.bindings[2].stride == 8 &&
          result.attributes[1].format == VK_FORMAT_R16G16B16A16_UINT, "4-byte index-only stream widened to 8");
    {
        // A live draw under a 32-bit blend-index edit (R32G32B32A32_UINT): the same element, four 32-bit indices.
        auto wide32 = p;
        wide32.streams[wide32.draws[0].vb[2]].stride = 24;
        auto draw = wide32.draws[0];
        draw.blend = VK_FORMAT_R32G32B32A32_UINT;
        check(wide32.vertex_input(draw, source, result, why) && result.bindings[2].stride == 24 &&
              result.attributes[3].format == VK_FORMAT_R32G32B32A32_UINT && result.attributes[3].offset == 8 &&
              result.attributes[2].format == VK_FORMAT_R16G16B16A16_UNORM,
              "a 32-bit blend-index edit widens the element to R32G32B32A32_UINT for that draw");
        CharacterPackage bones;
        bones.bones = 5;
        CharacterComponent a, b; a.first = 0; a.count = 2; b.first = 2; b.count = 3;
        bones.components = {a, b};
        { std::string spans_reason; bones.make_spans(spans_reason); }
        CharacterStream s; s.kind = CharacterStream::Vertex; s.stride = 24; s.offset = 0; s.size = 48;
        bones.streams = {s};
        bones.payload.assign(48, 0);
        for (uint32_t v = 0; v < 2; ++v) {
            const uint16_t weights[4] = {65535, 0, 0, 0};
            const uint32_t indices[4] = {v == 0 ? 1u : 3u, 4, 4, 4};
            std::memcpy(bones.payload.data() + v * 24, weights, 8);
            std::memcpy(bones.payload.data() + v * 24 + 8, indices, 16);
        }
        auto adopted = bones;
        check(adopted.adopt_bones(0, 8, 0, why, 4) && adopted.streams[0].spans == 3 && adopted.streams[0].bone_index_bytes == 4,
              "32-bit bone indices are read as such; only weighted bones count for the component mask");
        auto outside = bones;
        const uint32_t beyond = 5;
        std::memcpy(outside.payload.data() + 8 + 4, &beyond, 4);
        check(!outside.adopt_bones(0, 8, 0, why, 4) && why == "bone index outside the skeleton",
              "a 32-bit bone index outside the skeleton refuses, weighted or not");
        check(!bones.adopt_bones(0, 16, 0, why, 4), "32-bit indices that do not fit the element refuse");
    }
    {
        // A live draw whose list has no blend-index edit (R-08, D-161): the pipeline's own BLENDINDICES element
        // is read as the game declares it (8-bit here); the package's own widening does not apply to that draw.
        auto own = p;
        own.streams[own.draws[0].vb[2]].stride = 12;
        auto draw = own.draws[0];
        draw.pipeline_blend = true;
        check(own.vertex_input(draw, source, result, why) && result.bindings[2].stride == 12 &&
              result.attributes[3].format == VK_FORMAT_R8G8B8A8_UINT && result.attributes[3].offset == 8,
              "a draw without a blend-index edit keeps the pipeline's own 8-bit element");
        CharacterPackage bones;
        bones.bones = 5;
        CharacterComponent a, b; a.first = 0; a.count = 2; b.first = 2; b.count = 3;
        bones.components = {a, b};
        { std::string spans_reason; bones.make_spans(spans_reason); }
        CharacterStream s; s.kind = CharacterStream::Vertex; s.stride = 12; s.offset = 0; s.size = 24;
        bones.streams = {s};
        bones.payload.assign(24, 0);
        for (uint32_t v = 0; v < 2; ++v) {
            const uint16_t weights[4] = {65535, 0, 0, 0};
            const uint8_t indices[4] = {uint8_t(v == 0 ? 1 : 3), 4, 4, 4};
            std::memcpy(bones.payload.data() + v * 12, weights, 8);
            std::memcpy(bones.payload.data() + v * 12 + 8, indices, 4);
        }
        auto adopted = bones;
        check(adopted.adopt_bones(0, 8, 0, why, 1) && adopted.streams[0].spans == 3 && adopted.streams[0].bone_index_bytes == 1,
              "8-bit bone indices are read as such");
        auto outside = bones;
        outside.payload[8 + 1] = 5;
        check(!outside.adopt_bones(0, 8, 0, why, 1) && why == "bone index outside the skeleton", "an 8-bit bone index outside the skeleton refuses");
    }
    auto depth_only = source;
    depth_only.bindings = {{0, 16, VK_VERTEX_INPUT_RATE_VERTEX}};
    depth_only.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}};
    check(p.vertex_input(p.draws[0], depth_only, result, why) && result.bindings.size() == 1 && result.bindings[0].stride == 16,
          "streams the pipeline does not declare are ignored");
    auto wide = source;
    wide.bindings[1].stride = 20; wide.attributes[1] = {1, 1, VK_FORMAT_R32G32_SFLOAT, 12};
    check(p.vertex_input(p.draws[0], wide, result, why) && result.bindings[1].stride == 8 && result.attributes[1].offset == 12,
          "an element a little past the file's stride is kept where it is, as the source API reads it");
    wide.attributes[1].offset = 100;
    check(!p.vertex_input(p.draws[0], wide, result, why) && !why.empty(), "an element far outside the file's stride refuses");
    auto unknown = source;
    unknown.attributes[1].format = VK_FORMAT_R64G64_SFLOAT;
    check(!p.vertex_input(p.draws[0], unknown, result, why) && !why.empty(), "an element of an unknown format in a replaced stream refuses");
    auto twice = source;
    twice.bindings.push_back(twice.bindings[0]);
    check(!p.vertex_input(p.draws[0], twice, result, why), "a declaration with a repeated binding refuses");
    auto instanced = source;
    instanced.bindings[2].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    check(!p.vertex_input(p.draws[0], instanced, result, why), "a per-instance stream is not replaced");
    auto same = source;
    same.bindings[2].stride = 16; same.attributes[3].format = VK_FORMAT_R16G16B16A16_UINT;
    check(p.vertex_input(p.draws[0], same, result, why) && result.bindings[2].stride == 16 && result.attributes[3].format == VK_FORMAT_R16G16B16A16_UINT,
          "a declaration that already has the file's layout passes unchanged");
}

// RabbitFX's roles by the bound textures (D-082): the decisions server/efmi_vk/texture_roles.py makes.
static void texture_role_tests()
{
    using efmi::gpu::RoleSample; using efmi::gpu::RoleTexture;
    const RoleTexture srgb{true, 146, 2048, 2048, 12}, linear{true, 145, 2048, 2048, 12}, small{true, 145, 512, 512, 10},
                      bc5{true, 141, 2048, 2048, 12}, target{true, 122, 1708, 1068, 1}, placeholder{true, 43, 4, 4, 1};
    auto run = [](std::vector<std::pair<RoleSample, RoleTexture>> items) {
        std::vector<RoleSample> samples;
        for (const auto& item : items) samples.push_back(item.first);
        return efmi::gpu::texture_roles(samples, [&](const RoleSample& s) {
            for (const auto& item : items) if (item.first.t == s.t) return item.second;
            return RoleTexture{};
        });
    };
    auto sample = [](uint32_t t, uint32_t at, bool decoded = false) { RoleSample s; s.t = t; s.at = at; s.decoded = decoded; return s; };
    check(efmi::gpu::texture_class(srgb) == 'S' && efmi::gpu::texture_class(linear) == 'U' && efmi::gpu::texture_class(bc5) == 'N' &&
          efmi::gpu::texture_class(target) == 'o' && efmi::gpu::texture_class(placeholder) == 'o' &&
          efmi::gpu::texture_class(RoleTexture{true, 146, 2048, 2048, 11}) == 'o',
          "texture classes: sRGB, linear BC, BC5 asset textures; targets, placeholders and short mip chains are none");
    auto r = run({{sample(16, 10), srgb}, {sample(17, 20), linear}, {sample(18, 50, true), bc5}});
    check(r.diffuse == 16 && r.lightmap == 17 && r.normal == 18, "diffuse, a lightmap of its size, then a BC5 normal map");
    r = run({{sample(16, 10), srgb}, {sample(17, 20), linear}, {sample(18, 50, true), linear}, {sample(12, 90), small}});
    check(r.diffuse == 16 && r.lightmap == 17 && r.normal == 18, "a linear normal map decoded right after the lightmap");
    r = run({{sample(0, 10), srgb}, {sample(1, 40, true), srgb}});
    check(r.diffuse == 0 && r.lightmap == -1 && r.normal == 1, "an sRGB texture decoded as a normal near the diffuse");
    r = run({{sample(14, 10), srgb}, {sample(12, 20), target}, {sample(13, 700, true), linear}});
    check(r.diffuse == 14 && r.lightmap == -1 && r.normal == -1, "a normal decode far from the diffuse is not a role");
    r = run({{sample(13, 10), srgb}, {sample(14, 20), small}});
    check(r.diffuse == 13 && r.lightmap == -1 && r.normal == 14, "a linear texture of another size is the normal map, not a lightmap");
    r = run({{sample(5, 10), target}, {sample(6, 20), linear}});
    check(r.diffuse == -1 && r.lightmap == -1 && r.normal == -1, "no sRGB asset texture: no role");
    r = run({{sample(16, 10), srgb}, {sample(16, 15), srgb}, {sample(17, 20), RoleTexture{}}});
    check(r.diffuse == 16 && r.lightmap == -1 && r.normal == -1, "a register sampled twice counts once; an unknown texture has no class");
}

// The DX11 register rule (D-076) on the fixture's fragment modules. Their bone block is a read-only
// buffer and comes first (t0); the image follows (t1) wherever its binding is, as for a feature
// variant of a game shader that moves its bindings.
static void register_tests()
{
    using efmi::gpu::SourceRegister;
    std::vector<SourceRegister> r;
    std::string reason;
    const auto at = [&](char letter, uint32_t index) { return efmi::gpu::find_source_register(r, letter, index); };
    check(efmi::gpu::source_registers(variant_fixture::shader(false, true), r, reason) && at('t', 0) && at('t', 1) &&
          at('t', 0)->kind == SourceRegister::BufferSrv && at('t', 0)->set == 0 && at('t', 0)->binding == 7 &&
          at('t', 1)->kind == SourceRegister::ImageSrv && at('t', 1)->set == 1 && at('t', 1)->binding == 6 && at('t', 1)->dim == 1 && !at('t', 2),
          "the read-only bone block is t0 and the image t1 of the material fragment module");
    check(efmi::gpu::source_registers(variant_fixture::shader(false, true, 8), r, reason) && at('t', 1) && at('t', 1)->binding == 8,
          "the variant with the image at another binding keeps it at t1");
    check(efmi::gpu::source_registers(variant_fixture::shader(false, false), r, reason) && at('t', 0) && !at('t', 1) &&
          at('b', 0) && at('b', 0)->kind == SourceRegister::ConstantBuffer && at('b', 0)->set == 2 && at('b', 0)->binding == 0,
          "a module without an image has its buffer at t0 and its uniform block at b0");
    auto cut = variant_fixture::shader(false, true);
    cut.resize(cut.size() - 3);
    r = {SourceRegister{}};
    check(!efmi::gpu::source_registers(cut, r, reason) && !efmi::gpu::source_registers({1, 2, 3}, r, reason) && r.size() == 1,
          "a truncated or foreign module is refused and the output stays");
}

int main(int argc, char** argv)
{
    SetEnvironmentVariableW(L"EFMI_VK_LOG", L"character-test.log");
    util_init(GetModuleHandleW(nullptr));
    CharacterPackage p;
    std::string why;
    if (argc == 3 && !strcmp(argv[1], "--material-shader")) {
        const auto code = variant_fixture::shader(false, true);
        std::ofstream out(argv[2], std::ios::binary);
        out.write(reinterpret_cast<const char*>(code.data()), code.size() * 4);
        return out ? 0 : 1;
    }
    if (argc == 3 && !strcmp(argv[1], "--materials")) {
        std::vector<std::vector<CharacterMaterial>> plans;
        std::set<uint32_t> counts;
        if (!p.load(argv[2], why) || !p.materials || !prepare_character_materials(p, plans, counts, why)) {
            fprintf(stderr, "materials rejected: %s\n", why.c_str()); return 1;
        }
        JsonWriter w;w.begin_object();w.kn("vendor",p.material_vendor);w.kn("device",p.material_device);
        w.key("profiles");w.begin_array();
        for(size_t i=0;i<plans.size();++i){
            const auto& code=p.profiles[i].code;
            w.begin_object();w.kn("crc",crc32c(0,code.data(),code.size()*4));w.key("draws");w.begin_array();
            for(const auto& plan:plans[i]){
                w.begin_object();w.kb("ok",plan.supported);w.kb("compatibility",plan.compatibility);w.kv("reason",plan.reason);
                w.key("images");w.begin_array();
                for(size_t j=0;j<plan.patch.images.size();++j){
                    w.begin_array();w.num(plan.assets[j]);w.num(plan.patch.images[j].set);w.num(plan.patch.images[j].binding);w.end_array();
                }
                w.end_array();w.key("nulls");w.begin_array();
                for(const auto& image:plan.patch.nulls){w.begin_array();w.num(image.set);w.num(image.binding);w.end_array();}
                w.end_array();w.end_object();
            }
            w.end_array();w.end_object();
        }
        w.end_array();w.end_object();printf("%s\n",w.out.c_str());return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "--derived")) {
        std::vector<std::vector<CharacterMaterial>> known;
        std::set<uint32_t> counts, used;
        if (!p.load(argv[2], why) || !p.materials || !prepare_character_materials(p, known, counts, why)) {
            fprintf(stderr, "materials rejected: %s\n", why.c_str()); return 1;
        }
        std::ifstream in(argv[3], std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CharacterMaterialProfile source;
        source.code.resize(bytes.size() / 4);
        if (bytes.empty() || bytes.size() % 4) { fprintf(stderr, "fragment module unreadable\n"); return 1; }
        memcpy(source.code.data(), bytes.data(), bytes.size());
        for (int i = 4; i < argc; ++i) {
            CharacterMaterialRule r; uint32_t kind = 0;
            if (sscanf(argv[i], "%u:%u:%u:%u", &r.slot, &kind, &r.set, &r.binding) != 4) { fprintf(stderr, "rule unreadable\n"); return 1; }
            r.kind = static_cast<CharacterMaterialRule::Kind>(kind); source.rules.push_back(r);
        }
        std::vector<efmi::gpu::SourceRegister> registers;
        std::string unread;
        const bool readable = efmi::gpu::source_registers(source.code, registers, unread);
        std::vector<CharacterMaterial> plans;
        const bool prepared = prepare_character_profile(p, source, plans, used, why);
        bool covered = true;
        for (uint32_t count : used) covered &= counts.count(count) != 0;
        JsonWriter w; w.begin_object(); w.kb("readable", readable); w.kb("prepared", prepared); w.kv("reason", why); w.kb("counts_covered", covered);
        // The DX11 registers the rule gives the module's resources: [letter, register, set, binding].
        w.key("registers"); w.begin_array();
        for (const auto& r : registers) {
            w.begin_array(); w.str(std::string(1, r.letter)); w.num(r.index); w.num(r.set); w.num(r.binding); w.end_array();
        }
        w.end_array(); w.key("draws"); w.begin_array();
        for (const auto& plan : plans) {
            w.begin_object(); w.kb("ok", plan.supported); w.kv("reason", plan.reason);
            w.key("images"); w.begin_array();
            for (size_t j = 0; j < plan.patch.images.size(); ++j) {
                w.begin_array(); w.num(plan.assets[j]); w.num(plan.patch.images[j].set); w.num(plan.patch.images[j].binding); w.end_array();
            }
            w.end_array(); w.key("nulls"); w.begin_array();
            for (const auto& image : plan.patch.nulls) { w.begin_array(); w.num(image.set); w.num(image.binding); w.end_array(); }
            w.end_array(); w.end_object();
        }
        w.end_array(); w.end_object(); printf("%s\n", w.out.c_str()); return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--view-formats")) {
        // D-170: the DXGI format the layer answers for a view of each Vulkan format, per aspect (1 colour,
        // 2 depth, 4 stencil): "<VkFormat> <aspect> <DXGI_FORMAT>" for those it maps (scripts/check_texture_properties.py).
        for (uint32_t format = 1; format < 185; ++format)
            for (uint32_t aspect : {1u, 2u, 4u})
                if (const uint32_t dxgi = efvk::dxgi_view_format(VkFormat(format), VkImageAspectFlags(aspect))) printf("%u %u %u\n", format, aspect, dxgi);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--shader-hooks")) {
        // EFMITTL's hook facts (D-113, D-171, D-185) of SPIR-V modules: "<file> <lit vertex 0/1> <copy fragment 0/1>
        // <vertex group 0-5> <pixel groups known 0/1> <colour slot or -1> <narrow first 0/1>".
        for (int i = 2; i < argc; ++i) {
            std::ifstream in(argv[i], std::ios::binary | std::ios::ate);
            std::vector<uint32_t> code(size_t(in.tellg()) / 4); in.seekg(0); in.read(reinterpret_cast<char*>(code.data()), code.size() * 4);
            const auto pixel = efvk::character_pixel_hook(code);
            printf("%s %d %d %u %d %d %d\n", argv[i], efvk::lit_character_vertex(code) ? 1 : 0, efvk::copy_fragment(code) ? 1 : 0,
                   efvk::character_vertex_hook(code), pixel.known ? 1 : 0, pixel.colour_slot, pixel.narrow_first ? 1 : 0);
        }
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--core-facts")) {
        // D-079 as the backend computes it, per vertex module: "<file> <slot> <rolling> <set>.<binding>".
        for (int i = 2; i < argc; ++i) {
            std::ifstream in(argv[i], std::ios::binary | std::ios::ate);
            std::vector<uint32_t> code(in ? size_t(in.tellg()) / 4 : 0);
            if (in) { in.seekg(0); in.read(reinterpret_cast<char*>(code.data()), code.size() * 4); }
            std::vector<efmi::gpu::SourceRegister> registers; std::string reason;
            if (!efmi::gpu::source_registers(code, registers, reason)) { printf("%s refused %s\n", argv[i], reason.c_str()); continue; }
            const auto facts = efmi::gpu::core_source_facts(registers);
            printf("%s %d %d %d.%d\n", argv[i], facts.vs_config_slot, facts.rolling_bones ? 1 : 0,
                   facts.config ? int(facts.config->set) : -1, facts.config ? int(facts.config->binding) : -1);
        }
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--srv-declarations")) {
        // D-124 as the backend answers it, per module: "<file> <t registers> <slot>:<type>.<dimension>.<stride>"
        // for each t register, "<slot>:-" where the SPIR-V does not decide it.
        for (int i = 2; i < argc; ++i) {
            std::ifstream in(argv[i], std::ios::binary | std::ios::ate);
            std::vector<uint32_t> code(in ? size_t(in.tellg()) / 4 : 0);
            if (in) { in.seekg(0); in.read(reinterpret_cast<char*>(code.data()), code.size() * 4); }
            std::vector<efmi::gpu::SourceRegister> registers; std::string reason;
            if (!efmi::gpu::source_registers(code, registers, reason)) { printf("%s refused %s\n", argv[i], reason.c_str()); continue; }
            uint32_t count = 0;
            for (const auto& r : registers) if (r.letter == 't') count = std::max(count, r.index + r.count);
            std::string line;
            for (uint32_t slot = 0; slot < count; ++slot) {
                uint32_t type = 0, dimension = 0, stride = 0; char item[48];
                if (srv_of(registers, slot, type, dimension, stride, reason))
                    snprintf(item, sizeof(item), " %u:%u.%u.%u", slot, type, dimension, stride);
                else snprintf(item, sizeof(item), " %u:-", slot);
                line += item;
            }
            printf("%s %u%s\n", argv[i], count, line.c_str());
        }
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--texture-roles")) {
        // D-082 as the backend reads a fragment module: "<file> t.set.binding.decoded.at ..." per sample.
        for (int i = 2; i < argc; ++i) {
            std::ifstream in(argv[i], std::ios::binary | std::ios::ate);
            std::vector<uint32_t> code(in ? size_t(in.tellg()) / 4 : 0);
            if (in) { in.seekg(0); in.read(reinterpret_cast<char*>(code.data()), code.size() * 4); }
            std::vector<efmi::gpu::RoleSample> samples; std::string reason;
            if (!efmi::gpu::role_samples(code, samples, reason)) { printf("%s refused %s\n", argv[i], reason.c_str()); continue; }
            std::string line;
            for (const auto& s : samples) {
                char item[64];
                snprintf(item, sizeof(item), "%s%u.%u.%u.%d.%u", line.empty() ? "" : " ", s.t, s.set, s.binding, s.decoded ? 1 : 0, s.at);
                line += item;
            }
            printf("%s %s\n", argv[i], line.c_str());
        }
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--inspect")) {
        if (!p.load(argv[2], why)) { fprintf(stderr, "package rejected: %s\n", why.c_str()); return 1; }
        printf("{\"bones\":%u,\"components\":%zu,\"streams\":%zu,\"sections\":%zu,\"draws\":%zu,\"payload_bytes\":%zu,\"required\":[",
               p.bones, p.components.size(), p.streams.size(), p.sections.size(), p.draws.size(), static_cast<size_t>(p.payload_bytes));
        for (size_t i = 0; i < p.lists.size(); ++i) printf("%s%llu", i ? "," : "", static_cast<unsigned long long>(p.lists[i].required));
        printf("],\"materials\":%s,\"images\":%zu,\"profiles\":%zu}\n", p.materials ? "true" : "false", p.images.size(), p.profiles.size());
        return 0;
    }
    {
        // D-079: the original Core's rules on a vertex module's registers.
        using efmi::gpu::SourceRegister;
        std::vector<SourceRegister> game = common_vertex_registers();
        auto facts = efmi::gpu::core_source_facts(game);
        check(facts.vs_config_slot == 2 && facts.rolling_bones && facts.config && facts.config->set == 2 && facts.bones->binding == 17,
              "the game's common vertex layout: the instance window at vs-cb2, the rolling bones at vs-t0");
        game[2].bytes = 448; game[1].bytes = 65536;
        facts = efmi::gpu::core_source_facts(game);
        check(facts.vs_config_slot == 1 && facts.rolling_bones, "Component_ReadConfig's order: vs-cb2, then vs-cb1, then vs-cb3");
        game[1].bytes = 3328;
        facts = efmi::gpu::core_source_facts(game);
        check(facts.vs_config_slot == -1 && !facts.rolling_bones, "no instance window: no facts, the Core leaves the mesh to the game");
        game[1].bytes = 65536; game[4].stride = 64;
        facts = efmi::gpu::core_source_facts(game);
        check(facts.vs_config_slot == 1 && !facts.rolling_bones, "vs-t0 of another element size is not the rolling bone buffer");
        game[4].stride = 16; game[4].kind = SourceRegister::ImageSrv;
        check(!efmi::gpu::core_source_facts(game).rolling_bones, "an image at vs-t0 is not the rolling bone buffer");
    }
    {
        // D-124: the DX11 declaration of a t register, as the original's ByteCodeReader reads it.
        using efmi::gpu::SourceRegister;
        auto t = [](uint32_t index, SourceRegister::Kind kind, uint32_t count = 1) {
            SourceRegister r; r.kind = kind; r.letter = 't'; r.index = index; r.count = count; return r;
        };
        std::vector<SourceRegister> shader = {t(0, SourceRegister::BufferSrv), t(1, SourceRegister::BufferSrv), t(2, SourceRegister::TexelSrv),
                                              t(3, SourceRegister::ImageSrv, 2), t(5, SourceRegister::ImageSrv), t(6, SourceRegister::ImageSrv),
                                              t(7, SourceRegister::ImageSrv), t(8, SourceRegister::ImageSrv)};
        shader[0].stride = 16;
        shader[1].stride = 4; shader[1].words = true;
        shader[3].dim = 1;
        shader[4].dim = 3; shader[4].arrayed = true;
        shader[5].dim = 1; shader[5].arrayed = true; shader[5].multisampled = true;
        shader[6].dim = 2;
        shader[7].dim = 2; shader[7].arrayed = true;
        auto is = [&](unsigned slot, uint32_t type, uint32_t dimension, uint32_t stride) {
            uint32_t a = 0, b = 0, c = 0; std::string why;
            return srv_of(shader, slot, a, b, c, why) && a == type && b == dimension && c == stride;
        };
        auto unknown = [&](unsigned slot) { uint32_t a = 0, b = 0, c = 0; std::string why; return !srv_of(shader, slot, a, b, c, why) && !why.empty(); };
        check(is(0, 2, 1, 16), "a read-only buffer of 16-byte elements is dcl_resource_structured with its stride (vs->srv_stride(0) == 16)");
        check(unknown(1), "A buffer of uint words cannot identify a raw or structured original declaration");
        check(is(2, 1, 1, 0), "a texel buffer is a typed buffer view");
        check(is(3, 1, 4, 0) && is(4, 1, 4, 0), "each element of an image array declares its own typed 2D register");
        check(is(5, 1, 10, 0) && is(6, 1, 7, 0) && is(7, 1, 8, 0), "cube arrays, multisampled 2D arrays and 3D images by their view dimensions");
        check(unknown(8), "an arrayed 3D image has no DX11 view");
        check(is(9, 0, 0, 0) && is(59, 0, 0, 0), "a slot below t60 without a register is not declared, as the DX11 build declares none there");
        check(unknown(60) && unknown(127), "a slot from t60 up without a register is not known: the DX11 builds declare more than their SPIR-V there");
    }
    Model m;
    const auto valid = package(&m);
    check(p.decode(valid, why) && p.bones == 5 && p.components.size() == 2 && p.draws.size() == 2 && p.payload.size() == m.payload.size(),
          "valid package accepted");
    check(p.components[0].has_remap && p.components[0].remap_lod1 == std::vector<uint16_t>({1, 0}) && !p.components[1].has_remap,
          "LOD 1 remap read for the component that has one");
    check(p.lists[0].required == 3 && p.lists[2].required == 0 && p.lists[0].count == 2 && p.lists[2].count == 0,
          "a draw list requires exactly the components whose bones carry weight; a hidden component requires none");
    check(p.sections[0].match_first == 0 && p.sections[1].match_first == kCharacterNone && p.sections[1].lod == kCharacterNone,
          "sections keep an unspecified first index and an unassigned LOD");
    {
        // D-141: the original Core sizes its pools by the Mod's component count; 40 components load and a
        // list's bones of the 40th are a bit of its mask.
        CharacterPackage wide;
        check(wide.decode(package(nullptr, 38), why) && wide.components.size() == 40 && wide.lists[0].required == (1ull | 1ull << 39),
              "a character of 40 components loads, and its list requires the 40th component's bones");
    }
    const auto kept = p.payload;
    const size_t tables = 48, payload = 48 + m.tables.size();
    {
        auto hidden = valid;
        poke(hidden, tables + m.section_table + 12, kCharacterNone);
        poke(hidden, tables + m.section_table + 16, kCharacterNone);
        seal(hidden);
        CharacterPackage independent;
        check(independent.decode(hidden, why) && independent.sections[0].component == kCharacterNone,
              "exact IB-only hide does not need a fabricated bone component");
        poke(hidden, tables + m.section_table + 16, uint32_t(0)); seal(hidden);
        check(!independent.decode(hidden, why), "IB-only hide cannot change the character LOD");
    }
    auto reject = [&](std::vector<uint8_t> data, const char* text, bool reseal = true) {
        if (reseal) seal(data);
        std::string reason;
        check(!p.decode(data, reason) && !reason.empty() && p.payload == kept, text);
    };
    { auto d = valid; d[0] = 'X'; reject(d, "wrong magic refused", false); }
    { auto d = valid; d.back() ^= 1; reject(d, "checksum mismatch refused", false); }
    { auto d = valid; d.pop_back(); reject(d, "truncated file refused"); }
    { auto d = valid; d.push_back(0); reject(d, "trailing bytes refused"); }
    { auto d = valid; poke(d, 8, uint32_t(65536)); reject(d, "bone count past 16-bit indices refused"); }
    { auto d = valid; poke(d, tables + m.component_table + 0, uint32_t(3)); reject(d, "components out of the order of their first bones refused"); }
    {
        // BUG-093: the second component (three bones) from the first one's first bone on, as the Core's
        // pools allow. The two bones they share are one span, which either component's import fills; the
        // second's third bone is a span of its own.
        auto d = valid; poke(d, tables + m.component_table + 24, uint32_t(0)); seal(d);
        CharacterPackage shared; std::string reason;
        check(shared.decode(d, reason) && shared.spans.size() == 2 && shared.spans[0].first == 0 && shared.spans[0].count == 2 &&
              shared.spans[0].cover == 3 && shared.spans[1].first == 2 && shared.spans[1].count == 1 && shared.spans[1].cover == 2,
              "two components on the same bones decode: one span of both, then the rest of the second");
    }
    { auto d = valid; poke(d, tables + m.component_table + 24, uint32_t(5000)); reject(d, "a component past the skeleton refused"); }
    { auto d = valid; poke(d, tables + m.component_table + 24, uint32_t(3)); poke(d, tables + m.component_table + 28, uint32_t(2)); seal(d);
      CharacterPackage gap; std::string reason;
      check(gap.decode(d, reason) && gap.components.size() == 2 && gap.components[1].first == 3 && gap.components[1].count == 2,
            "bones between components stay as initialized, as the Core leaves them (BUG-048)"); }
    { auto d = valid; poke(d, tables + m.component_table + 12, uint32_t(9)); reject(d, "draw list index out of range refused"); }
    { auto d = valid; poke(d, tables + m.stream_table + 16, uint64_t(1) << 40); reject(d, "stream larger than the payload refused"); }
    { auto d = valid; poke(d, tables + m.stream_table + 32 + 4, uint32_t(3)); reject(d, "index stride other than 2 or 4 refused"); }
    { auto d = valid; poke(d, tables + m.stream_table + 4 * 32 + 24, uint32_t(12)); reject(d, "bone indices outside their element refused"); }
    { auto d = valid; poke(d, tables + m.stream_table + 4 * 32 + 28, uint32_t(4)); reject(d, "weights overlapping the indices refused"); }
    { auto d = valid; poke(d, tables + m.section_table + 12, uint32_t(2)); reject(d, "section naming a missing component refused"); }
    { auto d = valid; poke(d, tables + m.section_table + 16, uint32_t(2)); reject(d, "section with LOD 2 refused"); }
    { auto d = valid; poke(d, tables + m.list_table + 4, uint32_t(3)); reject(d, "draw list past the draws refused"); }
    { auto d = valid; poke(d, tables + m.draw_table + 4, uint32_t(4)); reject(d, "draw past its index stream refused"); }
    { auto d = valid; poke(d, tables + m.draw_table + 12, int32_t(1)); reject(d, "base vertex that leaves a vertex stream refused"); }
    { auto d = valid; poke(d, tables + m.draw_table + 12, int32_t(-1)); reject(d, "negative vertex index refused"); }
    { auto d = valid; poke(d, tables + m.draw_table + 16, uint32_t(1)); reject(d, "draw binding an index stream as vertices refused"); }
    { auto d = valid; poke(d, tables + m.edit_table + 8, uint32_t(VK_FORMAT_R32G32B32A32_UINT)); reject(d, "layout edit outside the rule refused"); }
    { auto d = valid; poke(d, payload + m.index_data + 8, uint32_t(3)); reject(d, "index past the vertex count refused"); }
    { auto d = valid; poke(d, payload + m.blend_a + 8, uint16_t(5)); reject(d, "weighted bone outside the skeleton refused"); }
    { auto d = valid; poke(d, payload + m.blend_a + 14, uint16_t(5)); reject(d, "unweighted bone outside the skeleton refused"); }
    { auto d = valid; poke(d, payload + m.blend_b, uint16_t(100)); reject(d, "weights that do not sum to one refused"); }
    { auto d = valid; poke(d, payload + m.remap_data, uint16_t(2000)); reject(d, "remap source outside the window refused"); }
    check(p.decode(valid, why), "valid package still accepted after the refusals");
    declaration_tests(p);
    bone_layout_tests();
    memory_validation_tests(valid, m);
    payload_tests(valid, m);
    material_tests(valid);
    register_tests();
    texture_role_tests();
    check(!is_character_package("character-test-missing.bin"), "a missing file is not a character package");
    printf("character package: %d checks passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
