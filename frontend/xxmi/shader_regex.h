#pragma once
#include "shader_state.h"
#include <map>

namespace efmi::frontend {
class DrawPlanAdapter;
// Generation-local native ShaderRegex command links. Assembly-dependent groups
// remain explicit until their source bytecode/translation adapter is supplied.
class SourceShaderRegex {
public:
    void initialize(DrawPlanAdapter&);
    void preflight(const SourceShaderState&,unsigned stage_mask,bool require_known) const;
    void apply(const SourceShaderState&,unsigned stage_mask,DrawPlanAdapter&);
private:
    std::map<uint64_t,std::string> linked_;
    unsigned relevant_stages_=0;
    bool initialized_=false;
};
}
