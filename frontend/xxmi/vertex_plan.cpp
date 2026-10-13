#include "vertex_plan.h"
#include "CommandList.h"
#include <algorithm>
#include <cstring>
#include <set>

bool FrontendApplyInputLayoutOverrides(std::vector<D3D11_INPUT_ELEMENT_DESC>&,CommandListState&);
namespace efmi::frontend {
namespace {
struct Format {DXGI_FORMAT dx;VkFormat vk;};
const Format formats[]={
    {DXGI_FORMAT_R32G32B32A32_FLOAT,VK_FORMAT_R32G32B32A32_SFLOAT},
    {DXGI_FORMAT_R32G32B32_FLOAT,VK_FORMAT_R32G32B32_SFLOAT},
    {DXGI_FORMAT_R32G32_FLOAT,VK_FORMAT_R32G32_SFLOAT},{DXGI_FORMAT_R32_FLOAT,VK_FORMAT_R32_SFLOAT},
    {DXGI_FORMAT_R16G16B16A16_UNORM,VK_FORMAT_R16G16B16A16_UNORM},
    {DXGI_FORMAT_R16G16B16A16_UINT,VK_FORMAT_R16G16B16A16_UINT},
    {DXGI_FORMAT_R16G16B16A16_FLOAT,VK_FORMAT_R16G16B16A16_SFLOAT},
    {DXGI_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM},
    {DXGI_FORMAT_R8G8B8A8_UINT,VK_FORMAT_R8G8B8A8_UINT},
    {DXGI_FORMAT_R8G8B8A8_SNORM,VK_FORMAT_R8G8B8A8_SNORM},
};
DXGI_FORMAT dx_format(VkFormat value){for(auto f:formats)if(f.vk==value)return f.dx;return DXGI_FORMAT_UNKNOWN;}
VkFormat vk_format(uint32_t value){for(auto f:formats)if(f.dx==value)return f.vk;return VK_FORMAT_UNDEFINED;}
bool name_valid(const std::string& name){
    if(name.empty() || name.size()>128)return false;
    for(unsigned char c:name)if(c<33 || c>126)return false;
    return true;
}
InputLayoutElementOverride original_edit(const assets::LayoutEdit& edit)
{
    InputLayoutElementOverride out;
    out.match={edit.match.semantic_name,edit.match.semantic_index,edit.match.input_slot,DXGI_FORMAT(edit.match.format),
               edit.match.format_byte_size,edit.match.aligned_byte_offset};
    out.replace={edit.replace.semantic_name,edit.replace.semantic_index,DXGI_FORMAT(edit.replace.format),edit.replace.aligned_byte_offset};
    return out;
}
}
bool VertexProfile::prepare(const std::vector<uint32_t>& vertex,const std::vector<VertexSemantic>& source,std::string& reason)
{
    reason.clear();
    if(vertex.size()<5 || vertex.size()>(1u<<22) || source.size()>256){reason="Vertex profile budget exceeded";return false;}
    VertexProfile next;next.code_=vertex;std::set<std::pair<std::string,uint32_t>> names;
    for(const auto& semantic:source){
        if(semantic.location>=256 || !name_valid(semantic.name) || semantic.index>=256 ||
           !next.semantics_.emplace(semantic.location,semantic).second || !names.emplace(semantic.name,semantic.index).second){
            reason="Duplicate or invalid vertex semantic correspondence";return false;
        }
    }
    *this=std::move(next);return true;
}
bool VertexProfile::resolve(const assets::FileDraws& input,const std::vector<uint32_t>& code,
                            const efvk::VertexInputRecipe& source,VertexPlan& output,std::string& reason) const
{
    reason.clear();auto refuse=[&](const char* text){reason=text;return false;};
    if(code_.empty() || code_!=code)return refuse("Vertex profile does not match the exact vertex bytecode");
    if(input.draws.size()>256 || source.bindings.size()>32 || source.attributes.size()>256)
        return refuse("Vertex declaration transaction budget exceeded");
    efvk::VertexInputRecipe validated;
    if(!efvk::patch_vertex_streams(source,{},validated,reason))return false;
    for(const auto& binding:source.bindings)if(binding.binding>=32 || binding.stride>2048 ||
        (binding.inputRate!=VK_VERTEX_INPUT_RATE_VERTEX && binding.inputRate!=VK_VERTEX_INPUT_RATE_INSTANCE))
        return refuse("Vertex source binding is unsupported");
    std::vector<D3D11_INPUT_ELEMENT_DESC> original;
    for(const auto& attribute:source.attributes){
        const auto semantic=semantics_.find(attribute.location);const auto format=dx_format(attribute.format);
        if(semantic==semantics_.end() || !format || attribute.binding>=32 || attribute.offset>2048)
            return refuse("Vertex attribute has no supported semantic/format correspondence");
        const auto binding=std::find_if(source.bindings.begin(),source.bindings.end(),[&](const auto& b){return b.binding==attribute.binding;});
        if(binding==source.bindings.end())
            return refuse("Vertex source binding is unsupported");
        const auto& sem=semantic->second;
        original.push_back({sem.name.c_str(),sem.index,format,attribute.binding,attribute.offset,
                            binding->inputRate==VK_VERTEX_INPUT_RATE_VERTEX?D3D11_INPUT_PER_VERTEX_DATA:D3D11_INPUT_PER_INSTANCE_DATA,
                            binding->inputRate==VK_VERTEX_INPUT_RATE_VERTEX?0u:1u});
    }
    VertexPlan next;next.draws.reserve(input.draws.size());
    for(const auto& draw:input.draws){
        if(draw.layout.size()>256)return refuse("Vertex layout edit budget exceeded");
        std::vector<InputLayoutElementOverride> edits;edits.reserve(draw.layout.size());
        for(const auto& edit:draw.layout){
            if((!edit.match.semantic_name.empty() && !name_valid(edit.match.semantic_name)) ||
               (!edit.replace.semantic_name.empty() && !name_valid(edit.replace.semantic_name)) ||
               (edit.match.format && !vk_format(edit.match.format)) || (edit.replace.format && !vk_format(edit.replace.format)) ||
               (edit.replace.aligned_byte_offset!=UINT32_MAX && edit.replace.aligned_byte_offset>2048))
                return refuse("Vertex layout edit exceeds supported fields");
            edits.push_back(original_edit(edit));
        }
        CommandListState state;for(auto& edit:edits)state.input_layout_overrides.push_back(&edit);
        auto changed=original;FrontendApplyInputLayoutOverrides(changed,state);
        std::vector<efvk::VertexStreamPatch> patches;
        for(uint32_t slot=0;slot<draw.vertex.size();++slot){
            const auto& file=draw.vertex[slot];
            auto binding=std::find_if(source.bindings.begin(),source.bindings.end(),[&](const auto& b){return b.binding==slot;});
            if(file.source==assets::BindingSource::Application){
                for(size_t i=0;i<changed.size();++i)if(original[i].InputSlot==slot &&
                    (changed[i].Format!=original[i].Format || changed[i].AlignedByteOffset!=original[i].AlignedByteOffset))
                    return refuse("Layout edit on an inherited application stream requires another adapter");
                continue;
            }
            if(file.source!=assets::BindingSource::File || !file.resource || file.resource->usage!=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT ||
               !file.resource->stride || binding==source.bindings.end())return refuse("Replaced vertex stream requires a declared file and source binding");
            efvk::VertexStreamPatch patch{slot,binding->stride,file.resource->stride,{}};
            for(size_t i=0;i<changed.size();++i){
                const auto& before=original[i];const auto& after=changed[i];if(before.InputSlot!=slot)continue;
                if(after.SemanticIndex!=before.SemanticIndex || strcmp(after.SemanticName,before.SemanticName))
                    return refuse("Changing input semantics requires shader relinking");
                const auto format=vk_format(after.Format);if(!format)return refuse("Changed vertex format is unsupported");
                efvk::VertexElementPatch element{before.AlignedByteOffset,source.attributes[i].format,after.AlignedByteOffset,format};
                const auto previous=std::find_if(patch.elements.begin(),patch.elements.end(),[&](const auto& e){return e.source_offset==element.source_offset && e.source_format==element.source_format;});
                if(previous!=patch.elements.end()){
                    if(previous->offset!=element.offset || previous->format!=element.format)return refuse("Aliased vertex inputs request conflicting replacements");
                }else patch.elements.push_back(element);
            }
            patches.push_back(std::move(patch));
        }
        // Even semantic edits on preserved streams must not disappear silently.
        for(size_t i=0;i<changed.size();++i)if(changed[i].SemanticIndex!=original[i].SemanticIndex ||
            strcmp(changed[i].SemanticName,original[i].SemanticName))return refuse("Changing input semantics requires shader relinking");
        efvk::VertexInputRecipe replacement;
        if(!efvk::patch_vertex_streams(source,patches,replacement,reason))return false;
        next.draws.push_back(std::move(replacement));
    }
    output=std::move(next);return true;
}
}
