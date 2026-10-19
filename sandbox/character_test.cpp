// Bounded decoding of a character package and the vertex declaration it asks for.
// "--inspect <file>" runs the production decoder on a real package and prints what it holds.
// "--derived <package> <fragment.spv> <slot:kind:set:binding>..." prepares that fragment module with
// those rules as the backend does for a shader without a profile, and prints what each draw gets and
// the DX11 registers the rule of D-076 gives the module's resources.
#include "../layer/src/character_package.h"
#include "../layer/src/character_materials.h"
#include "../frontend/gpu/source_registers.h"
#include "../layer/src/json.h"
#include "variant_fixture.h"
#include <cstdio>
#include <fstream>

using namespace efvk;
static int passed = 0, failed = 0;
static void check(bool ok, const char* text) { if (ok) ++passed; else { ++failed; printf("FAIL: %s\n", text); } }
template <class T> static void put(std::vector<uint8_t>& data, T value)
{
    const auto* p = reinterpret_cast<const uint8_t*>(&value);
    data.insert(data.end(), p, p + sizeof(value));
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
static std::vector<uint8_t> package(Model* out = nullptr)
{
    Model m;
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
        for (uint16_t value : {uint16_t(40000), uint16_t(25535), uint16_t(0), uint16_t(0), uint16_t(0), uint16_t(3), uint16_t(0), uint16_t(0)}) put(blend_b, value);
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
    m.stream_table = t.size();
    for (const auto& e : table) { put(t, e.kind); put(t, e.stride); put(t, e.offset); put(t, e.size); put(t, e.index_offset); put(t, e.weight_offset); }
    m.section_table = t.size();
    for (uint32_t value : {0x11111111u, 3u, 0u, 0u, 0u}) put(t, value);
    for (uint32_t value : {0x22222222u, 12u, kCharacterNone, 1u, kCharacterNone}) put(t, value);
    m.list_table = t.size();
    for (uint32_t value : {0u, 2u, 0u, 2u, 2u, 0u, 2u, 0u}) put(t, value);
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

static void declaration_tests(const CharacterPackage& p)
{
    VertexInputRecipe source, result;
    std::string why;
    source.bindings = {{0, 16, VK_VERTEX_INPUT_RATE_VERTEX}, {1, 8, VK_VERTEX_INPUT_RATE_VERTEX}, {2, 12, VK_VERTEX_INPUT_RATE_VERTEX},
                       {3, 16, VK_VERTEX_INPUT_RATE_VERTEX}, {4, 0, VK_VERTEX_INPUT_RATE_VERTEX}};
    source.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 1, VK_FORMAT_R32G32_SFLOAT, 0}, {2, 2, VK_FORMAT_R16G16B16A16_UNORM, 0},
                         {3, 2, VK_FORMAT_R8G8B8A8_UINT, 8}, {4, 4, VK_FORMAT_R8G8B8A8_UNORM, 12}};
    check(p.vertex_input(p.draws[0], source, result, why) && result.bindings[2].stride == 16 && result.bindings[1].stride == 8 &&
          result.bindings[4].stride == 0 && result.attributes[3].format == VK_FORMAT_R16G16B16A16_UINT && result.attributes[3].offset == 8 &&
          result.attributes[2].format == VK_FORMAT_R16G16B16A16_UNORM && result.attributes[4].binding == 4,
          "12-byte blend stream widened to 16; other streams and the default attribute untouched");
    auto narrow = source;
    narrow.bindings[2].stride = 4; narrow.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {3, 2, VK_FORMAT_R8G8B8A8_UINT, 0}};
    auto small = p;
    small.streams[small.draws[0].vb[2]].stride = 8;
    check(small.vertex_input(small.draws[0], narrow, result, why) && result.bindings[2].stride == 8 &&
          result.attributes[1].format == VK_FORMAT_R16G16B16A16_UINT, "4-byte index-only stream widened to 8");
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
    if (argc == 3 && !strcmp(argv[1], "--inspect")) {
        if (!p.load(argv[2], why)) { fprintf(stderr, "package rejected: %s\n", why.c_str()); return 1; }
        printf("{\"bones\":%u,\"components\":%zu,\"streams\":%zu,\"sections\":%zu,\"draws\":%zu,\"payload_bytes\":%zu,\"required\":[",
               p.bones, p.components.size(), p.streams.size(), p.sections.size(), p.draws.size(), p.payload.size());
        for (size_t i = 0; i < p.lists.size(); ++i) printf("%s%u", i ? "," : "", p.lists[i].required);
        printf("],\"materials\":%s,\"images\":%zu,\"profiles\":%zu}\n", p.materials ? "true" : "false", p.images.size(), p.profiles.size());
        return 0;
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
    { auto d = valid; poke(d, 8, uint32_t(5000)); reject(d, "bone count outside limits refused"); }
    { auto d = valid; poke(d, tables + m.component_table + 24, uint32_t(3)); reject(d, "components with a gap refused"); }
    { auto d = valid; poke(d, tables + m.component_table + 4, uint32_t(1)); reject(d, "components that do not cover the skeleton refused"); }
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
    material_tests(valid);
    register_tests();
    check(!is_character_package("character-test-missing.bin"), "a missing file is not a character package");
    printf("character package: %d checks passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
