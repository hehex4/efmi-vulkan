// Host-owned GPU readback for a hunting frame; independent of replacement-module lifetime.
#pragma once
#include <memory>
#include <vulkan/vulkan.h>

namespace efvk {
struct Device;
struct CmdBuf;
struct SubmitWork;
class GpuAnalysis;
void gpu_analysis_frame(Device*, const char* directory, uint32_t options);
void gpu_analysis_dispatch(Device*, CmdBuf*);
void gpu_analysis_pass(Device*, CmdBuf*);
void gpu_analysis_prepare(Device*, SubmitWork&);
void gpu_analysis_submitted(SubmitWork&, VkResult);
void gpu_analysis_stop(Device*);
}
