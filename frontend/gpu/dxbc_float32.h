#pragma once
#include <cstdint>
#include <vector>

namespace efmi::gpu {
// Preserve the native fractional range and, when requested, flush float32
// arithmetic operands/results with integer bit operations. Pure data moves
// retain their bits. Unsupported arithmetic shapes refuse atomically.
void lower_dxbc_float32(std::vector<uint32_t>&,bool software_flush);
}
