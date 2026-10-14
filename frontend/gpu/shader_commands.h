#pragma once
#include "resource_commands.h"
#include "hlsl_compiler.h"
#include <array>

namespace efmi::gpu {
struct ShaderHost {
    PFN_vkGetDeviceProcAddr get_device_proc=nullptr;
    // Actual properties of the ResourceCommands physical device.
    VkPhysicalDeviceProperties properties{};
    std::filesystem::path validator;
    // Actual logical-device feature enablement, including required extension
    // enablement on API versions where these features are not core.
    bool scalar_block_layout=false,uniform_buffer_standard_layout=false;
    std::shared_ptr<assets::AllocationBudget> objects=std::make_shared<assets::AllocationBudget>(8192);
    // Share across all owned shader generations on this device, within the
    // sampler capacity reserved by the host. Other application samplers count
    // against the device limit too; physical support is not free headroom.
    std::shared_ptr<assets::AllocationBudget> sampler_objects=std::make_shared<assets::AllocationBudget>(1024);
};
class ComputePipeline {
public:
    struct Impl;
    explicit ComputePipeline(std::shared_ptr<Impl>);
    ~ComputePipeline();
    const HlslProgram& program() const;
private:
    friend class ShaderCommands;
    std::shared_ptr<Impl> impl_;
};
using Compute=std::shared_ptr<ComputePipeline>;
class ShaderSampler {
public:
    struct Impl;
    explicit ShaderSampler(std::shared_ptr<Impl>);
    ~ShaderSampler();
private:
    friend class ShaderCommands;
    std::shared_ptr<Impl> impl_;
};
using Sampler=std::shared_ptr<ShaderSampler>;
struct ShaderDescriptor {
    uint32_t binding=0,element=0;
    Buffer buffer;
    VkDeviceSize offset=0,size=0;
    VkFormat format=VK_FORMAT_UNDEFINED; // Typed buffer view format.
    ImageView image;
    Sampler sampler;
};
// Owned, outside-render-pass compute execution in exactly the resource queue
// domain. Each recorded dispatch owns immutable descriptors through its fence.
// There is no implicit submit/wait, game import or source-slot inference here.
class ShaderCommands {
public:
    ShaderCommands(ResourceCommands&,ShaderHost);
    ~ShaderCommands();
    Compute compile(const HlslProgram&);
    Sampler sampler(const VkSamplerCreateInfo&);
    void dispatch(const Compute&,const std::vector<ShaderDescriptor>&,std::array<uint32_t,3> groups);
    // The GPU-produced counts must satisfy maxComputeWorkGroupCount, as for
    // Vulkan itself. There is no implicit CPU readback or count modification.
    void dispatch_indirect(const Compute&,const std::vector<ShaderDescriptor>&,const Buffer&,VkDeviceSize offset);
private:
    void record(const Compute&,const std::vector<ShaderDescriptor>&,std::array<uint32_t,3>,const Buffer&,VkDeviceSize);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
