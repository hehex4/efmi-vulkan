#pragma once
#include "vertex_plan.h"
#include <array>
#include <filesystem>

namespace efmi::frontend {
// Whole-corpus generated metadata. It owns no Vulkan objects or character data.
// Read once while loading a generation; lookup does no IO or GPU work. A caller
// retains the returned profile with its recorded variants through completion.
class ShaderInputIndex {
public:
    bool load(const std::filesystem::path&, std::string& reason);
    bool decode(const std::vector<uint8_t>&, std::string& reason);
    bool lookup(const std::vector<uint32_t>& code, VertexProfile&, std::string& reason) const;
    size_t size() const { return entries_.size(); }
private:
    struct Entry { uint32_t bytes=0; std::vector<VertexSemantic> semantics; };
    std::map<std::array<uint8_t,32>,Entry> entries_;
};
// A different proof from input semantics: the original DXBC model is invariant
// across every paired occurrence of these exact SPIR-V bytes. No DXBC hash or
// declaration is inferred from this deliberately smaller evidence contract.
class ShaderModelIndex {
public:
    bool load(const std::filesystem::path&,std::string& reason);
    bool decode(const std::vector<uint8_t>&,std::string& reason);
    bool lookup(const std::vector<uint32_t>&,std::string& model) const;
    size_t size() const {return entries_.size();}
private:
    struct Entry {uint32_t bytes;std::string model;};
    std::map<std::array<uint8_t,32>,Entry> entries_;
};
}
