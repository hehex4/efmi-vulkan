#pragma once
#include "hlsl_compiler.h"

namespace efmi::gpu {
// The source input assembler fetches first, then the shader reads the register
// bits in its declared type. alpha_one handles the native BGRX unused byte.
struct VertexInputConversion {uint32_t location=0,scalar=0;bool alpha_one=false;};
HlslProgram lower_vertex_inputs(const HlslProgram&,const std::vector<VertexInputConversion>&);
}
