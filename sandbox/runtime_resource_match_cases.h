#include "../frontend/xxmi/resource_match.h"

struct GeneralMatchExtension final : RuntimeExtension {
    DrawPlanAdapter::ResourceMatchQuery source;
    unsigned failures=0;
    std::vector<std::wstring> install(DrawPlanAdapter& adapter) override {adapter.resource_matches(source);return {};}
    void execution_failed() noexcept override {++failures;}
};
static RuntimeShaderCall resource_match_call(){RuntimeShaderCall call;call.shaders=known_source_shaders();
    call.shaders.stages[0]={true,true,true,9,{}};call.call.type=DrawCall::DrawIndexed;call.call.IndexCount=6;call.call.FirstIndex=3;
    call.call.InstanceCount=1;call.original=[](CommandListState&,std::wstring&){return true;};return call;}
static SourceResourceMatch tracked_buffer_match(uint32_t hash=0x12345678){
    SourceResourceMatch source;source.bound=true;source.identity.tracking=SourceResourceIdentity::Tracking::Tracked;
    source.identity.hash_known=true;source.identity.hash=hash;
    source.description=D3D11_BUFFER_DESC{128,D3D11_USAGE_DEFAULT,D3D11_BIND_INDEX_BUFFER|D3D11_BIND_VERTEX_BUFFER,0,0,0};
    source.offset=8;source.stride=16;source.format=DXGI_FORMAT_R16_UINT;return source;
}
static void runtime_resource_match_cases(const std::filesystem::path& folder){
    const auto root=std::filesystem::absolute(folder/"resource-matching");std::filesystem::create_directories(root);
    for(unsigned mode=0;mode<14;++mode){
        const auto path=root/(std::to_string(mode)+".ini");const auto target=mode>=12?"vb2":"ib";
        const std::string text=std::string("namespace = resource_matching\n[Rendering]\ntrack_region_hashes = ")+(mode==0 || mode==7?"0":"1")+R"ini(
[Constants]
global $trace = 0
global $filter = 0
[ShaderOverrideRoot]
hash = 9
)ini"+"$filter = "+target+"\nchecktextureoverride = "+target+R"ini(
[TextureOverrideHash]
hash = 12345678
match_index_count = 6
match_priority = 100
filter_index = 17
$trace = $trace * 10 + 1
post $trace = $trace * 10 + 3
[TextureOverrideRegion]
hash = 22222222
match_index_count = 6
filter_index = 21
$trace = $trace * 10 + 5
post $trace = $trace * 10 + 6
[TextureOverrideDescriptor]
match_type = buffer
match_byte_width = 128
match_bind_flags = +index_buffer +vertex_buffer
match_priority = -100
filter_index = 31
$trace = $trace * 10 + 2
post $trace = $trace * 10 + 4
)ini";
        write(path,text.c_str());auto extension=std::make_shared<GeneralMatchExtension>();unsigned region_reads=0;
        extension->source=[&](ResourceCopyTarget& actual,CommandListState&,SourceResourceMatch& source,std::wstring&){
            check(actual.type==(mode>=12?ResourceCopyTargetType::VERTEX_BUFFER:ResourceCopyTargetType::INDEX_BUFFER),"General matcher receives the original IB/VB target");
            source=tracked_buffer_match();
            if(mode==8)source.identity={SourceResourceIdentity::Tracking::Untracked};
            else if(mode==9)source.bound=false;
            else{
                if(mode==5 || mode==6 || mode==7){source.identity.hash_known=false;source.identity.hash=0;}
                if(mode!=10)source.identity.region_hash=[&](UINT offset,UINT size,uint32_t& hash,std::wstring& why){++region_reads;
                    const unsigned expected_offset=mode>=12?72:14,expected_size=mode==12?48:mode==13?64:12;
                    check(offset==expected_offset && size==expected_size,"Original source helpers determine the exact requested buffer region");
                    if(mode==4){why=L"Source bytes unavailable";return false;}hash=mode==2 || mode==6?0:0x22222222;return true;};
            }return true;
        };
        RuntimeConfig config{root,{path},root/"settings","resource"+std::to_string(mode)};config.shader_hooks=true;std::string why;
        auto runtime=IniRuntime::load(config,extension,why);if(!runtime)throw std::runtime_error(why);
        auto call=resource_match_call();if(mode==3)call.call.IndexCount=99;if(mode==11)call.call.IndexCount=0;
        if(mode>=12){call.call.FirstVertex=4;call.call.VertexCount=mode==12?3:0;}
        unsigned original=0;call.original=[&](CommandListState&,std::wstring&){++original;return true;};
        const bool success=runtime->intercept(call,why),failure=mode==4 || mode==6 || mode==7 || mode==10;
        if(success==failure)throw std::runtime_error("Resource region mode "+std::to_string(mode)+": "+why);
        check(success!=failure && original==unsigned(!failure),"Unknown resource facts refuse before the original operation");
        if(failure){check(extension->failures==1,"Resource matching failure poisons the enclosing root");continue;}
        const auto values=runtime->status().variables;const float trace=mode==8 || mode==9?0:mode==3 || mode==11?24:mode==0 || mode==2?1234:5264;
        if(values.at("$\\resource_matching\\trace")!=trace)throw std::runtime_error("Resource match trace mode "+std::to_string(mode)+": actual "+
            std::to_string(values.at("$\\resource_matching\\trace"))+", expected "+std::to_string(trace));++checks;
        const float filter=values.at("$\\resource_matching\\filter");
        check(filter==(mode==8 || mode==9?0:31) && std::signbit(filter)==(mode==9),"Resource filter retains final native match priority and signed unbound zero");
        check(region_reads==(mode==0 || mode==3 || mode==8 || mode==9 || mode==11?0:3),"Region hashes are requested only after original draw-info prefiltering");
    }
    // Keep the dynamic target for this: changing the variable within its first
    // match must change the source slot seen by the following this expression.
    {
        const auto path=root/"dynamic.ini";write(path,R"ini(namespace = dynamic_match
[Constants]
global $slot = 2
global $seen = 0
global $trace = 0
[ShaderOverrideRoot]
hash = 9
pre checktextureoverride = vs-t[$slot]
[TextureOverrideFirst]
hash = 12345678
$slot = 3
$seen = this
$trace = $trace * 10 + 1
[TextureOverrideOther]
hash = 22222222
filter_index = 73
)ini");
        auto extension=std::make_shared<GeneralMatchExtension>();std::vector<unsigned> slots;
        extension->source=[&](ResourceCopyTarget& target,CommandListState& state,SourceResourceMatch& source,std::wstring&){
            check(target.type==ResourceCopyTargetType::SHADER_RESOURCE && target.shader_type==L'v',"Non-pixel resource targets reach the general matcher");
            const auto slot=target.ResolveSlot(&state);slots.push_back(slot);source=tracked_buffer_match(slot==2?0x12345678:0x22222222);return true;};
        RuntimeConfig config{root,{path},root/"settings","dynamic"};config.shader_hooks=true;std::string why;
        auto runtime=IniRuntime::load(config,extension,why);if(!runtime)throw std::runtime_error(why);
        check(runtime->intercept(resource_match_call(),why),"Dynamic checked target executes");
        check(slots==std::vector<unsigned>{2,3} && runtime->status().variables.at("$\\dynamic_match\\seen")==73,
            "this re-evaluates the original dynamic slot after a matched command changes it");
    }
    {
        const auto path=root/"region-this.ini";write(path,R"ini(namespace = region_this
[Rendering]
track_region_hashes = 1
[Constants]
global $inside = 0
[ShaderOverrideRoot]
hash = 9
pre checktextureoverride = ib
[TextureOverrideFull]
hash = 12345678
match_index_count = 6
filter_index = 17
[TextureOverrideRegion]
hash = 22222222
match_index_count = 6
filter_index = 21
$inside = this
)ini");
        auto extension=std::make_shared<GeneralMatchExtension>();unsigned regions=0;
        extension->source=[&](ResourceCopyTarget&,CommandListState&,SourceResourceMatch& source,std::wstring&){source=tracked_buffer_match();
            source.identity.region_hash=[&](UINT,UINT,uint32_t& hash,std::wstring&){++regions;hash=0x22222222;return true;};return true;};
        RuntimeConfig config{root,{path},root/"settings","region-this"};config.shader_hooks=true;std::string why;
        auto runtime=IniRuntime::load(config,extension,why);if(!runtime)throw std::runtime_error(why);
        check(runtime->intercept(resource_match_call(),why),"Region match can query its current this target");
        check(regions==1 && runtime->status().variables.at("$\\region_this\\inside")==17,
            "Native this resolves the buffer but uses whole-resource matching instead of IA region matching");
    }
    for(unsigned mode=0;mode<3;++mode){
        const auto path=root/("slot-guard"+std::to_string(mode)+".ini");write(path,R"ini(namespace = slot_guard
[Constants]
global $slot = 0
global $value = 0
[ShaderOverrideRoot]
hash = 9
$value = ps-t[$slot]
)ini");
        auto extension=std::make_shared<GeneralMatchExtension>();unsigned resolved=0;
        extension->source=[&](ResourceCopyTarget& target,CommandListState& state,SourceResourceMatch& source,std::wstring&){
            target.ResolveSlot(&state);++resolved;source={};return true;};
        RuntimeConfig config{root,{path},root/"settings","slot-guard"+std::to_string(mode)};config.shader_hooks=true;std::string why;
        auto runtime=IniRuntime::load(config,extension,why);if(!runtime)throw std::runtime_error(why);
        auto call=resource_match_call();call.original=[&](CommandListState&,std::wstring&){command_list_globals.at(L"$\\slot_guard\\slot").fval=
            mode==0?std::numeric_limits<float>::quiet_NaN():mode==1?std::numeric_limits<float>::infinity():-std::numeric_limits<float>::infinity();return true;};
        check(runtime->intercept(call,why) && resolved==1,"Finite resource slot reaches the original resolver");
        check(!runtime->intercept(call,why) && resolved==1 && extension->failures==1,"Non-finite resource slots refuse before native unsigned conversion");
    }
}
