#include "globals.h"
#include "../frontend/xxmi/resource_match.h"

static void native_resource_match_reference(Device& d,const std::filesystem::path& root){
    ShaderFixture fixture(d,root,R"ini(namespace = compute
[TextureOverrideZero]
hash = 00000000
filter_index = 99
[TextureOverrideHash]
hash = 12345678
match_priority = 100
filter_index = 17
[TextureOverrideDraw]
hash = 12345678
match_priority = 110
match_index_count = 6
match_first_index = 3
match_instance_count = 2
filter_index = 19
[TextureOverrideBuffer]
match_type = buffer
match_byte_width = 128
match_priority = -100
filter_index = 31
[TextureOverrideLine]
match_type = texture1d
match_width = 8
filter_index = 41
[TextureOverrideImage]
match_type = texture2d
match_width = 8
match_height = 4
filter_index = 51
[TextureOverrideVolume]
match_type = texture3d
match_width = 8
match_height = 4
match_depth = 2
filter_index = 61
)ini",true);
    ImageNativeReference native;using Ptr=ImageNativeReference::Ptr<ID3D11Resource>;
    std::vector<std::pair<Ptr,frontend::SourceResourceDescription>> resources;
    {
        D3D11_BUFFER_DESC desc{128,D3D11_USAGE_DEFAULT,D3D11_BIND_VERTEX_BUFFER,0,0,0};ImageNativeReference::Ptr<ID3D11Buffer> buffer;
        check(SUCCEEDED(native.device->CreateBuffer(&desc,nullptr,&buffer)),"Native matcher reference creates a real buffer");resources.push_back({buffer,desc});
        desc.ByteWidth=64;buffer.Reset();check(SUCCEEDED(native.device->CreateBuffer(&desc,nullptr,&buffer)),"Native matcher creates a nonmatching buffer");resources.push_back({buffer,desc});
    }
    {
        D3D11_TEXTURE1D_DESC desc{8,1,1,DXGI_FORMAT_R32_UINT,D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE,0,0};ImageNativeReference::Ptr<ID3D11Texture1D> image;
        check(SUCCEEDED(native.device->CreateTexture1D(&desc,nullptr,&image)),"Native matcher creates a real 1D texture");resources.push_back({image,desc});
    }
    {
        D3D11_TEXTURE2D_DESC desc{8,4,1,1,DXGI_FORMAT_R32_UINT,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE,0,0};ImageNativeReference::Ptr<ID3D11Texture2D> image;
        check(SUCCEEDED(native.device->CreateTexture2D(&desc,nullptr,&image)),"Native matcher creates a real 2D texture");resources.push_back({image,desc});
    }
    {
        D3D11_TEXTURE3D_DESC desc{8,4,2,1,DXGI_FORMAT_R32_UINT,D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE,0,0};ImageNativeReference::Ptr<ID3D11Texture3D> image;
        check(SUCCEEDED(native.device->CreateTexture3D(&desc,nullptr,&image)),"Native matcher creates a real 3D texture");resources.push_back({image,desc});
    }
    frontend::RuntimeShaderCall call;call.call.type=DrawCall::Draw;for(auto& stage:call.shaders.stages)stage.known=true;
    call.original=[&](CommandListState& state,std::wstring&){
        for(const auto& resource:resources)for(unsigned identity=0;identity<4;++identity)for(unsigned context=0;context<4;++context){
            auto* handle=resource.first.Get();G->mResources.erase(handle);
            frontend::SourceResourceMatch source;source.bound=true;source.description=resource.second;
            if(identity){source.identity.tracking=frontend::SourceResourceIdentity::Tracking::Tracked;
                source.identity.hash_known=true;source.identity.hash=identity==1?0:identity==2?0x12345678:0xabcdef01;
                G->mResources[handle].hash=source.identity.hash;
            }else source.identity.tracking=frontend::SourceResourceIdentity::Tracking::Untracked;
            DrawCallInfo draw;draw.type=DrawCall::DrawIndexed;draw.IndexCount=context&1?6:7;draw.FirstIndex=3;draw.InstanceCount=context&2?2:1;
            state.call_info=&draw;TextureOverrideMatches expected,actual;
            // Calls the actual original cached command matcher on a real COM
            // resource, independently resolving its type/description and cache.
            find_texture_overrides_for_resource(handle,&expected,&draw);
            frontend::source_resource_matches(source,ResourceCopyTargetType::SHADER_RESOURCE,state,actual);
            check(actual==expected,"Source snapshots match exact native cached hash/descriptor/draw-context lists and order");
            if(identity==2 && std::holds_alternative<D3D11_BUFFER_DESC>(source.description) &&
               std::get<D3D11_BUFFER_DESC>(source.description).ByteWidth==128)
                check(actual.size()>=2 && actual.front()->filter_index==17 && actual.back()->filter_index==31,
                    "Native fuzzy matches follow exact hash matches even with lower numeric priority");
            G->mResources.erase(handle);
        }
        state.call_info=&call.call;return true;
    };
    fixture.backend->resources().begin(d.queue,frontend::GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
    if(!fixture.ini->intercept(call,fixture.why))throw std::runtime_error(fixture.why);fixture.finish();fixture.release();
}
static void owned_resource_matching(Device& d,const std::filesystem::path& root){
    ShaderFixture f(d,root,R"ini(namespace = compute
[Constants]
global $hits = 0
global $post_hits = 0
global $alias = 0
global $copy = 0
global $before = 0
global $after = 0
global $rebind = 0
global $empty = 0
global $dynamic = 0
global $slot = 2
vs-t2 = ref ResourceA
vs-t3 = ref ResourceB
[ResourceA]
type = Buffer
format = R32_UINT
stride = 4
bind_flags = index_buffer vertex_buffer shader_resource unordered_access
data = R32_UINT 1 2 3 4
[ResourceB]
type = Buffer
format = R32_UINT
stride = 4
bind_flags = shader_resource
data = R32_UINT 5 6 7 8
[ResourceAlias]
[ResourceCopy]
bind_flags = shader_resource
[PoolMatches]
pool_size = 3
[TextureOverrideA]
hash = 12345678
filter_index = 17
$hits = $hits + 1
if $rebind
 $before = this
 this = ref ResourceB
 $after = this
endif
post $post_hits = $post_hits + 1
[TextureOverrideB]
hash = 22222222
filter_index = 73
[Present]
$hits = 0
$post_hits = 0
ResourceAlias = ref ResourceA
ResourceCopy = copy ResourceA
$alias = ResourceAlias
$copy = ResourceCopy
PoolMatches[0] = ref ResourceA
PoolMatches[1] = ref ResourceB
PoolMatches[2] = ref ResourceA
pre checktextureoverride = PoolMatches[-1:1]
pre checktextureoverride = vs-t[2:3]
hs-t0 = ref ResourceA
ds-t0 = ref ResourceA
gs-t0 = ref ResourceA
ps-t0 = ref ResourceA
cs-t0 = ref ResourceA
pre checktextureoverride = hs-t0
pre checktextureoverride = ds-t0
pre checktextureoverride = gs-t0
pre checktextureoverride = ps-t0
pre checktextureoverride = cs-t0
ib = ref ResourceA
vb2 = ref ResourceA
pre checktextureoverride = ib
pre checktextureoverride = vb2
cs-u0 = ref ResourceA
pre checktextureoverride = cs-u0
cs-u0 = null
vs-t2 = ref ResourceA
$rebind = 1
pre checktextureoverride = vs-t2
$rebind = 0
$empty = ps-t99
$slot = -1
$dynamic = vs-t[$slot]
pre checktextureoverride = vs-t[$slot]
pre checktextureoverride = vs-t[3:2]
)ini");
    auto& backend=f.backend->resources();auto a=backend.binding(L'v',ResourceCopyTargetType::SHADER_RESOURCE,2);
    auto b=backend.binding(L'v',ResourceCopyTargetType::SHADER_RESOURCE,3);
    frontend::SourceResourceIdentity identity;identity.tracking=frontend::SourceResourceIdentity::Tracking::Tracked;identity.hash_known=true;identity.hash=0x12345678;
    backend.identify_resource(a,identity);identity.hash=0x22222222;backend.identify_resource(b,identity);
    f.present();check(f.variable("alias")==17 && f.variable("copy")==0 && !std::signbit(f.variable("copy")),
        "Reference aliases share source identity while newly copied owned allocations remain untracked");
    check(f.variable("hits")==12 && f.variable("post_hits")==12,"General checks execute all stage slots, IA, UAV and wrapped pool ranges");
    check(f.variable("before")==17 && f.variable("after")==73 && backend.binding(L'v',ResourceCopyTargetType::SHADER_RESOURCE,2).buffer==b.buffer,
        "this rebinding changes the live slot and subsequent matching query");
    check(std::signbit(f.variable("empty")) && f.variable("empty")==0 && std::signbit(f.variable("dynamic")) && f.variable("dynamic")==0,
        "Missing and finite out-of-range source slots return native negative zero");
    a={};b={};f.release();
}
