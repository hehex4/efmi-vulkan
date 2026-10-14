#pragma once
#include "hlsl_compiler.h"

namespace efmi::gpu {
// Link compiler-produced graphics programs by original HLSL semantics. The
// returned copies are in pipeline order; input programs remain immutable.
// This is executable IO relocation, not pipeline creation or drawing. Actual
// device limits/features and independent SPIR-V validation belong to the host.
// maintenance4 must describe logical-device enablement, not physical support.
std::vector<HlslProgram> link_graphics(const std::vector<HlslProgram>&,bool maintenance4=false);
}
