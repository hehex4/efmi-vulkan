// Included by the standalone asset fixture. No game or source-asset mutation.
#include "../frontend/gpu/material_plan.h"
static efmi::gpu::MaterialPlan material_plan_tests(const FileBinding& red,const FileBinding& zero)
{
    using namespace efmi::gpu;
    const auto code=material_fixture::shader(false);std::string reason;
    const std::vector<MaterialSlot> rules={{15,MaterialSlotKind::Image,{1,6}},{17,MaterialSlotKind::Absent,{}}};
    MaterialProfile profile;
    check(profile.prepare(code,rules,reason),"material profile validates an explicit logical-to-physical correspondence");
    FileDraws draws;draws.draws.resize(4);draws.draws[0].pixel[15]=red;draws.draws[1].pixel[15]=zero;
    draws.draws[2].pixel[15].source=BindingSource::Null;
    draws.draws[3].pixel[17].source=BindingSource::File;
    MaterialPlan output;
    check(resolve_material_plan(draws,code,&profile,output,reason) && output.draws.size()==4 &&
          output.draws[0].images.size()==1 && output.draws[0].images[0].view==red.image->view() &&
          output.draws[1].images[0].view==zero.image->view(),"different draws resolve their own Ready image owners");
    check(output.draws[2].patch.nulls.size()==1 && output.draws[2].images.empty() &&
          output.draws[3].patch.images.empty() && output.draws[3].patch.nulls.empty(),
          "explicit null and shader-undeclared slots stay distinct");
    const auto saved=output;
    auto reject=[&](const FileDraws& input,const std::vector<uint32_t>& shader,const MaterialProfile* map,const char* label){
        check(!resolve_material_plan(input,shader,map,output,reason) && !reason.empty() &&
              output.draws.size()==saved.draws.size() && output.draws[0].images[0].view==red.image->view(),label);
    };
    auto changed=code;changed[2]^=1;
    reject(draws,changed,&profile,"a different exact shader refuses even with an unchanged interface");
    reject(draws,code,nullptr,"missing correspondence cannot drop requested material writes");
    auto bad=draws;bad.draws[3].pixel[18].source=BindingSource::Null;
    reject(bad,code,&profile,"a late unknown slot refuses all earlier resolved draws");
    bad=draws;bad.draws[1].pixel[15].image.reset();
    reject(bad,code,&profile,"a late unready material cannot publish a partial transaction");
    bad=draws;bad.draws[1].pixel[15].source=BindingSource(77);
    reject(bad,code,&profile,"invalid binding-source enum refuses instead of inheriting");
    bad=draws;bad.draws.resize(257);
    reject(bad,code,&profile,"material plan enforces the backend transaction draw budget");
    for(auto bad_rules:{std::vector<MaterialSlot>{{15,MaterialSlotKind::Image,{1,6}},{15,MaterialSlotKind::Absent,{}}},
                        std::vector<MaterialSlot>{{15,MaterialSlotKind::Image,{1,6}},{16,MaterialSlotKind::Image,{1,6}}},
                        std::vector<MaterialSlot>{{15,MaterialSlotKind::Image,{1,99}}},
                        std::vector<MaterialSlot>{{128,MaterialSlotKind::Absent,{}}}}){
        check(!profile.prepare(code,bad_rules,reason) && resolve_material_plan(draws,code,&profile,output,reason),
              "invalid or aliased profile preparation preserves the previous validated profile");
    }
    {
        // The same image declared as an integer image: neither a file nor a null can be patched in.
        // The profile still serves draws that do not bind it; one that does is refused (D-082: the
        // slots are those of every draw of a Mod, and one shader's odd image must not refuse the rest).
        auto integer=code;uint32_t int_type=0;
        for(size_t at=5;at<integer.size();at+=integer[at]>>16)if((integer[at]&65535)==21){int_type=integer[at+1];break;}
        bool changed=false;
        for(size_t at=5;at<integer.size() && int_type;at+=integer[at]>>16)
            if((integer[at]&65535)==25){integer[at+2]=int_type;changed=true;break;}
        MaterialProfile odd;
        check(changed && odd.prepare(integer,rules,reason),"an image slot that takes neither a file nor a null no longer refuses the profile");
        FileDraws untouched;untouched.draws.resize(1);untouched.draws[0].pixel[17].source=BindingSource::File;MaterialPlan kept;
        check(resolve_material_plan(untouched,integer,&odd,kept,reason) && kept.draws[0].images.empty(),
              "a draw that does not bind that slot still resolves");
        FileDraws touching;touching.draws.resize(1);touching.draws[0].pixel[15]=red;
        check(!resolve_material_plan(touching,integer,&odd,kept,reason) && !reason.empty(),"a draw that binds that slot is refused");
        check(!odd.prepare(integer,{{15,MaterialSlotKind::Image,{1,99}}},reason),"a binding the shader does not declare still refuses");
    }
    auto volume=code;
    for(size_t at=5;at<volume.size();at+=volume[at]>>16)if((volume[at]&65535)==25){volume[at+3]=2;break;}
    MaterialProfile volume_profile;
    check(volume_profile.prepare(volume,rules,reason),"3D correspondence supports explicit null without authorizing 2D files");
    reject(draws,volume,&volume_profile,"2D file for 3D shader refuses without an inferred-null policy");
    FileDraws null_only;null_only.draws.push_back(draws.draws[2]);MaterialPlan nulled;
    check(resolve_material_plan(null_only,volume,&volume_profile,nulled,reason) && nulled.draws[0].patch.nulls.size()==1,
          "an explicit null still works for a validated 3D image");
    FileDraws inherited;inherited.draws.emplace_back();MaterialPlan unchanged;
    check(resolve_material_plan(inherited,code,nullptr,unchanged,reason) && unchanged.draws[0].images.empty(),
          "application-only material bindings need no guessed correspondence");
    const auto probe=material_fixture::null_probe();MaterialProfile multi;
    check(multi.prepare(probe,{{18,MaterialSlotKind::Image,{1,6}},{15,MaterialSlotKind::Image,{1,8}}},reason),
          "logical slot order is independent of physical binding order");
    auto dual=draws;dual.draws.resize(1);dual.draws[0].pixel[18]=zero;
    check(resolve_material_plan(dual,probe,&multi,nulled,reason) && nulled.draws[0].patch.images[0].binding==8 &&
          nulled.draws[0].patch.images[1].binding==6 && nulled.draws[0].images[0].view==red.image->view() &&
          nulled.draws[0].images[1].view==zero.image->view(),"private image descriptors retain the same deterministic order as their contracts");
    std::weak_ptr<const void> owner=output.draws[0].images[0].owner;
    check(!owner.expired() && output.draws[0].images[0].owner.get()==red.image.get(),"material snapshots retain the exact upload owner");
    return saved;
}

