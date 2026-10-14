#pragma once
#include "CommandList.h"
#include "../gpu/hlsl_compiler.h"
#include <map>
#include <stdexcept>

namespace efmi::frontend {
struct SourceVertexElement {
    std::string semantic;
    uint32_t index=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    uint32_t slot=0,offset=D3D11_APPEND_ALIGNED_ELEMENT;
    D3D11_INPUT_CLASSIFICATION classification=D3D11_INPUT_PER_VERTEX_DATA;
    uint32_t step=0;
};
// Only proven native creation failures use this type. Unsupported host/device
// execution must not silently masquerade as CreateInputLayout failure.
struct InvalidInputLayout : std::runtime_error {using std::runtime_error::runtime_error;};
class SourceInputLayout {
public:
    SourceInputLayout(std::vector<SourceVertexElement>,std::vector<gpu::ShaderSignature> creation_signature);
    const std::vector<SourceVertexElement>& elements() const {return elements_;}
    const std::vector<gpu::ShaderSignature>& signature() const {return signature_;}
    const std::map<uint32_t,SourceVertexElement>& registers() const {return registers_;}
    std::vector<D3D11_INPUT_ELEMENT_DESC> descriptions() const;
private:
    std::vector<SourceVertexElement> elements_;
    std::vector<gpu::ShaderSignature> signature_;
    std::map<uint32_t,SourceVertexElement> registers_;
};
using InputLayout=std::shared_ptr<const SourceInputLayout>;
std::vector<SourceVertexElement> copy_vertex_elements(const std::vector<D3D11_INPUT_ELEMENT_DESC>&);
std::string input_semantic(const std::string&,uint32_t index);
std::string input_semantic(const std::string& indexed);
VkFormat vertex_format(DXGI_FORMAT);
}
