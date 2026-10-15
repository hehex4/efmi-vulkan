#pragma once
#include "shader_state.h"
#include <map>
#include <vector>

namespace efmi::frontend {
class DrawPlanAdapter;
struct SourceShaderPatch {
    uint64_t hash=0;unsigned stage=0;
    std::shared_ptr<const SourceShaderCode> original;
    std::string assembly;
    std::vector<uint8_t> bytecode;
    std::vector<std::string> notices;
    std::wstring tagline;
};
// Source matching is staged before publication. Patches can be analysed offline
// but cannot acquire live command links without an executable translation path.
class SourceShaderRegex {
public:
    struct Prepared {
        std::vector<SourceShaderPatch> patches;
    private:
        friend class SourceShaderRegex;
        struct Entry;
        const SourceShaderRegex* owner=nullptr;
        bool requires_translation=false;
        std::vector<std::shared_ptr<Entry>> entries;
    };
    using Preparation=std::unique_ptr<Prepared>;
    void initialize(DrawPlanAdapter&);
    Preparation prepare(const SourceShaderState&,unsigned stage_mask,bool require_known,bool analyse_patches=false) const;
    void apply(Preparation,DrawPlanAdapter&);
private:
    struct Identity {std::string model;std::shared_ptr<const SourceShaderCode> code;};
    std::map<uint64_t,Identity> linked_;
    size_t retained_bytes_=0;
    unsigned relevant_stages_=0;
    bool initialized_=false;
};
}
