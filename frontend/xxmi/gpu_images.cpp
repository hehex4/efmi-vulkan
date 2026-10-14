#include "gpu_resources.h"
#include "image_formats.h"
#include "../gpu/image_formats.h"
#include "util.h"
#include "PlatformHelpers.h"
#include <wrl/client.h>
#include <wincodec.h>
#include "LoaderHelpers.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace efmi::frontend {
namespace {
using Target=ResourceCopyTargetType;
using View=GpuResourceBinding::View;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
UINT rtv_integer(float value,UINT bits,bool sign){
    if(!bits || std::isnan(value))return 0;
    // Native WARP and hardware RTV clears truncate fractions, unlike the
    // historical DDI spec's general nearest-even wording. Saturation follows
    // WARP at overflow; native drivers can differ outside the 32-bit range.
    // Clamp as double before casting so NaN/infinity and UINT32_MAX are safe.
    const auto limit=uint64_t(1)<<(sign?bits-1:bits);
    const double low=sign?-double(limit):0.0,high=double(limit-1);
    const auto bounded=std::clamp(double(value),low,high);
    return sign?UINT(int64_t(bounded)):UINT(bounded);
}
UINT image_bind_flag(View kind){
    switch(kind){case View::ShaderResource:return D3D11_BIND_SHADER_RESOURCE;case View::UnorderedAccess:return D3D11_BIND_UNORDERED_ACCESS;
    case View::RenderTarget:return D3D11_BIND_RENDER_TARGET;case View::DepthStencil:return D3D11_BIND_DEPTH_STENCIL;default:return 0;}
}
View image_view_kind(Target type){switch(type){case Target::SHADER_RESOURCE:return View::ShaderResource;
    case Target::UNORDERED_ACCESS_VIEW:return View::UnorderedAccess;case Target::RENDER_TARGET:return View::RenderTarget;
    case Target::DEPTH_STENCIL_TARGET:return View::DepthStencil;default:return View::None;}}
Target image_target(View kind){switch(kind){case View::ShaderResource:return Target::SHADER_RESOURCE;case View::UnorderedAccess:return Target::UNORDERED_ACCESS_VIEW;
    case View::RenderTarget:return Target::RENDER_TARGET;case View::DepthStencil:return Target::DEPTH_STENCIL_TARGET;default:return Target::EMPTY;}}
VkImageUsageFlags image_usage(UINT flags){
    constexpr UINT allowed=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_RENDER_TARGET|D3D11_BIND_DEPTH_STENCIL;
    require(!(flags&~allowed),"Original texture bind flags are invalid for an image");VkImageUsageFlags usage=0;
    if(flags&D3D11_BIND_SHADER_RESOURCE)usage|=VK_IMAGE_USAGE_SAMPLED_BIT;
    if(flags&D3D11_BIND_UNORDERED_ACCESS)usage|=VK_IMAGE_USAGE_STORAGE_BIT;
    if(flags&D3D11_BIND_RENDER_TARGET)usage|=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if(flags&D3D11_BIND_DEPTH_STENCIL)usage|=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;return usage;
}
std::array<UINT,13> image_key(const ImageSourceDescription& source){const auto& d=source.texture;
    return {UINT(source.dimension),d.Width,d.Height,source.depth,d.MipLevels,d.ArraySize,UINT(d.Format),d.SampleDesc.Count,
        d.SampleDesc.Quality,UINT(d.Usage),d.BindFlags,d.CPUAccessFlags,d.MiscFlags};}
void apply_overrides(CustomResource& resource,ImageSourceDescription& source){
    const bool line=source.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE1D;
    const bool plane=source.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    // Validate only fields consumed by the matching original overload. Shared
    // declarations may contain unused dimension/sample overrides; those do not
    // become image-creation inputs merely because they exist in the INI.
    auto field=[](int value){require(value>=-1,"Negative image description override");};
    field(resource.override_width);field(resource.override_mips);
    if(!line)field(resource.override_height);
    if(line || plane)field(resource.override_array);else field(resource.override_depth);
    if(plane){field(resource.override_msaa);field(resource.override_msaa_quality);}
    const auto width=resource.override_width!=-1?UINT(resource.override_width):source.texture.Width;
    auto height=resource.override_height!=-1?UINT(resource.override_height):source.texture.Height;
    // Preserve the pinned source's 3D depth-to-Height assignment, including its
    // effect on the subsequent height multiplier. A declaration cannot invent Depth.
    if(source.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE3D && resource.override_depth!=-1)height=UINT(resource.override_depth);
    auto multiplied=[](UINT value,float multiplier){
        const auto product=float(value)*multiplier;
        require(std::isfinite(multiplier) && multiplier>=0 && std::isfinite(product) && double(product)<=UINT32_MAX,
                "Image dimension multiplier is outside unsigned range");
    };
    multiplied(width,resource.width_multiply);if(!line)multiplied(height,resource.height_multiply);
    require(!plane || resource.override_type!=CustomResourceType::CUBE || resource.override_array<=INT32_MAX/6,"Cube array override overflows the original count");
    original_image_override(resource,source);
}
struct DdsLevel {size_t offset=0,bytes=0;UINT mip=0,layer=0;};
struct DdsSource {ImageSourceDescription description;std::vector<uint8_t> bytes;std::vector<DdsLevel> levels;};
DdsSource read_dds(const CustomResource& resource) {
    using namespace DirectX;using namespace DirectX::LoaderHelpers;
    const auto path=std::filesystem::path(resource.filename);
    require(!_wcsicmp(path.extension().c_str(),L".dds"),"Non-DDS image loading remains a separate adapter; PNG integration is paused");
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    require(bool(stream) && stream.tellg()>=128 && uint64_t(stream.tellg())<=(128ull<<20),"DDS source is unreadable or outside its file budget");
    DdsSource out;out.bytes.resize(size_t(stream.tellg()));stream.seekg(0);stream.read(reinterpret_cast<char*>(out.bytes.data()),out.bytes.size());
    require(bool(stream),"DDS source read failed");UINT magic=0;DDS_HEADER header{};memcpy(&magic,out.bytes.data(),4);memcpy(&header,out.bytes.data()+4,sizeof(header));
    require(magic==DDS_MAGIC && header.size==sizeof(header) && header.ddspf.size==sizeof(DDS_PIXELFORMAT),"DDS magic or header size is invalid");
    auto& source=out.description;auto& d=source.texture;source.dimension=D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width=header.width;d.Height=header.height;source.depth=1;d.ArraySize=1;d.MipLevels=header.mipMapCount?header.mipMapCount:1;
    d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=resource.override_bind_flags==CustomResourceBindFlags::INVALID?resource.bind_flags:UINT(resource.override_bind_flags);
    d.MiscFlags=resource.override_misc_flags==ResourceMiscFlags::INVALID?resource.misc_flags:UINT(resource.override_misc_flags);
    // DirectXTK derives cube-ness from the file, clearing a caller's cube flag
    // for ordinary 1D/2D/3D files before setting it for actual cube metadata.
    d.MiscFlags&=~D3D11_RESOURCE_MISC_TEXTURECUBE;
    size_t offset=4+sizeof(header);
    if((header.ddspf.flags&DDS_FOURCC) && header.ddspf.fourCC==MAKEFOURCC('D','X','1','0')){
        require(out.bytes.size()-offset>=sizeof(DDS_HEADER_DXT10),"DDS DX10 header is truncated");DDS_HEADER_DXT10 extra{};
        memcpy(&extra,out.bytes.data()+offset,sizeof(extra));offset+=sizeof(extra);d.Format=extra.dxgiFormat;d.ArraySize=extra.arraySize;
        require(d.ArraySize>0 && d.ArraySize<=2048,"DDS array count is invalid");source.dimension=D3D11_RESOURCE_DIMENSION(extra.resourceDimension);
        if(source.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE1D){require(!(header.flags&DDS_HEIGHT) || d.Height==1,"DDS 1D height is invalid");d.Height=1;}
        else if(source.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE2D){
            if(extra.miscFlag&DDS_RESOURCE_MISC_TEXTURECUBE){require(d.ArraySize<=2048/6,"DDS cube array count is invalid");d.ArraySize*=6;d.MiscFlags|=D3D11_RESOURCE_MISC_TEXTURECUBE;}
        }else if(source.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE3D){
            require((header.flags&DDS_HEADER_FLAGS_VOLUME) && d.ArraySize==1,"DDS volume metadata is invalid");source.depth=header.depth;
        }else throw std::runtime_error("DDS resource dimension is not a texture");
    }else{
        d.Format=GetDXGIFormat(header.ddspf);
        if(header.flags&DDS_HEADER_FLAGS_VOLUME){source.dimension=D3D11_RESOURCE_DIMENSION_TEXTURE3D;source.depth=header.depth;}
        else if(header.caps2&DDS_CUBEMAP){require((header.caps2&DDS_CUBEMAP_ALLFACES)==DDS_CUBEMAP_ALLFACES,"DDS cube must contain all faces");
            d.ArraySize=6;d.MiscFlags|=D3D11_RESOURCE_MISC_TEXTURECUBE;}
    }
    require(d.Width && d.Width<=16384 && d.Height && d.Height<=16384 && source.depth && source.depth<=2048 &&
            d.MipLevels<=D3D11_REQ_MIP_LEVELS && d.ArraySize<=2048,"DDS dimensions exceed source limits");
    if(resource.override_color_space==CustomColorSpace::SRGB)d.Format=MakeSRGB(d.Format);
    // Use the same pinned DirectXTK format decoding and surface footprint rules
    // as the original file loader. Declaration dimension/format overrides do not
    // resize file textures in that loader; only its bind/misc and sRGB options apply.
    source_image_format(original_default_format(d.Format));
    for(UINT layer=0;layer<d.ArraySize;++layer){size_t w=d.Width,h=d.Height,z=source.depth;
        for(UINT mip=0;mip<d.MipLevels;++mip){size_t bytes=0,row=0;GetSurfaceInfo(w,h,d.Format,&bytes,&row,nullptr);
            require(bytes && z<=SIZE_MAX/bytes,"DDS surface footprint is invalid");bytes*=z;
            require(offset<=out.bytes.size() && bytes<=out.bytes.size()-offset,"DDS subresource payload is truncated");
            out.levels.push_back({offset,bytes,mip,layer});offset+=bytes;w=std::max<size_t>(1,w/2);h=std::max<size_t>(1,h/2);z=std::max<size_t>(1,z/2);
        }
    }
    return out;
}
}

