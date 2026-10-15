#pragma once
#include "hlsl_compiler.h"
#include <map>

namespace efmi::gpu {
struct DxbcBufferContract {ShaderBufferKind kind;uint32_t stride;};
using DxbcBufferContracts=std::map<std::pair<char,uint32_t>,DxbcBufferContract>;
// Supplement the native scan API, which omits structured strides and the
// distinction between typed uint and structured uint buffers. Uses the original
// token definitions and requires a bounded SM4.x/5.0 declaration encoding.
DxbcBufferContracts dxbc_buffer_contracts(const std::vector<uint8_t>&);
}
