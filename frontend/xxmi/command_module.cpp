// Compile the pinned original command engine without editing its source. This
// exposes its CPU input-layout evaluator to the backend declaration adapter.
#include "CommandList.cpp"
#include "image_rules.h"
#include "graphics_state.h"
#include <stdexcept>

bool FrontendApplyInputLayoutOverrides(std::vector<D3D11_INPUT_ELEMENT_DESC>& elements,CommandListState& state)
{
    return ApplyInputLayoutOverrides(elements,&state);
}

void FrontendRunCommandList(CommandList& list,CommandListState& state)
{
    _RunCommandList(&list,&state);
}

// A slot range as the original's SlotRangeCopyOperation::run resolves it (D-143): whether it binds slots
// or fetches them into a pool, its first slot, and per slot of the range the custom resource RunBind binds
// there (a pool element, the one resource every slot takes, or none) or the pool element RunFetch fills.
// False where the original does nothing: a range that does not resolve, or a pool range of another size.
bool FrontendSlotRange(SlotRangeCopyOperation& op,CommandListState& state,bool& bind,unsigned& first,std::vector<CustomResource*>& resources)
{
    bind=op.dst.evaluation_mode==ResourceCopyTargetEvaluationMode::SLOT_RANGE;
    ResourceCopyTarget& slots=bind?op.dst:op.src;
    ResourceCopyTarget& other=bind?op.src:op.dst;
    const bool has_pool=other.type==ResourceCopyTargetType::POOL;
    int slot_first=0,pool_first=0;unsigned count=0,pool_count=0;
    if(!slots.ResolveRange(&state,&slot_first,&count))return false;
    if(has_pool){
        if(!other.ResolveRange(&state,&pool_first,&pool_count))return false;
        if(pool_count!=count){
            LogOverlayW(LOG_WARNING,L"Slot range and pool range sizes differ (%u vs %u)\n - [%ls]\n",count,pool_count,op.ini_line.c_str());
            return false;
        }
    }
    if(!bind && !has_pool)return false;
    first=unsigned(slot_first);
    CustomResource* single=other.type==ResourceCopyTargetType::CUSTOM_RESOURCE?other.GetCustomResource(&state):nullptr;
    resources.clear();
    for(unsigned i=0;i<count;++i)resources.push_back(has_pool?pool_element(other.custom_resource_pool,pool_first,i,!bind):single);
    return true;
}

// Preserve the original buffer-view element calculation, including its use of
// buf_src_size as an end boundary rather than a length after offset.
void FrontendBufferViewElements(UINT stride,UINT offset,DXGI_FORMAT format,UINT end,UINT& first,UINT& count)
{
    D3D11_BUFFER_UAV view{};
    FillOutBufferDescCommon(&view,stride,offset,format,end);
    first=view.FirstElement;count=view.NumElements;
}