gpu::Image GpuResourceRuntime::allocate_image(ImageSourceDescription& source) {
    auto& d=source.texture;
    require(d.Usage==D3D11_USAGE_DEFAULT && !d.CPUAccessFlags,"Owned image usage requires an explicit staging adapter");
    require(!(d.MiscFlags&~(D3D11_RESOURCE_MISC_TEXTURECUBE|D3D11_RESOURCE_MISC_GENERATE_MIPS)),"Image misc flags require another source adapter");
    require(!(d.MiscFlags&D3D11_RESOURCE_MISC_GENERATE_MIPS) ||
        (d.BindFlags&(D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET))==(D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET),"Original generate-mips flags need SRV and RTV binds");
    require(d.SampleDesc.Quality==0,"Nonzero DX11 multisample quality requires sample-pattern adaptation");
    require(d.SampleDesc.Count<=D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT &&
        (d.SampleDesc.Count<=1 || !(d.BindFlags&D3D11_BIND_UNORDERED_ACCESS)),"Original multisample count or UAV flags are invalid");
    require(d.Width && d.Width<=16384 && d.Height && d.Height<=16384 && source.depth && source.depth<=2048 &&
        d.ArraySize && d.ArraySize<=2048,"Original image dimensions are invalid");
    gpu::ImageDescription desc;desc.type=source.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE1D?VK_IMAGE_TYPE_1D:
        source.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE2D?VK_IMAGE_TYPE_2D:VK_IMAGE_TYPE_3D;
    require(source.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE1D || source.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE2D ||
        source.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE3D,"Original image dimension is invalid");
    if(desc.type==VK_IMAGE_TYPE_3D)require(d.Width<=2048 && d.Height<=2048 && d.ArraySize==1,"Original volume dimensions are invalid");
    UINT mips=1;for(auto n=std::max({d.Width,d.Height,source.depth});n>1;n>>=1)++mips;
    if(!d.MipLevels)d.MipLevels=mips;
    require(d.MipLevels<=mips && d.MipLevels<=D3D11_REQ_MIP_LEVELS,"Original image mip chain is invalid");
    const bool typeless=original_default_format(d.Format)!=d.Format;
    const auto typed=typeless?original_typed(d.Format,bool(d.BindFlags&D3D11_BIND_DEPTH_STENCIL)):d.Format;
    const auto format=source_image_format(typed);
    require(!(d.BindFlags&D3D11_BIND_DEPTH_STENCIL) || (format.aspects&VK_IMAGE_ASPECT_DEPTH_BIT),"Typed color allocation cannot become an original depth target");
    desc.format=format.format;desc.extent={d.Width,d.Height,source.depth};desc.mips=d.MipLevels;desc.layers=d.ArraySize;
    desc.samples=VkSampleCountFlagBits(d.SampleDesc.Count);desc.usage=image_usage(d.BindFlags);
    if(typeless && format.aspects==VK_IMAGE_ASPECT_COLOR_BIT)desc.flags|=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    if(d.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE){require(d.ArraySize%6==0,"Original cube image array must contain full cubes");desc.flags|=VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;}
    return commands_.create_image(desc);
}
GpuResourceBinding GpuResourceRuntime::load_image(CustomResource& resource) {
    GpuResourceBinding out;auto& source=out.image_description;auto& d=source.texture;
    if(resource.filename.empty()){
        source.dimension=resource.override_type==CustomResourceType::TEXTURE1D?D3D11_RESOURCE_DIMENSION_TEXTURE1D:
            resource.override_type==CustomResourceType::TEXTURE3D?D3D11_RESOURCE_DIMENSION_TEXTURE3D:D3D11_RESOURCE_DIMENSION_TEXTURE2D;
        if(source.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE3D)source.depth=0;
        d.BindFlags=resource.bind_flags;d.MiscFlags=resource.misc_flags;d.Usage=D3D11_USAGE_DEFAULT;
        apply_overrides(resource,source);out.image=allocate_image(source);
    }else{
        auto file=read_dds(resource);source=file.description;out.image=allocate_image(source);
        const auto& vk=out.image->description();const auto aspects=gpu::image_formats[unsigned(vk.format)].aspects;
        for(const auto& level:file.levels){VkBufferImageCopy copy{};copy.imageSubresource={aspects,level.mip,level.layer,1};copy.imageExtent=out.image->extent(level.mip);
            const auto* data=file.bytes.data()+level.offset;
            if(aspects==(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT)){
                const size_t stride=vk.format==VK_FORMAT_D32_SFLOAT_S8_UINT?8:4;
                require(level.bytes%stride==0,"DDS packed depth/stencil payload is misaligned");const auto count=level.bytes/stride;
                std::vector<uint8_t> depth(count*4),stencil(count);
                for(size_t i=0;i<count;++i){memcpy(depth.data()+i*4,data+i*stride,4);stencil[i]=data[i*stride+(stride==8?4:3)];}
                copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;commands_.write(out.image,copy,depth.data(),depth.size());
                copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;commands_.write(out.image,copy,stencil.data(),stencil.size());
            }else commands_.write(out.image,copy,data,level.bytes);
        }
    }
    // SubstantiateTexture and LoadFromFile leave out-of-band format/stride at
    // their original values; declaration property overrides are queried separately.
    out.format=resource.format;out.stride=resource.stride;out.offset=resource.offset;out.visible_size=resource.buf_size;out.source_stride=resource.source_stride;
    resource.is_null=false;resources_[&resource]=out;return out;
}
void GpuResourceRuntime::image_view(GpuResourceBinding& b,View kind,bool complete_info) {
    if(kind==View::None){b.drop_view();return;}
    if(b.view==kind && b.image_view)return;
    require(b.bind_flags()&image_bind_flag(kind),"Original image allocation lacks view bind flags");
    if(complete_info)fill_info(b);
    const auto original=original_image_view(b.image_description,image_target(kind),b.format);
    require(original.valid,"Original image view dimension is invalid");
    auto range=b.image->full_range();VkImageViewType type=VK_IMAGE_VIEW_TYPE_2D;VkImageUsageFlags usage=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    const auto& source=b.image_description;const auto& d=source.texture;
    if(source.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE1D)type=d.ArraySize==1?VK_IMAGE_VIEW_TYPE_1D:VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    else if(source.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE3D)type=VK_IMAGE_VIEW_TYPE_3D;
    else type=d.ArraySize==1?VK_IMAGE_VIEW_TYPE_2D:VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    if(kind==View::ShaderResource){format=original.srv.Format;usage=VK_IMAGE_USAGE_SAMPLED_BIT;
        if(original.srv.ViewDimension==D3D11_SRV_DIMENSION_TEXTURECUBE)type=VK_IMAGE_VIEW_TYPE_CUBE;
        else if(original.srv.ViewDimension==D3D11_SRV_DIMENSION_TEXTURECUBEARRAY)type=VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    }else{range.levelCount=1;
        if(kind==View::UnorderedAccess){format=original.uav.Format;usage=VK_IMAGE_USAGE_STORAGE_BIT;require(d.SampleDesc.Count==1,"Original multisampled image cannot have a UAV");}
        else if(kind==View::RenderTarget){format=original.rtv.Format;usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;}
        else {format=original.dsv.Format;usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;}
    }
    // A supplied UNKNOWN view descriptor inherits a fully typed texture format
    // in D3D11. It does not choose a typed format for a typeless allocation.
    // The headless source-API fixture checks SRV, UAV and RTV behavior directly.
    if(format==DXGI_FORMAT_UNKNOWN){
        require(original_default_format(d.Format)==d.Format,"Original UNKNOWN image view cannot infer a typeless resource format");format=d.Format;
    }
    const bool typeless=original_default_format(d.Format)!=d.Format;
    require(typeless?source_image_family(format)==d.Format:format==d.Format,"Original image view format is outside its DXGI resource family");
    auto translated=source_image_format(format);const auto native=b.image->description().format;
    // D3D depth-compatible SRVs reinterpret one aspect of the same allocation;
    // Vulkan expresses this with an aspect mask, not a color-format image view.
    if(gpu::image_formats[unsigned(native)].aspects&VK_IMAGE_ASPECT_DEPTH_BIT){
        require(kind==View::ShaderResource || kind==View::DepthStencil,"Depth allocation cannot use this original view type");
        if(translated.aspects==VK_IMAGE_ASPECT_COLOR_BIT){require(format==DXGI_FORMAT_R32_FLOAT || format==DXGI_FORMAT_R16_UNORM,"Depth SRV format is incompatible");translated.aspects=VK_IMAGE_ASPECT_DEPTH_BIT;}
        translated.format=native;
    }
    range.aspectMask=translated.aspects;b.image_view=commands_.view(b.image,type,translated.format,range,usage);b.view=kind;b.view_format=format;
}
void GpuResourceRuntime::image_copy(ResourceCopyOperation& op,ResourceCopyTarget& destination,CommandListState& state,
    GpuResourceBinding source,CustomResource* custom,GpuResourceBinding& cache) {
    auto out=source;out.drop_view();auto& d=out.image_description.texture;
    d.BindFlags=custom?custom->bind_flags:image_bind_flag(image_view_kind(destination.type));
    require(custom || d.BindFlags,"Image cannot be copied into a buffer binding");
    d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.Format=original_typeless(d.Format);d.MiscFlags&=~D3D11_RESOURCE_MISC_GENERATE_MIPS;
    if((op.options&ResourceCopyOptions::RESOLVE_MSAA) && out.image_description.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE2D)d.SampleDesc={1,0};
    if(custom)apply_overrides(*custom,out.image_description);
    auto& pool=custom?resource_image_pools_[custom]:command_image_pools_[&op];const auto key=image_key(out.image_description);auto found=pool.find(key);
    if(found==pool.end()){out.image=allocate_image(out.image_description);pool.emplace(key,out.image);}
    else{out.image=found->second;d.MipLevels=out.image->description().mips;}
    if(cache.image==out.image){out.view=cache.view;out.image_view=cache.image_view;out.view_format=cache.view_format;}
    if(!(op.options&ResourceCopyOptions::COPY_DESC)){
        const auto& a=source.image->description();const auto& b=out.image->description();
        if(op.options&ResourceCopyOptions::RESOLVE_MSAA){
            if(out.image_description.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE2D){
                require(original_float_clear(original_default_format(d.Format)),"Original integer image format cannot be resolved");
                require(a.layers==b.layers && a.mips==b.mips,"Original image resolve subresource counts differ");
                for(UINT mip=0;mip<b.mips;++mip){VkImageResolve r{};r.srcSubresource=r.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,mip,0,b.layers};r.extent=out.image->extent(mip);
                    commands_.resolve(source.image,out.image,r);}
            }
        }else{
            require(source_image_family(source.image_description.texture.Format)==source_image_family(d.Format),"Original image copy formats belong to different DXGI families");
            require(a.type==b.type && a.extent.width==b.extent.width && a.extent.height==b.extent.height && a.extent.depth==b.extent.depth &&
                a.mips==b.mips && a.layers==b.layers && a.samples==b.samples,"Original whole-image copy descriptions differ");
            for(UINT mip=0;mip<a.mips;++mip){VkImageCopy r{};r.srcSubresource={source.image->full_range().aspectMask,mip,0,a.layers};
                r.dstSubresource={out.image->full_range().aspectMask,mip,0,b.layers};r.extent=source.image->extent(mip);
                if(r.srcSubresource.aspectMask==r.dstSubresource.aspectMask)commands_.copy(source.image,out.image,r);
                else commands_.copy_color_depth(source.image,out.image,r);}
        }
    }
    out.visible_size=0;out.region=false;
    if(!custom)select_view(out,image_view_kind(destination.type),false,false);
    cache=out;set(destination,state,out);
    if(op.options&ResourceCopyOptions::SET_VIEWPORT)viewport_={0,0,float(d.Width),out.image_description.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE1D?0.0f:float(d.Height),0,1};
    if((op.options&ResourceCopyOptions::NO_VIEW_CACHE) || op.src.forbid_view_cache)cache.drop_view();
}
void GpuResourceRuntime::image_clear(GpuResourceBinding b,ClearViewCommand& op) {
    if(b.view==View::ShaderResource)return;
    if(!b.image_view){View kind=View::None;
        if(op.clear_depth || op.clear_stencil)kind=View::DepthStencil;
        else if(op.clear_uav_uint)kind=View::UnorderedAccess;
        else if(b.bind_flags()&D3D11_BIND_DEPTH_STENCIL)kind=View::DepthStencil;
        else if(b.bind_flags()&D3D11_BIND_UNORDERED_ACCESS)kind=View::UnorderedAccess;
        else if(b.bind_flags()&D3D11_BIND_RENDER_TARGET)kind=View::RenderTarget;
        if(kind==View::None)return;select_view(b,kind);
    }
    auto range=b.image_view->range();
    if(b.view==View::DepthStencil){
        // The source's if/else priority chooses depth if both explicit flags are
        // true. With neither explicit flag it clears both available aspects.
        if(op.clear_depth)range.aspectMask&=VK_IMAGE_ASPECT_DEPTH_BIT;
        else if(op.clear_stencil)range.aspectMask&=VK_IMAGE_ASPECT_STENCIL_BIT;
        // D3D11 clamps depth to [0,1]; stencil-only clears ignore it entirely.
        if(range.aspectMask)commands_.clear(b.image,range,VkClearDepthStencilValue{std::clamp(op.dsv_depth,0.0f,1.0f),op.dsv_stencil});return;
    }
    require(b.view==View::UnorderedAccess || b.view==View::RenderTarget,"Image clear view type is unavailable");
    const auto format=source_image_format(b.view_format);VkClearColorValue value{};VkFormat clear_format=format.format;
    const bool integer_uav=b.view==View::UnorderedAccess && (op.clear_uav_uint || !original_float_clear(b.view_format));
    const bool integer_rtv=b.view==View::RenderTarget && !original_float_clear(b.view_format);
    if(integer_uav || integer_rtv){
        // D3D integer UAV clear writes the low N bits per channel without any
        // format conversion. Clear a size-compatible UINT image with packed bits,
        // then bit-copy its selected subresources into the original allocation.
        UINT bits=0;for(UINT i=0;i<4;++i)bits=std::max(bits,format.offsets[i]+format.bits[i]);
        require(bits && bits<=128 && bits%8==0,"Image integer clear format has no packed-color adapter");
        for(UINT i=0;i<4;++i){const auto channel=integer_uav?op.uval[i]:rtv_integer(op.fval[i],format.bits[i],format.signed_integer);
            for(UINT n=0;n<format.bits[i];++n)if(channel&(1u<<n)){
            const UINT bit=format.offsets[i]+n;value.uint32[bit/32]|=1u<<(bit%32);}
        }
        switch(bits){case 8:clear_format=VK_FORMAT_R8_UINT;break;case 16:clear_format=VK_FORMAT_R16_UINT;break;
        case 32:clear_format=VK_FORMAT_R32_UINT;break;case 64:clear_format=VK_FORMAT_R32G32_UINT;break;
        case 96:clear_format=VK_FORMAT_R32G32B32_UINT;break;case 128:clear_format=VK_FORMAT_R32G32B32A32_UINT;break;
        default:throw std::runtime_error("Image integer clear width needs a packed-color adapter");}
    }else{
        memcpy(value.float32,op.fval,sizeof(value.float32));
    }
    if(clear_format==b.image->description().format){commands_.clear(b.image,range,value);return;}
    auto desc=b.image->description();desc.format=clear_format;desc.usage=0;desc.flags=0;
    const std::array<UINT,10> key={UINT(desc.type),UINT(desc.format),desc.extent.width,desc.extent.height,desc.extent.depth,
        desc.mips,desc.layers,UINT(desc.samples),desc.usage,desc.flags};auto& scratch=clear_images_[key];if(!scratch)scratch=commands_.create_image(desc);
    commands_.clear(scratch,range,value);
    for(UINT i=0;i<range.levelCount;++i){VkImageCopy r{};r.srcSubresource=r.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,range.baseMipLevel+i,range.baseArrayLayer,range.layerCount};
        r.extent=b.image->extent(r.srcSubresource.mipLevel);commands_.copy(scratch,b.image,r);}
}
}
