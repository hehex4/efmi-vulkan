#include "resource_plan.h"
#include <stdexcept>

namespace efmi::frontend {
namespace {
std::shared_ptr<const assets::FileResource> describe(const CustomResource& r,unsigned role,std::wstring& reason)
{
    auto refuse=[&](const wchar_t* text)->std::shared_ptr<const assets::FileResource>{reason=text;return {};};
    if(r.filename.empty())return refuse(L"Binding needs a file-backed resource adapter");
    if(r.resource || r.view || r.device || r.substantiated || r.pool || r.pool_index>=0 || r.offset ||
       r.initial_data || r.initial_data_size)return refuse(L"Mutable, pooled or initialized resource requires its native adapter");
    const std::filesystem::path path(r.filename);
    if(!path.is_absolute() || r.filename.size()>32760 || r.filename.find(L'\0')!=std::wstring::npos || r.name.size()>4096)
        return refuse(L"Resource path or name outside owned declaration limits");
    if(r.override_misc_flags!=ResourceMiscFlags::INVALID || r.misc_flags ||
       r.override_width!=-1 || r.override_height!=-1 || r.override_depth!=-1 || r.override_mips!=-1 ||
       r.override_array!=-1 || r.override_msaa!=-1 || r.override_msaa_quality!=-1 ||
       r.override_byte_width!=-1 || r.width_multiply!=1.0f || r.height_multiply!=1.0f)
        return refuse(L"Resource transformation is not supported by immutable file upload");
    auto file=std::make_shared<assets::FileResource>();file->name=r.name;file->path=path.lexically_normal();
    if(role==2){
        if((r.override_type!=CustomResourceType::INVALID && r.override_type!=CustomResourceType::TEXTURE2D) ||
           _wcsicmp(path.extension().c_str(),L".dds") || r.override_stride!=-1 ||
           r.override_format!=DXGI_FORMAT(-1) || r.override_color_space!=CustomColorSpace::DEFAULT ||
           (r.override_bind_flags!=CustomResourceBindFlags::INVALID && r.override_bind_flags!=CustomResourceBindFlags::SHADER_RESOURCE))
            return refuse(L"Pixel file binding requires an unmodified DDS image");
        return file;
    }
    if(r.override_type!=CustomResourceType::BUFFER)return refuse(L"Mesh file binding requires a plain Buffer declaration");
    const auto flags=static_cast<unsigned>(r.override_bind_flags);
    const auto required=role==0?D3D11_BIND_VERTEX_BUFFER:D3D11_BIND_INDEX_BUFFER;
    if(flags && ((flags&~(D3D11_BIND_VERTEX_BUFFER|D3D11_BIND_INDEX_BUFFER)) || !(flags&required)))
        return refuse(L"Explicit buffer bind flags do not support this binding");
    if(role==0){
        if(r.override_stride<=0 || r.override_stride>2048)return refuse(L"Vertex stride is not a supported explicit byte stride");
        if(r.override_format!=DXGI_FORMAT(-1) && r.override_format!=DXGI_FORMAT_UNKNOWN)
            return refuse(L"Typed vertex resource format requires a declaration adapter");
        file->usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;file->stride=r.override_stride;return file;
    }
    if(r.override_format==DXGI_FORMAT_R32_UINT){file->index_type=VK_INDEX_TYPE_UINT32;file->stride=4;}
    else if(r.override_format==DXGI_FORMAT_R16_UINT){file->index_type=VK_INDEX_TYPE_UINT16;file->stride=2;}
    else return refuse(L"Index binding requires R16_UINT or R32_UINT");
    if(r.override_stride!=-1 && r.override_stride!=static_cast<int>(file->stride))
        return refuse(L"Index stride conflicts with its declared format");
    file->usage=VK_BUFFER_USAGE_INDEX_BUFFER_BIT;return file;
}
assets::LayoutEdit copy_layout(const InputLayoutElementOverride& source)
{
    assets::LayoutEdit out;
    out.match={source.match.semantic_name,source.match.semantic_index,source.match.input_slot,
               static_cast<uint32_t>(source.match.format),source.match.format_byte_size,source.match.aligned_byte_offset};
    out.replace={source.replace.semantic_name,source.replace.semantic_index,static_cast<uint32_t>(source.replace.format),source.replace.aligned_byte_offset};
    return out;
}
}
FilePlanCompiler::FilePlanCompiler(const CustomResources& resources)
{
    if(resources.size()>4096)throw std::invalid_argument("Frontend resource declaration budget exceeded");
    for(const auto& pair:resources){
        Declaration d;
        for(unsigned role=0;role<3;++role)d.bindings[role]=describe(pair.second,role,d.reasons[role]);
        declarations_.emplace(&pair.second,std::move(d));
    }
}
bool FilePlanCompiler::compile(const DrawPlan& source,assets::FilePlan& output,std::wstring& reason) const
{
    reason.clear();
    auto refuse=[&](const wchar_t* text){reason=text;return false;};
    if(source.items.size()>4096)return refuse(L"Resource plan operation budget exceeded");
    assets::FilePlan next;next.skip_original=source.skip_original;next.pre_items=source.pre_items;size_t draws=0;
    for(const auto& item:source.items){
        assets::FilePlanItem own;own.slot=item.slot;own.application_slot=item.application_slot;
        if(item.owned){
            if(item.operation!=PlanOperation::BindVertex || item.resource)return refuse(L"Owned buffer outside a vertex binding");
            const auto& o=*item.owned;
            own.owned=std::make_shared<const assets::OwnedBuffer>(assets::OwnedBuffer{o.buffer,o.offset,o.size,o.stride,o.keep});
        }
        switch(item.operation){
        case PlanOperation::BindVertex: own.operation=assets::FileOperation::BindVertex;break;
        case PlanOperation::BindIndex: own.operation=assets::FileOperation::BindIndex;break;
        case PlanOperation::BindPixelResource: own.operation=assets::FileOperation::BindPixelResource;break;
        case PlanOperation::DrawIndexed:
            if(!item.index_count || !item.instance_count || ++draws>256 || item.layout.size()>256)
                return refuse(L"Invalid indexed draw in resource plan");
            own.operation=assets::FileOperation::DrawIndexed;
            own.index_count=item.index_count;own.first_index=item.first_index;own.vertex_offset=item.vertex_offset;
            own.instance_count=item.instance_count;own.first_instance=item.first_instance;
            own.palette=item.palette;
            for(const auto& layout:item.layout)own.layout.push_back(copy_layout(layout));
            next.items.push_back(std::move(own));continue;
        case PlanOperation::External:
            if(item.external.empty() || item.external.size()>4096 || !item.phase_mask || item.phase_mask>3)
                return refuse(L"Invalid native boundary in resource plan");
            own.operation=assets::FileOperation::External;own.external=item.external;own.phase_mask=item.phase_mask;
            next.items.push_back(std::move(own));continue;
        default: return refuse(L"Unknown resource plan operation");
        }
        const unsigned role=item.operation==PlanOperation::BindVertex?0:item.operation==PlanOperation::BindIndex?1:2;
        if(item.slot>=(role==0?32u:role==1?1u:128u))return refuse(L"Resource binding slot out of range");
        if(item.resource){
            auto found=declarations_.find(item.resource);
            if(found==declarations_.end())return refuse(L"Resource belongs to another frontend generation");
            own.resource=found->second.bindings[role];
            if(!own.resource){reason=found->second.reasons[role];return false;}
        }
        next.items.push_back(std::move(own));
    }
    output=std::move(next);return true;
}
}