static int material_profile_file(const wchar_t* shader_path,const wchar_t* request_path,const wchar_t* output_path)
{
    using namespace efmi::gpu;
    try{
        auto read=[](const wchar_t* path,size_t limit){
            std::ifstream f(std::filesystem::path(path),std::ios::binary|std::ios::ate);const auto size=f.tellg();
            if(!f || size<0 || uint64_t(size)>limit)throw std::runtime_error("Material fixture file budget");
            std::vector<uint8_t> data(static_cast<size_t>(size));f.seekg(0);f.read(reinterpret_cast<char*>(data.data()),size);
            if(!f)throw std::runtime_error("Material fixture read failed");return data;
        };
        const auto bytes=read(shader_path,16u<<20),input=read(request_path,2u<<20);
        if(bytes.size()%4)throw std::runtime_error("Unaligned fragment bytecode");
        std::vector<uint32_t> code(bytes.size()/4);memcpy(code.data(),bytes.data(),bytes.size());size_t at=0;
        auto take=[&](size_t size){if(size>input.size()-at)throw std::runtime_error("Truncated material request");const auto* p=input.data()+at;at+=size;return p;};
        auto u32=[&](){uint32_t value;memcpy(&value,take(4),4);return value;};
        if(memcmp(take(8),"EFMIMP01",8))throw std::runtime_error("Bad material request magic");
        const auto count=u32();if(count>128)throw std::runtime_error("Too many material profile rules");std::vector<MaterialSlot> rules;
        for(uint32_t i=0;i<count;++i){MaterialSlot r;r.slot=u32();r.kind=MaterialSlotKind(u32());r.image={u32(),u32()};rules.push_back(r);}
        MaterialProfile profile;std::string reason;if(!profile.prepare(code,rules,reason))throw std::runtime_error(reason);
        const auto cases=u32();if(!cases || cases>4096)throw std::runtime_error("Material fixture case budget");
        std::ofstream out{std::filesystem::path(output_path)};
        for(uint32_t i=0;i<cases;++i){
            std::array<BindingSource,128> bindings;const auto* raw=take(bindings.size());
            for(size_t j=0;j<bindings.size();++j)bindings[j]=BindingSource(raw[j]);
            MaterialContract contract;const bool ok=profile.select(code,bindings,contract,reason);
            out<<"{\"ok\":"<<(ok?"true":"false")<<",\"images\":[";
            for(size_t j=0;j<contract.patch.images.size();++j){if(j)out<<',';const auto b=contract.patch.images[j];out<<'['<<contract.image_slots[j]<<','<<b.set<<','<<b.binding<<']';}
            out<<"],\"nulls\":[";
            for(size_t j=0;j<contract.patch.nulls.size();++j){if(j)out<<',';const auto b=contract.patch.nulls[j];out<<'['<<b.set<<','<<b.binding<<']';}
            out<<"],\"reason\":\""<<reason<<"\"}\n";
        }
        if(at!=input.size() || !out)throw std::runtime_error("Material request trailing bytes or write failed");return 0;
    }catch(const std::exception& e){fprintf(stderr,"Material profile fixture: %s\n",e.what());return 1;}
}
