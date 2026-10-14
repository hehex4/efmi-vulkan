#include "input_layout.h"
#include "image_formats.h"
#include "../gpu/image_formats.h"
#include <algorithm>
#include <array>
#include <set>

namespace efmi::frontend {
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void valid(bool value,const char* why){if(!value)throw InvalidInputLayout(why);}
std::string uppercase(std::string value){require(!value.empty() && value.size()<=1024,"Input semantic name exceeds bounded source domain");
    for(auto& c:value){require(c>32 && static_cast<unsigned char>(c)<127,"Non-ASCII input semantic requires an adapter");if(c>='a' && c<='z')c-=32;}return value;}
uint32_t source_bytes(DXGI_FORMAT format){
    // Native feature-level11_0 IA formats, independently checked on system WARP.
    const auto f=uint32_t(format);
    if(f>=2 && f<=4)return 16;if(f>=6 && f<=8)return 12;if(f>=10 && f<=14)return 8;if(f>=16 && f<=18)return 8;
    if((f>=24 && f<=28) || (f>=30 && f<=32) || (f>=34 && f<=38) || (f>=41 && f<=43) || f==87 || f==88){
        valid(f!=27,"Typeless source vertex format is invalid");return 4;}
    if((f>=49 && f<=52) || f==54 || (f>=56 && f<=59) || f==85 || f==86 || f==115)return 2;
    if(f>=61 && f<=64)return 1;
    if(f>115)throw std::runtime_error("New source vertex format needs a creation adapter");
    throw InvalidInputLayout("Source format cannot be used by the input assembler");
}
}
std::string input_semantic(const std::string& name,uint32_t index){return uppercase(name)+":"+std::to_string(index);}
std::string input_semantic(const std::string& indexed){
    const auto last=indexed.find_last_not_of("0123456789");require(last!=std::string::npos,"Input semantic lacks a name");
    const auto digits=indexed.substr(last+1);const uint64_t value=digits.empty()?0:std::stoull(digits);
    require(value<=UINT32_MAX,"Input semantic index exceeds source range");return input_semantic(indexed.substr(0,last+1),uint32_t(value));
}
SourceInputLayout::SourceInputLayout(std::vector<SourceVertexElement> elements,std::vector<gpu::ShaderSignature> signature):
    elements_(std::move(elements)),signature_(std::move(signature)){
    valid(elements_.size()<=32,"Source input layout has too many elements");require(signature_.size()<=128,"Input signature budget exceeded");
    std::array<uint64_t,32> ends{};std::map<uint32_t,std::pair<D3D11_INPUT_CLASSIFICATION,uint32_t>> slots;
    std::map<std::string,SourceVertexElement> names;
    for(auto& element:elements_){element.semantic=uppercase(element.semantic);const auto bytes=source_bytes(element.format),alignment=std::min(4u,bytes);
        valid(element.slot<32,"Source input slot exceeds feature-level11_0 limit");
        valid(element.classification==D3D11_INPUT_PER_VERTEX_DATA || element.classification==D3D11_INPUT_PER_INSTANCE_DATA,"Source input classification is invalid");
        valid(element.classification!=D3D11_INPUT_PER_VERTEX_DATA || element.step==0,"Per-vertex source data cannot have an instance step rate");
        const auto rate=std::make_pair(element.classification,element.step);const auto inserted=slots.emplace(element.slot,rate);
        valid(inserted.second || inserted.first->second==rate,"Source elements in one slot have different input rates");
        auto resolved=element;const uint64_t offset=element.offset==D3D11_APPEND_ALIGNED_ELEMENT?(ends[element.slot]+alignment-1)&~uint64_t(alignment-1):element.offset;
        require(offset<=UINT32_MAX,"Overflowing native APPEND offset needs an adapter");valid(offset%alignment==0,"Source element offset is misaligned");
        resolved.offset=uint32_t(offset);ends[element.slot]=offset+bytes;
        valid(names.emplace(input_semantic(element.semantic,element.index),std::move(resolved)).second,"Duplicate source input semantic");
    }
    for(const auto& input:signature_)if(!input.system_value){const auto found=names.find(input_semantic(input.semantic,input.index));
        // Native creation requires even an unused declared signature element.
        valid(found!=names.end(),"Source input layout lacks a required signature element");
        require(input.register_index<32 && input.mask && input.mask<=15 && (input.mask&(input.mask+1))==0,
            "Packed or noncontiguous native input signature needs lowering");
        require(registers_.emplace(input.register_index,found->second).second,"Packed native input registers need lowering");
    }
}
std::vector<D3D11_INPUT_ELEMENT_DESC> SourceInputLayout::descriptions() const {
    std::vector<D3D11_INPUT_ELEMENT_DESC> result;result.reserve(elements_.size());for(const auto& e:elements_)
        result.push_back({e.semantic.c_str(),e.index,e.format,e.slot,e.offset,e.classification,e.step});return result;
}
std::vector<SourceVertexElement> copy_vertex_elements(const std::vector<D3D11_INPUT_ELEMENT_DESC>& elements){
    require(elements.size()<=32,"Input element copy exceeds source budget");std::vector<SourceVertexElement> result;result.reserve(elements.size());
    for(const auto& e:elements){require(e.SemanticName!=nullptr,"Source input semantic is null");
        result.push_back({e.SemanticName,e.SemanticIndex,e.Format,e.InputSlot,e.AlignedByteOffset,e.InputSlotClass,e.InstanceDataStepRate});}return result;
}
VkFormat vertex_format(DXGI_FORMAT source){
    source_bytes(source);
    // Packed BGR and BGRX require a dedicated fetch/conversion reference first.
    require(source!=DXGI_FORMAT_B5G6R5_UNORM && source!=DXGI_FORMAT_B5G5R5A1_UNORM && source!=DXGI_FORMAT_B4G4R4A4_UNORM && source!=DXGI_FORMAT_B8G8R8X8_UNORM,
        "Packed BGR/BGRX vertex fetch requires another format adapter");
    return source_image_format(source).format;
}
}
