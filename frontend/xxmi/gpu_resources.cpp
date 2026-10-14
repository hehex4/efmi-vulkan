#include "gpu_resources.h"
#include "globals.h"
#include "util.h"
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
    default:throw std::runtime_error("GPU target needs a non-buffer adapter");
    }
}
bool same_desc(const D3D11_BUFFER_DESC& a,const D3D11_BUFFER_DESC& b) {
    return a.ByteWidth==b.ByteWidth && a.BindFlags==b.BindFlags && a.MiscFlags==b.MiscFlags && a.StructureByteStride==b.StructureByteStride;
}
void fill_info(GpuBufferBinding& b){
    // CPU mirror of the resource/view facts consumed by original FillInMissingInfo.
    b.visible_size=b.visible_size?std::min(b.visible_size,b.description.ByteWidth):b.description.ByteWidth;
    if(!b.stride)b.stride=b.description.StructureByteStride;
    if(b.view!=GpuBufferBinding::View::None){
        if(b.format==DXGI_FORMAT_UNKNOWN)b.format=b.view_format;
        if(!b.stride)b.stride=dxgi_format_size(b.format);
        if(!b.offset)b.offset=b.view_offset;
    }
    if(!b.stride)b.stride=dxgi_format_size(b.format);
    if(!b.stride)b.stride=b.visible_size;
}
void select_view(GpuBufferBinding& b,GpuBufferBinding::View kind){
    if(!b.buffer || b.view==kind)return;
    if(kind==GpuBufferBinding::View::None){b.view=kind;return;}
    fill_info(b);auto format=b.format;auto stride=b.stride;
    const bool raw=(b.description.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS) &&
        (format==DXGI_FORMAT_UNKNOWN || format==DXGI_FORMAT_R32_TYPELESS);
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
GpuBufferBinding::View view_kind(Target t){return t==Target::SHADER_RESOURCE?GpuBufferBinding::View::ShaderResource:
    t==Target::UNORDERED_ACCESS_VIEW?GpuBufferBinding::View::UnorderedAccess:GpuBufferBinding::View::None;}
}
GpuResourceRuntime::GpuResourceRuntime(gpu::ResourceDevice d,gpu::ResourceBudgets budgets):commands_(d,std::move(budgets)){}
std::vector<std::wstring> GpuResourceRuntime::install(DrawPlanAdapter& adapter) {
    require(!installed_,"GPU resource runtime cannot belong to two INI generations");installed_=true;
    adapter.resource_commands([this](auto& command,auto& state,auto& why){return run(command,state,why);});
    adapter.resource_queries([this](auto& target,auto& state,float& value,auto& why){return query(target,state,value,why);});
    return {};
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
    default:throw std::runtime_error("GPU target has no buffer slot adapter");
    }
    require(!stage || (t.shader_type && std::wstring(L"vhdgpc").find(t.shader_type)!=std::wstring::npos),"GPU shader stage invalid");
    const auto index=t.ResolveSlot(&state);require(index<limit,"GPU binding slot out of range");return {stage?t.shader_type:L'\0',t.type,index};
}
CustomResource* GpuResourceRuntime::declaration(ResourceCopyTarget& t,CommandListState& state,bool writing) {
    auto* r=t.GetCustomResource(&state,writing);
    if(r && r->pool){const auto* p=r->pool->ResolvePool();
        require((p->index_type==PoolIndexType::STATIC || p->index_type==PoolIndexType::RING) && p->expiration_timeout_frames==UINT32_MAX,
                "GPU resource pool expiration/key ownership is not adapted");}
    return r;
}
GpuBufferBinding GpuResourceRuntime::load(CustomResource& r) {
    const auto found=resources_.find(&r);
    if(found!=resources_.end())return r.is_null?GpuBufferBinding{}:found->second;
    require(resources_.size()<65536,"GPU resource declaration budget exceeded");
    if(r.filename.empty() && !r.initial_data && r.override_type==CustomResourceType::INVALID && r.override_format==DXGI_FORMAT(-1) && r.override_stride==-1)
        return {};
    require(r.override_type==CustomResourceType::BUFFER || r.override_type==CustomResourceType::STRUCTURED_BUFFER ||
            r.override_type==CustomResourceType::RAW_BUFFER || r.override_type==CustomResourceType::INVALID,"GPU image declaration requires image commands");
    GpuBufferBinding b;std::vector<uint8_t> bytes;
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
    r.OverrideOutOfBandInfo(&b.format,&b.stride);b.source_stride=b.stride;
    b.buffer=commands_.create(b.description.ByteWidth,usage(b.description),bytes.empty()?nullptr:bytes.data(),std::min<size_t>(bytes.size(),b.description.ByteWidth),true);
    r.is_null=false;resources_[&r]=b;return b;
}
GpuBufferBinding GpuResourceRuntime::get(ResourceCopyTarget& input,CommandListState& state) {
    auto& t=effective(input,state);GpuBufferBinding b;
    require(!t.IsRange(),"GPU range command requires the slot-range adapter");
    if(t.type==Target::EMPTY)return {};
    if(t.type==Target::CUSTOM_RESOURCE){if(auto* r=declaration(t,state))b=load(*r);}
    else {const auto key=slot(t,state);const auto found=slots_.find(key);if(found!=slots_.end())b=found->second;
        // XXGetShaderResources/UnorderedAccessViews return resource and view,
        // not the custom declaration's out-of-band stride/offset/size/format.
        if(t.type==Target::SHADER_RESOURCE || t.type==Target::UNORDERED_ACCESS_VIEW){b.stride=0;b.offset=0;b.visible_size=0;b.format=DXGI_FORMAT_UNKNOWN;b.region=false;}}
    if(b.buffer && t.evaluation_mode==Mode::RESOURCE_REGION) {
        require(t.member_args[0].expression && t.member_args[1].expression,"GPU region requires offset and size");
        b.offset=uint_value(t.member_args[0].GetValue(&state));b.visible_size=uint_value(t.member_args[1].GetValue(&state));
        b.region=true;
        require(b.offset<b.buffer->size() && b.visible_size<=b.buffer->size(),"GPU resource region outside allocation");
    }
    return b;
}
void GpuResourceRuntime::set(ResourceCopyTarget& input,CommandListState& state,const GpuBufferBinding& b) {
    auto& t=effective(input,state);
    require(t.evaluation_mode==Mode::RESOURCE && !t.IsRange(),"GPU assignment needs a single resource target");
    if(t.type==Target::CUSTOM_RESOURCE){auto* r=declaration(t,state,true);require(r!=nullptr,"GPU resource destination missing");
        require(resources_.size()<65536 || resources_.count(r),"GPU resource declaration budget exceeded");
        resources_[r]=b;r->is_null=!b.buffer;r->stride=b.stride;r->source_stride=b.source_stride;r->offset=b.offset;r->buf_size=b.visible_size;r->format=b.format;
    }else {
        auto key=slot(t,state);
        require(!b.buffer || (b.description.BindFlags&bind_flag(t.type)),"Referenced GPU resource lacks destination bind flags");
        auto bound=b;
        select_view(bound,view_kind(t.type));
        if(t.type==Target::CONSTANT_BUFFER && bound.region)
            require(bound.offset%16==0 && bound.visible_size%16==0 && bound.visible_size<=bound.description.ByteWidth-bound.offset,
                    "GPU constant-buffer reference region is not aligned or outside allocation");
        slots_[key]=std::move(bound);
    }
}
void GpuResourceRuntime::copy(ResourceCopyOperation& op,CommandListState& state) {
    require(typeid(op)==typeid(ResourceCopyOperation),"GPU staging command requires staging adaptation");
    require(op.src.evaluation_mode==Mode::RESOURCE || op.src.evaluation_mode==Mode::RESOURCE_REGION,"GPU copy source is not a resource");
    constexpr auto allowed=uint32_t(ResourceCopyOptions::COPY)|uint32_t(ResourceCopyOptions::COPY_DESC)|uint32_t(ResourceCopyOptions::REFERENCE)|uint32_t(ResourceCopyOptions::UNLESS_NULL);
    require(!(uint32_t(op.options)&~allowed),"GPU copy option requires an additional adapter");
    auto& dst=effective(op.dst,state);
    if(op.src.type==Target::EMPTY){set(dst,state,{});return;} // Explicit null ignores unless_null upstream.
    auto source=get(op.src,state);
    if(!source.buffer){if(!(op.options&ResourceCopyOptions::UNLESS_NULL))set(dst,state,{});return;}
    auto* custom=dst.type==Target::CUSTOM_RESOURCE?declaration(dst,state,true):nullptr;
    const bool explicit_region=op.src.evaluation_mode==Mode::RESOURCE_REGION;
    const auto region_size=source.visible_size;
    if(custom && custom->max_copies_per_frame) {
        if(custom->frame_no!=G->frame_no){custom->frame_no=G->frame_no;custom->copies_this_frame=1;}
        else if(custom->copies_this_frame++>=custom->max_copies_per_frame)return;
    }
    if(custom)custom->OverrideOutOfBandInfo(&source.format,&source.stride);
    fill_info(source);
    if(!(op.options&ResourceCopyOptions::COPY_MASK)){
        // UsableRefView retains a source view on a custom resource. If the
        // source has none, the original may reuse its old compatible view.
        if(custom && source.view==GpuBufferBinding::View::None){const auto cached=resources_.find(custom);
            if(cached!=resources_.end() && cached->second.buffer==source.buffer){source.view=cached->second.view;
                source.view_format=cached->second.view_format;source.view_offset=cached->second.view_offset;source.view_size=cached->second.view_size;}}
        if(!custom)select_view(source,view_kind(dst.type));
        source.visible_size=explicit_region?region_size:0;source.region=explicit_region;
        // The original clears a constant-slot offset unless ->Region was used.
        if(dst.type==Target::CONSTANT_BUFFER && op.src.evaluation_mode!=Mode::RESOURCE_REGION){source.offset=0;source.visible_size=0;source.region=false;}
        set(dst,state,source);return;
    }
    require(!source.offset && !source.region,"GPU region copy/conversion requires explicit source-view semantics");
    GpuBufferBinding out=source;out.description.BindFlags=custom?custom->bind_flags:bind_flag(dst.type);
    if(!(out.description.BindFlags&(D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS)))out.description.MiscFlags&=~D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    if(out.description.BindFlags&D3D11_BIND_CONSTANT_BUFFER){
        require(out.description.ByteWidth%16==0 && out.description.ByteWidth<=65536,"GPU copy constant-buffer resizing is not yet adapted");
        out.description.MiscFlags&=~D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;out.description.StructureByteStride=0;
    }
    if(custom){custom->OverrideBufferDesc(&out.description);custom->OverrideOutOfBandInfo(&out.format,&out.stride);}
    out.offset=0;out.visible_size=0;out.region=false;out.view=GpuBufferBinding::View::None;
    validate_desc(out.description);
    require((op.options&ResourceCopyOptions::COPY_DESC) || out.description.ByteWidth==source.description.ByteWidth,"GPU size-changing copy requires a region adapter");
    auto& cache=custom?resources_[custom]:copies_[&op];
    if(cache.buffer && same_desc(cache.description,out.description)){out.buffer=cache.buffer;out.view=cache.view;
        out.view_format=cache.view_format;out.view_offset=cache.view_offset;out.view_size=cache.view_size;}
    else out.buffer=commands_.create(out.description.ByteWidth,usage(out.description));
    if(!(op.options&ResourceCopyOptions::COPY_DESC))commands_.copy(source.buffer,0,out.buffer,0,source.description.ByteWidth);
    cache=out;set(dst,state,out);
}
void GpuResourceRuntime::clear(ClearViewCommand& op,CommandListState& state) {
    auto b=get(op.target,state);if(!b.buffer)return;
    // ClearViewCommand queries the existing view first. An SRV has no clear
    // operation, even when its underlying buffer also has the UAV bind flag.
    if(b.view==GpuBufferBinding::View::ShaderResource)return;
    select_view(b,GpuBufferBinding::View::UnorderedAccess);
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
        const auto* actual=pool->ResolvePool();
        require((actual->index_type==PoolIndexType::RING || actual->index_type==PoolIndexType::STATIC) &&
                actual->expiration_timeout_frames==UINT32_MAX,"GPU range pool expiration/key ownership is not adapted");
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
    std::vector<GpuBufferBinding> fetched;
    if(!bind)for(unsigned i=0;i<count;++i){auto& one=operation(i);fetched.push_back(get(one.src,state));}
    CustomResource* broadcast=bind && other.type==Target::CUSTOM_RESOURCE?declaration(other,state):nullptr;
    for(unsigned i=0;i<count;++i){auto& one=operation(i);
        if(bind){
            auto* source=pool?pool->GetResource(float(int64_t(pool_first)+i),false,true,false):broadcast;
            one.src.SetCustomResource(source);
            if(per_slot){copy(one,state);continue;}
            auto value=source?load(*source):GpuBufferBinding{};
            // Unlike scalar `null`, range null with unless_null preserves slots.
            if(!value.buffer && (op.options&ResourceCopyOptions::UNLESS_NULL))continue;
            if(value.buffer){
                require(value.description.BindFlags&bind_flag(slots.type),"Range reference lacks destination bind flags");
                const auto kind=view_kind(slots.type);
                if(kind==GpuBufferBinding::View::None)value.view=kind;
                else if(value.view!=kind){
                    if(views[i].buffer==value.buffer && views[i].view==kind){value.view=kind;value.view_format=views[i].view_format;
                        value.view_offset=views[i].view_offset;value.view_size=views[i].view_size;}
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
            slots_[slot(one.dst,state)]=std::move(value);
        }else{
            const auto& source=fetched[i];
            if(!source.buffer && (op.options&ResourceCopyOptions::UNLESS_NULL))continue;
            auto* destination=pool->GetResource(float(int64_t(pool_first)+i),false,true,true);
            require(destination!=nullptr,"GPU range pool destination missing");one.dst.SetCustomResource(destination);
            if(per_slot){copy(one,state);continue;}
            // Plain range fetch is SetResource directly: no per-frame copy
            // counter and no destination stride/format override is applied.
            auto value=source;value.stride=0;value.format=DXGI_FORMAT_UNKNOWN;value.source_stride=0;
            set(one.dst,state,value);
        }
    }
    if(op.options&ResourceCopyOptions::NO_VIEW_CACHE)for(auto& cached:views)cached={};
}
void GpuResourceRuntime::store(StoreCommand& op,CommandListState& state) {
    auto source=get(op.src,state);if(!source.buffer)return; // Original null keeps the previous variable value.
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
    try{active();
        if(auto* c=dynamic_cast<ResourceCopyOperation*>(&command))copy(*c,state);
        else if(auto* c=dynamic_cast<SlotRangeCopyOperation*>(&command))ranges(*c,state);
        else if(auto* c=dynamic_cast<ClearViewCommand*>(&command))clear(*c,state);
        else if(auto* c=dynamic_cast<StoreCommand*>(&command))store(*c,state);
        else throw std::runtime_error("GPU draw/dispatch needs a shader execution adapter");
        return true;
    }catch(const std::exception& e){if(active_)failed_=true;reason=widen(e.what())+L": "+command.ini_line;return false;}
}
bool GpuResourceRuntime::query(ResourceCopyTarget& input,CommandListState& state,float& result,std::wstring& reason) {
    try{active();auto& t=effective(input,state);
        require(t.evaluation_mode==Mode::RESOURCE_STRIDE || t.evaluation_mode==Mode::RESOURCE_SIZE,
                "GPU resource property requires source-semantic adaptation");
        if(t.evaluation_mode==Mode::RESOURCE_STRIDE && t.type==Target::CUSTOM_RESOURCE){result=t.GetResourceStride(&state);return true;}
        if(t.evaluation_mode==Mode::RESOURCE_STRIDE && t.type==Target::CONSTANT_BUFFER){result=16;return true;}
        auto b=get(t,state);
        // Sentinels from the pinned CommandList.cpp ResourcePropertyResult.
        if(!b.buffer){result=-2.0f;return true;}
        switch(t.evaluation_mode){
        case Mode::RESOURCE_SIZE:result=float(b.visible_size?b.visible_size:b.description.ByteWidth);break;
        case Mode::RESOURCE_STRIDE:result=b.stride?float(b.stride):b.description.StructureByteStride?float(b.description.StructureByteStride):-1.0f;break;
        default:throw std::runtime_error("GPU resource property requires source-semantic adaptation");
        }
        return true;
    }catch(const std::exception& e){if(active_)failed_=true;reason=widen(e.what());return false;}
}
GpuBufferBinding GpuResourceRuntime::binding(wchar_t stage,Target type,unsigned index) const {
    const auto found=slots_.find({stage,type,index});return found==slots_.end()?GpuBufferBinding{}:found->second;
}
}
