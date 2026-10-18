#include "gpu_resources.h"
#include "image_formats.h"
#include "globals.h"
#include "util.h"
#include "ResourceHash.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <typeinfo>

void FrontendBufferViewElements(UINT,UINT,DXGI_FORMAT,UINT,UINT&,UINT&);

namespace efmi::frontend {
namespace {
using Target=ResourceCopyTargetType;
using Mode=ResourceCopyTargetEvaluationMode;
constexpr uint64_t max_bytes=128ull<<20;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::wstring widen(const char* text){std::string s(text);return {s.begin(),s.end()};}
bool swap_chain(Target t){return t==Target::SWAP_CHAIN || t==Target::REAL_SWAP_CHAIN || t==Target::FAKE_SWAP_CHAIN;}
uint32_t uint_value(float v){require(std::isfinite(v) && double(v)>=0 && double(v)<=UINT32_MAX,"GPU resource index outside unsigned range");return uint32_t(v);}
void validate_desc(const D3D11_BUFFER_DESC& d) {
    require(d.ByteWidth && d.ByteWidth<=max_bytes,"GPU buffer description exceeds size budget");
    require(!(d.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) || (d.StructureByteStride && d.StructureByteStride%4==0 &&
            d.StructureByteStride<=D3D11_REQ_MULTI_ELEMENT_STRUCTURE_SIZE_IN_BYTES && d.ByteWidth%d.StructureByteStride==0 &&
            !(d.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)),"GPU structured buffer size/stride invalid");
    require(!(d.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS) || d.ByteWidth%4==0,"GPU raw buffer size invalid");
    require(!(d.BindFlags&D3D11_BIND_CONSTANT_BUFFER) || (d.ByteWidth%16==0 && d.ByteWidth<=65536),"GPU constant buffer size invalid");
}
VkBufferUsageFlags usage(const D3D11_BUFFER_DESC& d) {
    VkBufferUsageFlags result=0;
    if(d.BindFlags&D3D11_BIND_VERTEX_BUFFER)result|=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if(d.BindFlags&D3D11_BIND_INDEX_BUFFER)result|=VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if(d.BindFlags&D3D11_BIND_CONSTANT_BUFFER)result|=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if(d.BindFlags&D3D11_BIND_SHADER_RESOURCE)result|=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    if(d.BindFlags&D3D11_BIND_UNORDERED_ACCESS)result|=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
    if(d.MiscFlags&D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS)result|=VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    require(!(d.BindFlags&~(D3D11_BIND_VERTEX_BUFFER|D3D11_BIND_INDEX_BUFFER|D3D11_BIND_CONSTANT_BUFFER|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS)),"GPU buffer bind flags need another adapter");
    return result;
}
uint32_t bind_flag(Target t) {
    switch(t){
    case Target::VERTEX_BUFFER:return D3D11_BIND_VERTEX_BUFFER;
    case Target::INDEX_BUFFER:return D3D11_BIND_INDEX_BUFFER;
    case Target::CONSTANT_BUFFER:return D3D11_BIND_CONSTANT_BUFFER;
    case Target::SHADER_RESOURCE:return D3D11_BIND_SHADER_RESOURCE;
    case Target::UNORDERED_ACCESS_VIEW:return D3D11_BIND_UNORDERED_ACCESS;
    case Target::RENDER_TARGET:return D3D11_BIND_RENDER_TARGET;
    case Target::DEPTH_STENCIL_TARGET:return D3D11_BIND_DEPTH_STENCIL;
    default:throw std::runtime_error("GPU target needs a non-buffer adapter");
    }
}
void fill_buffer_info(GpuResourceBinding& b,Target type=Target::CUSTOM_RESOURCE){
    // CPU mirror of the resource/view facts consumed by original FillInMissingInfo.
    b.visible_size=b.visible_size?std::min(b.visible_size,b.description.ByteWidth):b.description.ByteWidth;
    if(!b.stride)b.stride=b.description.StructureByteStride;
    if(b.view!=GpuResourceBinding::View::None && (type==Target::SHADER_RESOURCE || type==Target::UNORDERED_ACCESS_VIEW)){
        if(b.format==DXGI_FORMAT_UNKNOWN)b.format=b.view_format;
        if(!b.stride)b.stride=dxgi_format_size(b.format);
        if(!b.offset)b.offset=b.view_offset;
    }
    if(!b.stride)b.stride=dxgi_format_size(b.format);
    if(!b.stride)b.stride=b.visible_size;
}
void select_buffer_view(GpuResourceBinding& b,GpuResourceBinding::View kind,bool force_raw=false,bool complete_info=true){
    if(!b.buffer || b.view==kind)return;
    if(kind==GpuResourceBinding::View::None){b.view=kind;return;}
    require(kind==GpuResourceBinding::View::ShaderResource || kind==GpuResourceBinding::View::UnorderedAccess,"GPU buffer target view needs an additional adapter");
    if(complete_info)fill_buffer_info(b);auto format=b.format;auto stride=b.stride;
    require(!force_raw || (b.description.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS),"GPU raw view requires raw allocation flags");
    const bool raw=force_raw || ((b.description.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS) &&
        (format==DXGI_FORMAT_UNKNOWN || format==DXGI_FORMAT_R32_TYPELESS));
    if(raw){format=DXGI_FORMAT_R32_TYPELESS;stride=4;}
    require(format!=DXGI_FORMAT_R32_TYPELESS || raw,"GPU typeless buffer view requires raw view flags");
    require(!(b.description.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) || format==DXGI_FORMAT_UNKNOWN,
            "GPU structured buffer view must have unknown format");
    const auto width=format!=DXGI_FORMAT_UNKNOWN && format!=DXGI_FORMAT(-1)?dxgi_format_size(format):b.description.StructureByteStride;
    require(width>0,"GPU buffer view requires a typed, raw or structured element");
    require(b.offset<=b.visible_size,"GPU buffer view ends before its offset");
    UINT first=0,count=0;FrontendBufferViewElements(stride,b.offset,format,b.visible_size,first,count);
    const uint64_t offset=uint64_t(first)*width,size=uint64_t(count)*width;
    require(size && offset<b.buffer->size() && size<=b.buffer->size()-offset,"GPU buffer view has an empty or invalid element range");
    b.view=kind;b.view_format=format;b.view_offset=UINT(offset);b.view_size=UINT(size);
}
GpuResourceBinding::View view_kind(Target t){return t==Target::SHADER_RESOURCE?GpuResourceBinding::View::ShaderResource:
    t==Target::UNORDERED_ACCESS_VIEW?GpuResourceBinding::View::UnorderedAccess:
    t==Target::RENDER_TARGET?GpuResourceBinding::View::RenderTarget:
    t==Target::DEPTH_STENCIL_TARGET?GpuResourceBinding::View::DepthStencil:GpuResourceBinding::View::None;}
}
void GpuResourceRuntime::fill_info(GpuResourceBinding& b,Target type){
    if(!b.image){fill_buffer_info(b,type);return;}
    if(b.image_view){
        if(b.format==DXGI_FORMAT_UNKNOWN && (type==Target::SHADER_RESOURCE || type==Target::UNORDERED_ACCESS_VIEW ||
           type==Target::RENDER_TARGET || type==Target::DEPTH_STENCIL_TARGET))b.format=b.view_format;
    }else if(b.format==DXGI_FORMAT_UNKNOWN)b.format=b.image_description.texture.Format;
    if(!b.stride)b.stride=dxgi_format_size(b.format);
    if(!b.stride)b.stride=b.visible_size;
}
void GpuResourceRuntime::select_view(GpuResourceBinding& b,GpuResourceBinding::View kind,bool raw,bool complete_info){
    if(b.image){image_view(b,kind,complete_info);return;}
    select_buffer_view(b,kind,raw,complete_info);
}
GpuResourceRuntime::GpuResourceRuntime(gpu::ResourceDevice d,gpu::ResourceBudgets budgets):commands_(d,std::move(budgets)){}
void GpuResourceRuntime::backbuffer(gpu::Image image,DXGI_FORMAT format,UINT bind_flags){
    if(!image){backbuffer_={};return;}
    const auto& vk=image->description();
    require(vk.type==VK_IMAGE_TYPE_2D && vk.mips==1 && vk.layers==1 && vk.samples==VK_SAMPLE_COUNT_1_BIT && vk.extent.depth==1,
            "Host back buffer must be one single-sample 2D image");
    require(source_image_format(format).format==vk.format,"Host back buffer format does not match its image");
    require((bind_flags&D3D11_BIND_RENDER_TARGET) && !(bind_flags&~(D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS)),
            "Host back buffer bind flags are invalid");
    GpuResourceBinding b;b.image=std::move(image);auto& s=b.image_description;s.dimension=D3D11_RESOURCE_DIMENSION_TEXTURE2D;s.depth=1;
    auto& d=s.texture;d.Width=vk.extent.width;d.Height=vk.extent.height;d.MipLevels=1;d.ArraySize=1;d.Format=format;
    d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=bind_flags;
    backbuffer_=std::move(b);
}
std::vector<std::wstring> GpuResourceRuntime::install(DrawPlanAdapter& adapter) {
    require(!installed_,"GPU resource runtime cannot belong to two INI generations");installed_=true;
    adapter.resource_commands([this](auto& command,auto& state,auto& why){return run(command,state,why);});
    adapter.resource_queries([this](auto& target,auto& state,float& value,auto& why){return query(target,state,value,why);});
    adapter.resource_matches([this](auto& target,auto& state,auto& value,auto& why){return match_source(target,state,value,why);});
    adapter.parameter_flush([this](auto& state,bool force,auto& why){return flush_parameters(state,force,why);});
    return {};
}
void GpuResourceRuntime::draw_commands(DrawExecutor executor){
    require(!installed_ && bool(executor),"Install GPU draw executor before generation adaptation");draw_executor_=std::move(executor);
}
void GpuResourceRuntime::begin(VkQueue queue,ExecutionSite site) {
    require(!failed_,"GPU resource generation failed; prepare a new generation");
    require(!active_ && queue && site==ExecutionSite::OutsideRenderPass,"GPU resource execution requires an explicit outside-render-pass scope");
    require(!queue_ || queue_==queue,"GPU resource scope cannot change queues");queue_=queue;active_=true;
}
std::shared_ptr<gpu::ResourceSubmission> GpuResourceRuntime::finish() {
    active();require(!failed_,"Cannot submit a failed GPU resource scope");auto result=commands_.submit(queue_);active_=false;return result;
}
void GpuResourceRuntime::discard(){commands_.discard();active_=false;failed_=true;}
void GpuResourceRuntime::active() const {require(active_,"GPU resource operation has no outside-render-pass execution scope");}
ResourceCopyTarget& GpuResourceRuntime::effective(ResourceCopyTarget& t,CommandListState& state) {
    if(t.type!=Target::THIS_RESOURCE)return t;
    require(state.this_target && state.this_target->type!=Target::THIS_RESOURCE,"GPU this target missing or cyclic");return *state.this_target;
}
GpuResourceRuntime::Slot GpuResourceRuntime::slot(ResourceCopyTarget& t,CommandListState& state) {
    unsigned limit=0;bool stage=false;
    switch(t.type){
    case Target::VERTEX_BUFFER:limit=32;break;
    case Target::INDEX_BUFFER:limit=1;break;
    case Target::CONSTANT_BUFFER:limit=14;stage=true;break;
    case Target::SHADER_RESOURCE:limit=128;stage=true;break;
    case Target::UNORDERED_ACCESS_VIEW:limit=64;stage=true;require(t.shader_type==L'p' || t.shader_type==L'c',"GPU UAV stage invalid");break;
    case Target::RENDER_TARGET:limit=8;break;
    case Target::DEPTH_STENCIL_TARGET:limit=1;break;
    default:throw std::runtime_error("GPU target has no buffer slot adapter");
    }
    require(!stage || (t.shader_type && std::wstring(L"vhdgpc").find(t.shader_type)!=std::wstring::npos),"GPU shader stage invalid");
    const auto index=t.ResolveSlot(&state);require(index<limit,"GPU binding slot out of range");return {stage?t.shader_type:L'\0',t.type,index};
}
CustomResource* GpuResourceRuntime::declaration(ResourceCopyTarget& t,CommandListState& state,bool writing) {
    if(t.custom_resource_pool)validate_pool_runtime(*t.custom_resource_pool);
    auto* r=t.GetCustomResource(&state,writing);
    if(r && r->pool)validate_pool_runtime(*r->pool);
    return r;
}
GpuResourceBinding GpuResourceRuntime::load(CustomResource& r) {
    const auto found=resources_.find(&r);
    if(found!=resources_.end()){
        if(r.is_null)return {};
        auto value=found->second;value.stride=r.stride;value.offset=r.offset;value.format=r.format;
        value.visible_size=r.buf_size;value.source_stride=r.source_stride;return value;
    }
    require(resources_.size()<65536,"GPU resource declaration budget exceeded");
    if(r.filename.empty() && r.override_type==CustomResourceType::INVALID)
        return {};
    if(r.override_type==CustomResourceType::TEXTURE1D || r.override_type==CustomResourceType::TEXTURE2D ||
       r.override_type==CustomResourceType::TEXTURE3D || r.override_type==CustomResourceType::CUBE ||
       (!r.filename.empty() && r.override_type==CustomResourceType::INVALID))return load_image(r);
    require(r.override_type==CustomResourceType::BUFFER || r.override_type==CustomResourceType::STRUCTURED_BUFFER ||
            r.override_type==CustomResourceType::RAW_BUFFER || r.override_type==CustomResourceType::INVALID,"GPU image declaration requires image commands");
    GpuResourceBinding b;std::vector<uint8_t> bytes;
    if(!r.filename.empty()) {
        require(r.override_type!=CustomResourceType::INVALID,"GPU untyped file requires image/resource type adaptation");
        std::ifstream in(std::filesystem::path(r.filename),std::ios::binary|std::ios::ate);
        require(bool(in) && in.tellg()>0 && uint64_t(in.tellg())<=max_bytes,"GPU resource file unreadable or over budget");
        bytes.resize(size_t(in.tellg()));in.seekg(0);in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());require(bool(in),"GPU resource file read failed");
        b.visible_size=uint32_t(bytes.size()); // Original LoadBufferFromFile keeps file size separately.
    }else if(r.initial_data_size){require(r.initial_data && r.initial_data_size<=max_bytes,"GPU initial data exceeds budget");
        const auto* p=static_cast<const uint8_t*>(r.initial_data);bytes.assign(p,p+r.initial_data_size);}
    b.description.ByteWidth=UINT(bytes.size());b.description.BindFlags=r.bind_flags;b.description.MiscFlags=r.misc_flags;
    if(r.override_type==CustomResourceType::STRUCTURED_BUFFER)b.description.StructureByteStride=UINT(bytes.size());
    // Original description rules, including format-derived stride and array.
    r.OverrideBufferDesc(&b.description);validate_desc(b.description);
    r.OverrideOutOfBandInfo(&b.format,&b.stride);b.source_stride=r.source_stride;
    b.buffer=commands_.create(b.description.ByteWidth,usage(b.description),bytes.empty()?nullptr:bytes.data(),std::min<size_t>(bytes.size(),b.description.ByteWidth),true);
    r.is_null=false;r.stride=b.stride;r.format=b.format;r.buf_size=b.visible_size;resources_[&r]=b;return b;
}
GpuResourceBinding GpuResourceRuntime::resolve(ResourceCopyTarget& input,CommandListState& state){active();return get(input,state);}
namespace {
std::shared_ptr<void> resource_owner(const GpuResourceBinding& binding){
    return binding.image?std::static_pointer_cast<void>(binding.image):std::static_pointer_cast<void>(binding.buffer);
}
}
void GpuResourceRuntime::identify_resource(const GpuResourceBinding& binding,SourceResourceIdentity identity){
    require(bool(binding) && !failed_,"Resource identity requires a live owned allocation");
    require(identity.hash_known || !identity.hash,"Unknown original resource hash has an asserted value");
    require(identity.tracking==SourceResourceIdentity::Tracking::Tracked || (!identity.hash_known && !identity.region_hash),
            "Untracked resource has asserted source hash facts");
    const std::weak_ptr<void> key=resource_owner(binding);
    if(identity.tracking==SourceResourceIdentity::Tracking::Untracked){source_identities_.erase(key);return;}
    if(!source_identities_.count(key) && source_identities_.size()>=65536){
        for(auto it=source_identities_.begin();it!=source_identities_.end();)if(it->first.expired())it=source_identities_.erase(it);else ++it;
        require(source_identities_.size()<65536,"Original resource identity budget exceeded");
    }
    source_identities_[key]=std::move(identity);
}
bool GpuResourceRuntime::match_source(ResourceCopyTarget& target,CommandListState& state,SourceResourceMatch& result,std::wstring& reason){
    try{active();require(!failed_,"GPU resource generation failed");
        result={};ResourceCopyTarget single;ResourceCopyTarget* resolved=&target;
        if(target.slot_expression){const auto slot=target.ResolveSlot(&state);if(slot==UINT_MAX)return true;
            single.type=target.type;single.shader_type=target.shader_type;single.slot=slot;single.max_slot=target.max_slot;resolved=&single;}
        auto binding=get(*resolved,state);result.bound=bool(binding);if(!binding)return true;
        const auto found=source_identities_.find(std::weak_ptr<void>(resource_owner(binding)));
        if(found==source_identities_.end())result.identity.tracking=SourceResourceIdentity::Tracking::Untracked;
        else result.identity=found->second;
        result.stride=binding.stride;result.offset=binding.offset;result.format=binding.format;
        if(!binding.image)result.description=binding.description;
        else{const auto& s=binding.image_description;const auto& d=s.texture;
            if(s.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE1D)
                result.description=D3D11_TEXTURE1D_DESC{d.Width,d.MipLevels,d.ArraySize,d.Format,d.Usage,d.BindFlags,d.CPUAccessFlags,d.MiscFlags};
            else if(s.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE2D)result.description=d;
            else if(s.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE3D)
                result.description=D3D11_TEXTURE3D_DESC{d.Width,d.Height,s.depth,d.MipLevels,d.Format,d.Usage,d.BindFlags,d.CPUAccessFlags,d.MiscFlags};
            else throw std::runtime_error("Original image dimension is unknown");
        }
        return true;
    }catch(const std::exception& e){if(active_)failed_=true;reason=widen(e.what());return false;}
}
GpuResourceBinding GpuResourceRuntime::get(ResourceCopyTarget& input,CommandListState& state) {
    auto& t=effective(input,state);GpuResourceBinding b;
    require(!t.IsRange(),"GPU range command requires the slot-range adapter");
    if(t.type==Target::EMPTY)return {};
    if(t.type==Target::INI_PARAMS){
        require(parameters_initialized_,"GPU parameters used before initialization");b=parameters_;
        // The original special target returns its image/view without supplying
        // any out-of-band format or buffer metadata. Reading never flushes it.
        b.format=DXGI_FORMAT_UNKNOWN;b.stride=b.source_stride=b.offset=b.visible_size=0;
    }else if(t.type==Target::CUSTOM_RESOURCE){if(auto* r=declaration(t,state))b=load(*r);}
    else if(swap_chain(t.type)){
        // GetBuffer(0) returns the resource alone: no view and no out-of-band format or size.
        b=backbuffer_;b.drop_view();b.format=DXGI_FORMAT_UNKNOWN;b.stride=b.source_stride=b.offset=b.visible_size=0;
    }
    else {const auto key=slot(t,state);const auto found=slots_.find(key);if(found!=slots_.end())b=found->second;
        // XXGetShaderResources/UnorderedAccessViews return resource and view,
        // not the custom declaration's out-of-band stride/offset/size/format.
        b.source_stride=0;
        if(t.type==Target::SHADER_RESOURCE || t.type==Target::UNORDERED_ACCESS_VIEW || t.type==Target::RENDER_TARGET ||
           t.type==Target::DEPTH_STENCIL_TARGET){b.stride=0;b.offset=0;b.visible_size=0;b.format=DXGI_FORMAT_UNKNOWN;b.region=false;}
        else if(t.type==Target::CONSTANT_BUFFER){b.stride=0;b.format=DXGI_FORMAT_UNKNOWN;}
        else if(t.type==Target::INDEX_BUFFER){b.stride=dxgi_format_size(b.format);b.visible_size=0;}
        else if(t.type==Target::VERTEX_BUFFER){b.format=DXGI_FORMAT_UNKNOWN;b.visible_size=0;}}
    if(b && input.evaluation_mode==Mode::RESOURCE_REGION) {
        require(input.member_args[0].expression && input.member_args[1].expression,"GPU region requires offset and size");
        b.offset=uint_value(input.member_args[0].GetValue(&state));b.visible_size=uint_value(input.member_args[1].GetValue(&state));
        b.region=true;
        require(!b.buffer || (b.offset<b.buffer->size() && b.visible_size<=b.buffer->size()),"GPU resource region outside allocation");
    }
    return b;
}
void GpuResourceRuntime::set(ResourceCopyTarget& input,CommandListState& state,const GpuResourceBinding& b) {
    auto& t=effective(input,state);
    require(t.evaluation_mode==Mode::RESOURCE && !t.IsRange(),"GPU assignment needs a single resource target");
    if(t.type==Target::CUSTOM_RESOURCE){auto* r=declaration(t,state,true);require(r!=nullptr,"GPU resource destination missing");
        require(resources_.size()<65536 || resources_.count(r),"GPU resource declaration budget exceeded");
        auto& cached=resources_[r];
        // Original SetResource(null) retains the old resource/view as a cache.
        // source_stride is changed by copy instructions, not SetResource itself.
        if(b)cached=b;
        cached.stride=b.stride;cached.offset=b.offset;cached.visible_size=b.visible_size;cached.format=b.format;cached.source_stride=r->source_stride;
        r->is_null=!b;r->stride=b.stride;r->offset=b.offset;r->buf_size=b.visible_size;r->format=b.format;
    }else {
        auto key=slot(t,state);
        require(!b || (b.bind_flags()&bind_flag(t.type)),"Referenced GPU resource lacks destination bind flags");
        auto bound=b;
        select_view(bound,view_kind(t.type));
        if(t.type==Target::CONSTANT_BUFFER && bound.region)
            require(bound.offset%16==0 && bound.visible_size%16==0 && bound.visible_size<=bound.description.ByteWidth-bound.offset,
                    "GPU constant-buffer reference region is not aligned or outside allocation");
        assign_binding(key,std::move(bound));
    }
}
void GpuResourceRuntime::copy(ResourceCopyOperation& op,CommandListState& state) {
    require(typeid(op)==typeid(ResourceCopyOperation),"GPU staging command requires staging adaptation");
    require(op.src.evaluation_mode==Mode::RESOURCE || op.src.evaluation_mode==Mode::RESOURCE_REGION,"GPU copy source is not a resource");
    constexpr auto allowed=uint32_t(ResourceCopyOptions::COPY)|uint32_t(ResourceCopyOptions::COPY_DESC)|uint32_t(ResourceCopyOptions::REFERENCE)|
        uint32_t(ResourceCopyOptions::UNLESS_NULL)|uint32_t(ResourceCopyOptions::NO_VIEW_CACHE)|uint32_t(ResourceCopyOptions::RAW_VIEW)|
        uint32_t(ResourceCopyOptions::MONO)|uint32_t(ResourceCopyOptions::SET_VIEWPORT)|uint32_t(ResourceCopyOptions::RESOLVE_MSAA);
    require(!(uint32_t(op.options)&~allowed),"GPU copy option requires an additional adapter");
    auto& dst=effective(op.dst,state);
    if(op.src.type==Target::EMPTY){set(dst,state,{});return;} // Explicit null ignores unless_null upstream.
    auto source=get(op.src,state);
    if(dst.type!=Target::POOL){copy_to(op,dst,state,std::move(source));return;}
    require(dst.custom_resource_pool && (dst.evaluation_mode==Mode::RESOURCE || dst.evaluation_mode==Mode::POOL_FULL_RANGE_RESOURCE),
            "GPU whole-pool assignment needs a resource pool target");
    auto* pool=dst.custom_resource_pool;validate_pool_runtime(*pool);
    const auto count=pool->GetPoolSize();require(count<=65536,"GPU whole-pool assignment budget exceeded");
    // Retain the resolved source resource/view once, before changing any pool
    // element. A local target avoids mutating the original command on failure.
    ResourceCopyTarget target;target.type=Target::CUSTOM_RESOURCE;
    for(size_t i=0;i<count;++i){
        auto* destination=pool->GetResource(float(i),false,true,true);
        require(destination!=nullptr,"GPU whole-pool destination missing");target.SetCustomResource(destination);
        copy_to(op,target,state,source);
    }
}
void GpuResourceRuntime::copy_to(ResourceCopyOperation& op,ResourceCopyTarget& dst,CommandListState& state,GpuResourceBinding source) {
    if(!source){if(!(op.options&ResourceCopyOptions::UNLESS_NULL))set(dst,state,{});return;}
    auto* custom=dst.type==Target::CUSTOM_RESOURCE?declaration(dst,state,true):nullptr;
    const bool explicit_region=op.src.evaluation_mode==Mode::RESOURCE_REGION;
    const auto region_size=source.visible_size;
    if(custom && custom->max_copies_per_frame) {
        if(custom->frame_no!=G->frame_no){custom->frame_no=G->frame_no;custom->copies_this_frame=1;}
        else if(custom->copies_this_frame++>=custom->max_copies_per_frame)return;
    }
    if(custom)custom->OverrideOutOfBandInfo(&source.format,&source.stride);
    source.source_stride=op.src.type==Target::CUSTOM_RESOURCE && source.source_stride?source.source_stride:source.stride;
    if(custom){
        // Original CopyResourceToResource reads source_stride from the current
        // declaration per destination; the retained GPU source itself is fixed.
        if(op.src.type==Target::CUSTOM_RESOURCE){auto* origin=declaration(op.src,state);
            source.source_stride=origin && origin->source_stride?origin->source_stride:source.stride;}
        custom->source_stride=source.source_stride;
    }
    fill_info(source,op.src.type);
    require(!custom || resources_.size()<65536 || resources_.count(custom),"GPU resource declaration budget exceeded");
    auto& cache=custom?resources_[custom]:copies_[&op];
    const bool no_view_cache=bool(op.options&ResourceCopyOptions::NO_VIEW_CACHE) || op.src.forbid_view_cache;
    const bool raw_view=bool(op.options&ResourceCopyOptions::RAW_VIEW);
    auto reuse_view=[&](GpuResourceBinding& value){
        if(cache.buffer==value.buffer && cache.image==value.image){value.view=cache.view;value.view_format=cache.view_format;
            value.view_offset=cache.view_offset;value.view_size=cache.view_size;value.image_view=cache.image_view;}
    };
    if(!(op.options&ResourceCopyOptions::COPY_MASK)){
        // UsableRefView retains a source view on a custom resource. If the
        // source has none, the original may reuse its old compatible view.
        if(custom && source.view==GpuResourceBinding::View::None)reuse_view(source);
        if(!custom && source.view!=view_kind(dst.type)){
            source.drop_view();reuse_view(source);
            select_view(source,view_kind(dst.type),raw_view,false);cache=source;
        }
        source.visible_size=explicit_region?region_size:0;source.region=explicit_region;
        // The original clears a constant-slot offset unless ->Region was used.
        if(dst.type==Target::CONSTANT_BUFFER && op.src.evaluation_mode!=Mode::RESOURCE_REGION){source.offset=0;source.visible_size=0;source.region=false;}
        set(dst,state,source);
        if(source.image && (op.options&ResourceCopyOptions::SET_VIEWPORT))viewport_={0,0,float(source.image_description.texture.Width),
            source.image_description.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE1D?0.0f:float(source.image_description.texture.Height),0,1};
        if(no_view_cache)cache.drop_view();return;
    }
    if(source.image){image_copy(op,dst,state,std::move(source),custom,cache);return;}
    GpuResourceBinding out=source;out.description.BindFlags=custom?custom->bind_flags:bind_flag(dst.type);
    if(custom)out.description.MiscFlags|=custom->misc_flags;
    if(!(out.description.BindFlags&(D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS)))out.description.MiscFlags&=~D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    UINT source_end=source.visible_size,destination_size=0;
    const bool source_view=source.view!=GpuResourceBinding::View::None;
    // RecreateCompatibleBuffer: visible CB windows, CB size constraints,
    // view-less structure conversion, then the original declaration overrides.
    if((out.description.BindFlags&D3D11_BIND_CONSTANT_BUFFER) || op.src.type==Target::CONSTANT_BUFFER){
        if(op.src.type==Target::CONSTANT_BUFFER && source_end){destination_size=source_end;
            source_end=out.description.ByteWidth;out.description.ByteWidth=destination_size;}
        if(out.description.BindFlags&D3D11_BIND_CONSTANT_BUFFER){
            const auto rounded=std::min((out.description.ByteWidth+15)&~15u,65536u);
            out.description.MiscFlags&=~D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;out.description.StructureByteStride=0;
            if(source.offset || rounded!=out.description.ByteWidth){destination_size=rounded;out.description.ByteWidth=rounded;}
        }
    }else if(!source_view && source.format==DXGI_FORMAT_UNKNOWN && source.stride &&
             (out.description.BindFlags&(D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS))){
        out.description.MiscFlags|=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;out.description.StructureByteStride=source.stride;
        const auto rounded=out.description.ByteWidth/source.stride*source.stride;
        if(source.offset || rounded!=out.description.ByteWidth){destination_size=rounded;out.description.ByteWidth=rounded;}
    }else if(!source_view && source.offset){
        destination_size=out.description.ByteWidth;
    }
    if(custom){custom->OverrideBufferDesc(&out.description);custom->OverrideOutOfBandInfo(&out.format,&out.stride);}
    out.view=GpuResourceBinding::View::None;
    validate_desc(out.description);
    auto& pool=custom?resource_copy_pools_[custom]:command_copy_pools_[&op];
    const std::array<UINT,4> key={out.description.ByteWidth,out.description.BindFlags,out.description.MiscFlags,out.description.StructureByteStride};
    auto found=pool.find(key);
    if(found!=pool.end())out.buffer=found->second;
    else{out.buffer=commands_.create(out.description.ByteWidth,usage(out.description));pool.emplace(key,out.buffer);}
    reuse_view(out);
    // The original resolve helper returns for buffers. It still creates/reuses
    // the destination, but must not replace existing bytes with a normal copy.
    // MONO has no implementation upstream; buffer SET_VIEWPORT returns as well.
    if(!(op.options&ResourceCopyOptions::COPY_DESC) && !(op.options&ResourceCopyOptions::RESOLVE_MSAA)){
        if(destination_size){
            // SpecialCopyBufferRegion treats source size as an end boundary.
            const uint64_t end=std::min<uint64_t>(source_end,uint64_t(source.offset)+destination_size);
            require(end>=source.offset,"GPU buffer copy region ends before its offset");
            auto bytes=end-source.offset;if(source.stride)bytes=bytes/source.stride*source.stride;
            require(bytes<=out.description.ByteWidth,"GPU copy override truncates the original region");
            if(bytes)commands_.copy(source.buffer,source.offset,out.buffer,0,bytes);
            out.offset=0;
        }else{
            require(out.description.ByteWidth==source.description.ByteWidth,"GPU whole copy has incompatible allocation sizes");
            commands_.copy(source.buffer,0,out.buffer,0,source.description.ByteWidth);
        }
    }
    out.visible_size=source_end;
    if(!custom)select_view(out,view_kind(dst.type),raw_view,false);
    out.visible_size=destination_size;out.region=dst.type==Target::CONSTANT_BUFFER && (out.offset || out.visible_size);
    cache=out;set(dst,state,out);if(no_view_cache)cache.drop_view();
}
void GpuResourceRuntime::clear(ClearViewCommand& op,CommandListState& state) {
    auto b=get(op.target,state);if(!b)return;
    if(b.image){image_clear(std::move(b),op);return;}
    // ClearViewCommand queries the existing view first. An SRV has no clear
    // operation, even when its underlying buffer also has the UAV bind flag.
    if(b.view==GpuResourceBinding::View::ShaderResource)return;
    select_view(b,GpuResourceBinding::View::UnorderedAccess);
    b.format=b.view_format;b.offset=b.view_offset;b.visible_size=b.view_size;
    require(!op.clear_depth && !op.clear_stencil,"GPU buffer clear cannot use depth/stencil flags");
    require(b.description.BindFlags&D3D11_BIND_UNORDERED_ACCESS,"GPU buffer clear needs a UAV-compatible resource");
    // Raw/structured UAV clears use only the first uint, including when the
    // INI token looked floating point. Typed 32-bit views use all components.
    unsigned channels=1;bool floating=false;
    if(!(b.description.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) && b.format!=DXGI_FORMAT_R32_TYPELESS){
        switch(b.format){
        case DXGI_FORMAT_R32_FLOAT:channels=1;floating=true;break;
        case DXGI_FORMAT_R32G32_FLOAT:channels=2;floating=true;break;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:channels=4;floating=true;break;
        case DXGI_FORMAT_R32_UINT:case DXGI_FORMAT_R32_SINT:channels=1;break;
        case DXGI_FORMAT_R32G32_UINT:case DXGI_FORMAT_R32G32_SINT:channels=2;break;
        case DXGI_FORMAT_R32G32B32A32_UINT:case DXGI_FORMAT_R32G32B32A32_SINT:channels=4;break;
        default:throw std::runtime_error("GPU typed clear format needs a conversion adapter");
        }
    }
    const auto count=b.visible_size?b.visible_size:b.description.ByteWidth-b.offset;
    require(b.offset%(channels*4)==0 && count%(channels*4)==0,"GPU clear view range alignment invalid");
    uint32_t words[4]{};
    for(unsigned i=0;i<channels;++i){if(floating && !op.clear_uav_uint)memcpy(&words[i],&op.fval[i],4);else words[i]=op.uval[i];}
    bool repeated=true;for(unsigned i=1;i<channels;++i)repeated&=words[i]==words[0];
    if(repeated)commands_.fill(b.buffer,b.offset,count,words[0]);
    else {std::vector<uint32_t> data(count/4);for(size_t i=0;i<data.size();++i)data[i]=words[i%channels];commands_.write(b.buffer,b.offset,data.data(),count);}
}
void GpuResourceRuntime::ranges(SlotRangeCopyOperation& op,CommandListState& state) {
    const bool bind=op.dst.evaluation_mode==Mode::SLOT_RANGE;
    auto& slots=bind?op.dst:op.src;auto& other=bind?op.src:op.dst;
    require(slots.evaluation_mode==Mode::SLOT_RANGE && slots.range_start && slots.range_end,"GPU slot range bounds missing");
    int first=0,pool_first=0;unsigned count=0,pool_count=0;
    require(slots.ResolveRange(&state,&first,&count) && count && count<=128,"GPU slot range is invalid");
    auto* pool=other.type==Target::POOL?other.custom_resource_pool:nullptr;
    if(pool){
        validate_pool_runtime(*pool);
        require(other.range_start && other.range_end && other.ResolveRange(&state,&pool_first,&pool_count) && pool_count==count,
                "GPU slot and pool ranges differ");
    }else require(bind && (other.type==Target::CUSTOM_RESOURCE || other.type==Target::EMPTY),"GPU range requires a pool or broadcast resource");
    const int plain_options=int(ResourceCopyOptions::REFERENCE)|int(ResourceCopyOptions::UNLESS_NULL)|int(ResourceCopyOptions::NO_VIEW_CACHE);
    const bool per_slot=(int(op.options)&~plain_options) && op.src.type!=Target::EMPTY;
    require(range_operations_.size()<4096 || range_operations_.count(&op),"GPU range command budget exceeded");
    auto& operations=range_operations_[&op];auto& views=range_views_[&op];
    if(operations.size()<count)operations.resize(count);if(views.size()<count)views.resize(count);
    auto operation=[&](unsigned i)->ResourceCopyOperation&{
        if(!operations[i])operations[i]=std::make_unique<ResourceCopyOperation>();
        auto& one=*operations[i];one.ini_line=op.ini_line;one.options=op.options;
        auto& target=bind?one.dst:one.src;target.type=slots.type;target.shader_type=slots.shader_type;
        target.slot=unsigned(first)+i;target.max_slot=slots.max_slot;
        auto& resource=bind?one.src:one.dst;resource.type=Target::CUSTOM_RESOURCE;return one;
    };
    // Fetch the entire source range before writes to a pool. This also retains
    // owners independently of aliases and element switches during assignment.
    std::vector<GpuResourceBinding> fetched;
    if(!bind)for(unsigned i=0;i<count;++i){auto& one=operation(i);fetched.push_back(get(one.src,state));}
    Bindings pending;
    if(bind)for(unsigned i=0;i<count;++i){auto& one=operation(i);auto key=slot(one.dst,state);
        pending.emplace(key,binding(std::get<0>(key),std::get<1>(key),std::get<2>(key)));}
    require(!deferred_bindings_,"Nested GPU binding publication is invalid");
    struct DeferredScope {Bindings*& target;~DeferredScope(){target=nullptr;}} deferred{deferred_bindings_};
    if(bind)deferred_bindings_=&pending;
    CustomResource* broadcast=bind && other.type==Target::CUSTOM_RESOURCE?declaration(other,state):nullptr;
    for(unsigned i=0;i<count;++i){auto& one=operation(i);
        if(bind){
            auto* source=pool?pool->GetResource(float(int64_t(pool_first)+i),false,true,false):broadcast;
            one.src.SetCustomResource(source);
            if(per_slot){copy(one,state);continue;}
            auto value=source?load(*source):GpuResourceBinding{};
            // Unlike scalar `null`, range null with unless_null preserves slots.
            if(!value && (op.options&ResourceCopyOptions::UNLESS_NULL))continue;
            if(value){
                require(value.bind_flags()&bind_flag(slots.type),"Range reference lacks destination bind flags");
                const auto kind=view_kind(slots.type);
                if(kind==GpuResourceBinding::View::None)value.drop_view();
                else if(value.view!=kind){
                    if(views[i].buffer==value.buffer && views[i].image==value.image && views[i].view==kind){value.view=kind;value.view_format=views[i].view_format;
                        value.view_offset=views[i].view_offset;value.view_size=views[i].view_size;value.image_view=views[i].image_view;}
                    else if(value.image){value.format=DXGI_FORMAT_UNKNOWN;select_view(value,kind,false,false);views[i]=value;}
                    else {
                        // Original plain range ViewForSlot calls its view helper
                        // with zero stride/offset/size and UNKNOWN format. That
                        // creates one structured element, not an invented full
                        // view. Typed/raw fresh-view failures remain explicit.
                        require(value.description.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                                "Original plain buffer range needs an existing compatible view or a structured resource");
                        UINT start=0,n=0;FrontendBufferViewElements(0,0,DXGI_FORMAT_UNKNOWN,0,start,n);
                        value.view=kind;value.view_format=DXGI_FORMAT_UNKNOWN;
                        value.view_offset=start*value.description.StructureByteStride;value.view_size=n*value.description.StructureByteStride;
                        views[i]=value;
                    }
                }
                // Plain CB ranges bind the complete buffer; no resource-region
                // metadata is forwarded by the original fast path.
                value.stride=0;value.offset=0;value.visible_size=0;value.format=DXGI_FORMAT_UNKNOWN;value.region=false;
            }
            pending[slot(one.dst,state)]=std::move(value);
        }else{
            const auto& source=fetched[i];
            if(!source && (op.options&ResourceCopyOptions::UNLESS_NULL))continue;
            auto* destination=pool->GetResource(float(int64_t(pool_first)+i),false,true,true);
            require(destination!=nullptr,"GPU range pool destination missing");one.dst.SetCustomResource(destination);
            if(per_slot){copy(one,state);continue;}
            // Plain range fetch is SetResource directly: no per-frame copy
            // counter and no destination stride/format override is applied.
            auto value=source;value.stride=0;value.format=DXGI_FORMAT_UNKNOWN;value.source_stride=0;
            set(one.dst,state,value);
        }
    }
    deferred_bindings_=nullptr;
    if(bind)publish_bindings(std::move(pending));
    if(op.options&ResourceCopyOptions::NO_VIEW_CACHE)for(auto& cached:views)cached={};
}
void GpuResourceRuntime::store(StoreCommand& op,CommandListState& state) {
    auto source=get(op.src,state);if(!source)return; // Original null keeps the previous variable value.
    require(!source.image,"Original store requires a buffer source");
    require(op.offset_expression!=nullptr,"GPU store offset missing");
    const uint64_t offset=uint64_t(uint_value(op.offset_expression->evaluate(&state)))*sizeof(float);
    UINT64 from=0,size=0;UINT value_offset=0;float value=0;
    // The original treats the offset as a 4-byte element index even for
    // structured buffers, and returns zero for an out-of-range read.
    if(op.CalculateCopyRegion(source.description,sizeof(float),offset,&from,&size,&value_offset) &&
       from<=source.description.ByteWidth && size<=source.description.ByteWidth-from){
        auto bytes=commands_.readback(queue_,source.buffer,from,size);memcpy(&value,bytes.data()+value_offset,sizeof(value));
    }
    op.SetOutputValue(&state,value);
}
bool GpuResourceRuntime::run(CommandListCommand& command,CommandListState& state,std::wstring& reason) {
    try{active();require(!failed_,"GPU resource generation failed");
        if(auto* c=dynamic_cast<ResourceCopyOperation*>(&command))copy(*c,state);
        else if(auto* c=dynamic_cast<SlotRangeCopyOperation*>(&command))ranges(*c,state);
        else if(auto* c=dynamic_cast<ClearViewCommand*>(&command))clear(*c,state);
        else if(auto* c=dynamic_cast<StoreCommand*>(&command))store(*c,state);
        else if(auto* c=dynamic_cast<DrawCommand*>(&command);c && draw_executor_){
            if(!draw_executor_(*c,state,reason)){failed_=true;return false;}
        }
        else throw std::runtime_error("GPU draw/dispatch needs a shader execution adapter");
        return true;
    }catch(const std::exception& e){if(active_)failed_=true;reason=widen(e.what())+L": "+command.ini_line;return false;}
}
bool GpuResourceRuntime::query(ResourceCopyTarget& input,CommandListState& state,float& result,std::wstring& reason) {
    try{require(!failed_,"GPU resource generation failed");const auto mode=input.evaluation_mode;
        require(mode==Mode::RESOURCE_STRIDE || mode==Mode::RESOURCE_SIZE || mode==Mode::RESOURCE_SOURCE_STRIDE ||
                mode==Mode::RESOURCE_FORMAT || mode==Mode::RESOURCE_WIDTH || mode==Mode::RESOURCE_HEIGHT ||
                mode==Mode::RESOURCE_ARRAY || mode==Mode::RESOURCE_MIPS || mode==Mode::RESOURCE_BIND_FLAGS ||
                mode==Mode::RESOURCE_OFFSET || mode==Mode::RESOURCE_IDENTITY,"GPU resource property requires source-semantic adaptation");
        auto* custom=input.type==Target::CUSTOM_RESOURCE?declaration(input,state):nullptr;
        // These original getters return declaration metadata before attempting
        // resource creation. They also work in Constants without a GPU scope.
        if(mode==Mode::RESOURCE_SOURCE_STRIDE){result=custom?float(custom->source_stride):-1.0f;return true;}
        if(mode==Mode::RESOURCE_STRIDE && input.type==Target::CUSTOM_RESOURCE){result=input.GetResourceStride(&state);return true;}
        if(mode==Mode::RESOURCE_STRIDE && input.type==Target::CONSTANT_BUFFER){result=16;return true;}
        if(custom){
            if(mode==Mode::RESOURCE_BIND_FLAGS){result=float(custom->bind_flags);return true;}
            if(mode==Mode::RESOURCE_SIZE && custom->buf_size){result=float(custom->buf_size);return true;}
            if(mode==Mode::RESOURCE_FORMAT){
                if(custom->override_format!=DXGI_FORMAT(-1) && custom->override_format!=DXGI_FORMAT_UNKNOWN){result=float(custom->override_format);return true;}
                if(custom->format!=DXGI_FORMAT_UNKNOWN){result=float(custom->format);return true;}
            }
            const int declared=mode==Mode::RESOURCE_WIDTH?custom->override_width:mode==Mode::RESOURCE_HEIGHT?custom->override_height:
                mode==Mode::RESOURCE_ARRAY?custom->override_array:mode==Mode::RESOURCE_MIPS?custom->override_mips:-1;
            if(declared!=-1){result=float(declared);return true;}
        }
        active();auto b=get(input,state);
        // Sentinels from the pinned CommandList.cpp ResourcePropertyResult.
        if(!b){result=mode==Mode::RESOURCE_IDENTITY || mode==Mode::RESOURCE_BIND_FLAGS?0.0f:-2.0f;return true;}
        switch(mode){
        case Mode::RESOURCE_SIZE:result=b.visible_size?float(b.visible_size):b.image?-3.0f:float(b.description.ByteWidth);break;
        case Mode::RESOURCE_STRIDE:
            // This getter does not request a format, so IAGetIndexBuffer does
            // not derive stride from it. The resource description is used.
            if(effective(input,state).type==Target::INDEX_BUFFER)b.stride=0;
            result=b.stride?float(b.stride):b.image?-3.0f:b.description.StructureByteStride?float(b.description.StructureByteStride):-1.0f;break;
        case Mode::RESOURCE_IDENTITY:result=EncodeFloat30(uint32_t(HashPointer(b.image?static_cast<void*>(b.image.get()):static_cast<void*>(b.buffer.get()))));break;
        case Mode::RESOURCE_BIND_FLAGS:result=float(b.bind_flags());break;
        case Mode::RESOURCE_FORMAT:{
            auto format=b.format;
            if(format==DXGI_FORMAT_UNKNOWN && (input.type==Target::SHADER_RESOURCE || input.type==Target::UNORDERED_ACCESS_VIEW ||
               input.type==Target::RENDER_TARGET || input.type==Target::DEPTH_STENCIL_TARGET))format=b.view_format;
            if(format==DXGI_FORMAT_UNKNOWN && b.image)format=b.image_description.texture.Format;
            result=format==DXGI_FORMAT_UNKNOWN?-1.0f:float(format);break;
        }
        case Mode::RESOURCE_OFFSET:
            if(b.image){result=-3.0f;break;}
            result=-1.0f;
            if(input.type==Target::CONSTANT_BUFFER)result=float(b.offset);
            else if(input.type==Target::VERTEX_BUFFER || input.type==Target::INDEX_BUFFER){
                require(state.call_info!=nullptr,"GPU buffer offset query requires a draw context");
                result=float(input.type==Target::VERTEX_BUFFER?GetVertexBufferRegionOffset(b.stride,state.call_info,b.offset):
                             GetIndexBufferRegionOffset(b.format,state.call_info,b.offset));
            }break;
        case Mode::RESOURCE_WIDTH:result=b.image?float(b.image_description.texture.Width):-4.0f;break;
        case Mode::RESOURCE_HEIGHT:result=b.image?float(b.image_description.texture.Height):-4.0f;break;
        case Mode::RESOURCE_ARRAY:result=b.image && b.image_description.dimension!=D3D11_RESOURCE_DIMENSION_TEXTURE3D?float(b.image_description.texture.ArraySize):-4.0f;break;
        case Mode::RESOURCE_MIPS:result=b.image?float(b.image_description.texture.MipLevels):-4.0f;break;
        default:throw std::runtime_error("GPU resource property requires source-semantic adaptation");
        }
        return true;
    }catch(const std::exception& e){if(active_)failed_=true;reason=widen(e.what());return false;}
}
GpuResourceBinding GpuResourceRuntime::binding(wchar_t stage,Target type,unsigned index) const {
    const auto found=slots_.find({stage,type,index});return found==slots_.end()?GpuResourceBinding{}:found->second;
}
}