namespace efmi::frontend {
SourceGraphicsState source_graphics_defaults(){
    SourceGraphicsState state;
    state.blend=CD3D11_BLEND_DESC(D3D11_DEFAULT);
    state.depth=CD3D11_DEPTH_STENCIL_DESC(D3D11_DEFAULT);
    state.raster=CD3D11_RASTERIZER_DESC(D3D11_DEFAULT);
    return state;
}
SourceGraphicsState source_graphics_override(const CustomShader& shader,const SourceGraphicsState& previous){
    auto state=previous;
    auto merge=[](auto& dest,const auto& value,const auto& mask,const auto& inherited,int mode){
        if(mode<0 || mode>2)throw std::runtime_error("Invalid original graphics state override mode");
        if(!mode)return;
        dest=value;
        if(mode==2){auto source=inherited,copy_mask=mask;memcpy_masked_merge(&dest,&source,&copy_mask,sizeof(dest));}
    };
    merge(state.blend,shader.blend_desc,shader.blend_mask,previous.blend,shader.blend_override);
    if(shader.blend_override){
        for(unsigned i=0;i<4;++i)state.blend_factor[i]=shader.blend_override==2 && shader.blend_factor_merge_mask[i]?previous.blend_factor[i]:shader.blend_factor[i];
        state.sample_mask=shader.blend_sample_mask;
        if(shader.blend_override==2)state.sample_mask=(state.sample_mask&~shader.blend_sample_mask_merge_mask)|(previous.sample_mask&shader.blend_sample_mask_merge_mask);
    }
    merge(state.depth,shader.depth_stencil_desc,shader.depth_stencil_mask,previous.depth,shader.depth_stencil_override);
    if(shader.depth_stencil_override){state.stencil_ref=shader.stencil_ref;
        if(shader.depth_stencil_override==2)state.stencil_ref=(state.stencil_ref&~shader.stencil_ref_mask)|(previous.stencil_ref&shader.stencil_ref_mask);}
    merge(state.raster,shader.rs_desc,shader.rs_mask,previous.raster,shader.rs_override);
    if(shader.topology!=D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED)state.topology=shader.topology;
    if(shader.sampler_override)state.pixel_samplers[0]=shader.sampler_desc;
    return state;
}
namespace {
D3D11_TEXTURE1D_DESC texture1d(const ImageSourceDescription& source) {
    const auto& d=source.texture;return {d.Width,d.MipLevels,d.ArraySize,d.Format,d.Usage,d.BindFlags,d.CPUAccessFlags,d.MiscFlags};
}
D3D11_TEXTURE3D_DESC texture3d(const ImageSourceDescription& source) {
    const auto& d=source.texture;return {d.Width,d.Height,source.depth,d.MipLevels,d.Format,d.Usage,d.BindFlags,d.CPUAccessFlags,d.MiscFlags};
}
template<class T> bool fill_image_view(T& view,const ImageSourceDescription& source,DXGI_FORMAT format) {
    switch(source.dimension) {
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:{auto desc=texture1d(source);return FillOutTex1DDesc(&view,&desc,format)!=nullptr;}
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D:{auto desc=source.texture;return FillOutTex2DDesc(&view,&desc,format)!=nullptr;}
    case D3D11_RESOURCE_DIMENSION_TEXTURE3D:return FillOutTex3DDesc(&view,format)!=nullptr;
    default:return false;
    }
}
// The original predicate only asks GetDesc. This stack proxy exposes that CPU
// fact without creating a D3D device, resource or view in the Vulkan runtime.
class FormatQuery final : public ID3D11UnorderedAccessView {
public:
    explicit FormatQuery(DXGI_FORMAT f):format(f){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override {throw std::runtime_error("Unexpected image-rule QueryInterface");}
    ULONG STDMETHODCALLTYPE AddRef() override {throw std::runtime_error("Unexpected image-rule AddRef");}
    ULONG STDMETHODCALLTYPE Release() override {throw std::runtime_error("Unexpected image-rule Release");}
    void STDMETHODCALLTYPE GetDevice(ID3D11Device**) override {throw std::runtime_error("Unexpected image-rule GetDevice");}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*) override {throw std::runtime_error("Unexpected image-rule private data");}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*) override {throw std::runtime_error("Unexpected image-rule private data");}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*) override {throw std::runtime_error("Unexpected image-rule private data");}
    void STDMETHODCALLTYPE GetResource(ID3D11Resource**) override {throw std::runtime_error("Unexpected image-rule GetResource");}
    void STDMETHODCALLTYPE GetDesc(D3D11_UNORDERED_ACCESS_VIEW_DESC* desc) override {*desc={};desc->Format=format;}
private:
    DXGI_FORMAT format;
};
}
void original_image_override(CustomResource& resource,ImageSourceDescription& source) {
    auto& out=source.texture;
    switch(source.dimension) {
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:{auto d=texture1d(source);resource.OverrideTexDesc(&d);
        out={d.Width,1,d.MipLevels,d.ArraySize,d.Format,{1,0},d.Usage,d.BindFlags,d.CPUAccessFlags,d.MiscFlags};source.depth=1;break;}
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D:resource.OverrideTexDesc(&out);source.depth=1;break;
    case D3D11_RESOURCE_DIMENSION_TEXTURE3D:{auto d=texture3d(source);resource.OverrideTexDesc(&d);
        out={d.Width,d.Height,d.MipLevels,1,d.Format,{1,0},d.Usage,d.BindFlags,d.CPUAccessFlags,d.MiscFlags};source.depth=d.Depth;break;}
    default:throw std::runtime_error("Original image description dimension is invalid");
    }
}
DXGI_FORMAT original_typeless(DXGI_FORMAT f){return MakeTypeless(f);}
DXGI_FORMAT original_typed(DXGI_FORMAT f,bool depth){return depth?MakeDSVFormat(f):MakeNonDSVFormat(f);}
DXGI_FORMAT original_default_format(DXGI_FORMAT f){return EnsureNotTypeless(f);}
OriginalImageView original_image_view(const ImageSourceDescription& source,ResourceCopyTargetType type,DXGI_FORMAT format) {
    OriginalImageView view;
    switch(type) {
    case ResourceCopyTargetType::SHADER_RESOURCE:view.valid=fill_image_view(view.srv,source,format);break;
    case ResourceCopyTargetType::UNORDERED_ACCESS_VIEW:view.valid=fill_image_view(view.uav,source,format);break;
    case ResourceCopyTargetType::RENDER_TARGET:view.valid=fill_image_view(view.rtv,source,format);break;
    case ResourceCopyTargetType::DEPTH_STENCIL_TARGET:view.valid=fill_image_view(view.dsv,source,format);break;
    default:break;
    }
    return view;
}
bool original_float_clear(DXGI_FORMAT format){FormatQuery query(format);return UAVSupportsFloatClear(&query);}
}
