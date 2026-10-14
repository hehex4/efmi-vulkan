#include "shader_commands.h"
#include "image_formats.h"
#include <spirv_reflect.h>
#include <spirv-tools/libspirv.h>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace efmi::gpu {
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void checked(VkResult result,const char* why){if(result!=VK_SUCCESS)throw std::runtime_error(std::string(why)+": "+std::to_string(result));}
struct ShaderDevice {
    ResourceDevice resource;
    ShaderHost host;
    std::shared_ptr<const uint8_t> domain;
    HMODULE validator=nullptr;
#define VALIDATOR_FUNCS(X) X(spvContextCreate) X(spvContextDestroy) X(spvValidatorOptionsCreate) X(spvValidatorOptionsDestroy) \
    X(spvValidatorOptionsSetScalarBlockLayout) X(spvValidatorOptionsSetUniformBufferStandardLayout) X(spvValidateWithOptions) X(spvDiagnosticDestroy)
#define X(name) decltype(&::name) name=nullptr;
    VALIDATOR_FUNCS(X)
#undef X
    ~ShaderDevice(){if(validator)FreeLibrary(validator);}
    void validate(const HlslProgram& program){
        struct Validation {
            ShaderDevice& d;spv_context context=nullptr;spv_validator_options options=nullptr;spv_diagnostic diagnostic=nullptr;
            ~Validation(){if(diagnostic)d.spvDiagnosticDestroy(diagnostic);if(options)d.spvValidatorOptionsDestroy(options);if(context)d.spvContextDestroy(context);}
        } v{*this};
        v.context=spvContextCreate(program.vulkan_version>=VK_API_VERSION_1_2?SPV_ENV_VULKAN_1_2:SPV_ENV_VULKAN_1_1);
        v.options=spvValidatorOptionsCreate();require(v.context && v.options,"Cannot create shader validator");
        spvValidatorOptionsSetScalarBlockLayout(v.options,host.scalar_block_layout);
        spvValidatorOptionsSetUniformBufferStandardLayout(v.options,host.uniform_buffer_standard_layout);
        spv_const_binary_t binary{program.spirv.data(),program.spirv.size()};
        if(spvValidateWithOptions(v.context,v.options,&binary,&v.diagnostic)!=SPV_SUCCESS)
            throw std::runtime_error(std::string("Shader is invalid for enabled Vulkan layout features: ")+(v.diagnostic?v.diagnostic->error:"validation failed"));
    }
#define SHADER_FUNCS(X) \
    X(CreateShaderModule) X(DestroyShaderModule) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
    X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateComputePipelines) X(DestroyPipeline) \
    X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
    X(CreateBufferView) X(DestroyBufferView) X(CreateSampler) X(DestroySampler) \
    X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdDispatch)
#define X(name) PFN_vk##name name=nullptr;
    SHADER_FUNCS(X)
#undef X
};
struct ObjectLease {
    std::shared_ptr<assets::AllocationBudget> budget;
    explicit ObjectLease(const std::shared_ptr<ShaderDevice>& d):budget(d->host.objects){require(budget && budget->acquire(1),"Shader object budget exhausted");}
    ~ObjectLease(){budget->release(1);}
};
const ImageFormatInfo& format_info(VkFormat format){require(format>VK_FORMAT_UNDEFINED && uint32_t(format)<185,"Shader view format requires an extension adapter");return image_formats[uint32_t(format)];}
void capabilities(const HlslProgram& program,const ResourceDevice& device){
    const auto& f=device.enabled_features;
    for(auto cap:program.capabilities){bool supported=false;
        switch(cap){
        case SpvCapabilityMatrix:case SpvCapabilityShader:case SpvCapabilitySampled1D:case SpvCapabilityImage1D:
        case SpvCapabilitySampledBuffer:case SpvCapabilityImageBuffer:case SpvCapabilityImageQuery:
        case SpvCapabilityImageMSArray:case SpvCapabilityAtomicStorage:supported=true;break;
        case SpvCapabilityFloat64:supported=f.shaderFloat64;break;
        case SpvCapabilityInt64:supported=f.shaderInt64;break;
        case SpvCapabilityInt16:supported=f.shaderInt16;break;
        case SpvCapabilityImageGatherExtended:supported=f.shaderImageGatherExtended;break;
        case SpvCapabilityStorageImageMultisample:supported=f.shaderStorageImageMultisample;break;
        case SpvCapabilityStorageImageExtendedFormats:supported=f.shaderStorageImageExtendedFormats;break;
        case SpvCapabilityStorageImageReadWithoutFormat:supported=f.shaderStorageImageReadWithoutFormat;break;
        case SpvCapabilityStorageImageWriteWithoutFormat:supported=f.shaderStorageImageWriteWithoutFormat;break;
        case SpvCapabilityUniformBufferArrayDynamicIndexing:supported=f.shaderUniformBufferArrayDynamicIndexing;break;
        case SpvCapabilitySampledImageArrayDynamicIndexing:supported=f.shaderSampledImageArrayDynamicIndexing;break;
        case SpvCapabilityStorageBufferArrayDynamicIndexing:supported=f.shaderStorageBufferArrayDynamicIndexing;break;
        case SpvCapabilityStorageImageArrayDynamicIndexing:supported=f.shaderStorageImageArrayDynamicIndexing;break;
        case SpvCapabilityImageCubeArray:case SpvCapabilitySampledCubeArray:supported=f.imageCubeArray;break;
        default:break;
        }
        require(supported,"Shader capability is not enabled or needs another execution adapter");
    }
    // Workgroup storage needs its own size/layout proof before accepting shared
    // memory. Do not trust pipeline creation alone to enforce physical limits.
    for(size_t at=5;at<program.spirv.size();){const uint32_t n=program.spirv[at]>>16,op=program.spirv[at]&65535;
        require(n && n<=program.spirv.size()-at,"Invalid shader instruction boundary");
        if(op==SpvOpVariable && n>=4)require(program.spirv[at+3]!=SpvStorageClassWorkgroup,"Workgroup storage layout requires an execution adapter");
        at+=n;
    }
}
VkImageViewType view_type(const ShaderBinding& b){
    switch(b.image_dimension){
    case SpvDim1D:return b.image_array?VK_IMAGE_VIEW_TYPE_1D_ARRAY:VK_IMAGE_VIEW_TYPE_1D;
    case SpvDim2D:return b.image_array?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D;
    case SpvDim3D:require(!b.image_array,"Shader declares an unsupported array of volume images");return VK_IMAGE_VIEW_TYPE_3D;
    case SpvDimCube:return b.image_array?VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:VK_IMAGE_VIEW_TYPE_CUBE;
    default:throw std::runtime_error("Shader image dimension requires another adapter");
    }
}
}
struct ComputePipeline::Impl {
    std::shared_ptr<ShaderDevice> device;
    ObjectLease lease;
    HlslProgram source;
    VkShaderModule module=VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptors=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    std::map<VkDescriptorType,uint32_t> counts;
    explicit Impl(std::shared_ptr<ShaderDevice> d):device(std::move(d)),lease(device){}
    ~Impl(){const auto& d=*device;const auto dev=d.resource.device;
        if(pipeline)d.DestroyPipeline(dev,pipeline,nullptr);if(layout)d.DestroyPipelineLayout(dev,layout,nullptr);
        if(descriptors)d.DestroyDescriptorSetLayout(dev,descriptors,nullptr);if(module)d.DestroyShaderModule(dev,module,nullptr);}
};
ComputePipeline::ComputePipeline(std::shared_ptr<Impl> impl):impl_(std::move(impl)){}
ComputePipeline::~ComputePipeline()=default;
const HlslProgram& ComputePipeline::program() const {return impl_->source;}
struct ShaderSampler::Impl {
    std::shared_ptr<ShaderDevice> device;
    ObjectLease lease;
    VkSampler handle=VK_NULL_HANDLE;
    VkSamplerCreateInfo info{};
    explicit Impl(std::shared_ptr<ShaderDevice> d):device(std::move(d)),lease(device){}
    ~Impl(){if(handle)device->DestroySampler(device->resource.device,handle,nullptr);}
};
ShaderSampler::ShaderSampler(std::shared_ptr<Impl> impl):impl_(std::move(impl)){}
ShaderSampler::~ShaderSampler()=default;
struct ShaderCommands::Impl {
    ResourceCommands& commands;
    std::shared_ptr<ShaderDevice> device=std::make_shared<ShaderDevice>();
    explicit Impl(ResourceCommands& c):commands(c){}
};
ShaderCommands::ShaderCommands(ResourceCommands& commands,ShaderHost host):impl_(std::make_unique<Impl>(commands)){
    require(host.get_device_proc && host.objects && host.properties.apiVersion>=commands.device_.api_version &&
        commands.device_.api_version>=VK_API_VERSION_1_1 && (commands.device_.queue_flags&VK_QUEUE_COMPUTE_BIT),
        "Shaders require explicit compute queue/device capabilities");
    auto& d=*impl_->device;d.resource=commands.device_;d.host=std::move(host);d.domain=commands.domain_;
    require(d.host.validator.is_absolute(),"SPIR-V validator DLL path must be explicit and absolute");
    d.validator=LoadLibraryExW(d.host.validator.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    require(d.validator!=nullptr,"Cannot load configured SPIR-V validator");
#define X(name) d.name=reinterpret_cast<decltype(d.name)>(GetProcAddress(d.validator,#name));require(d.name!=nullptr,"Missing validator entry " #name);
    VALIDATOR_FUNCS(X)
#undef X
#define X(name) d.name=reinterpret_cast<PFN_vk##name>(d.host.get_device_proc(d.resource.device,"vk" #name));require(d.name!=nullptr,"Missing shader dispatch vk" #name);
    SHADER_FUNCS(X)
#undef X
}
ShaderCommands::~ShaderCommands()=default;
Compute ShaderCommands::compile(const HlslProgram& program){
    auto d=impl_->device;const auto& limits=d->host.properties.limits;
    require(program.stage==VK_SHADER_STAGE_COMPUTE_BIT && program.vulkan_version<=d->resource.api_version &&
        program.spirv.size()>=5 && program.spirv[0]==SpvMagicNumber,"Shader stage/target is not compatible with this compute domain");
    capabilities(program,d->resource);
    d->validate(program);
    uint64_t threads=1;for(unsigned i=0;i<3;++i){require(program.threads[i] && program.threads[i]<=limits.maxComputeWorkGroupSize[i],"Shader local size exceeds the device limit");threads*=program.threads[i];}
    require(threads<=limits.maxComputeWorkGroupInvocations,"Shader workgroup invocation count exceeds device limit");
    auto result=std::make_shared<ComputePipeline::Impl>(d);result->source=program;
    std::vector<VkDescriptorSetLayoutBinding> bindings;std::set<uint32_t> used;uint32_t total=0;
    for(const auto& b:program.bindings){require(b.count && b.count<=2048 && total<=2048-b.count && used.insert(b.binding).second,"Shader descriptor count/identity invalid");total+=b.count;
        switch(b.type){case VK_DESCRIPTOR_TYPE_SAMPLER:case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:break;
        default:throw std::runtime_error("Shader descriptor type needs another adapter");}
        result->counts[b.type]+=b.count;bindings.push_back({b.binding,b.type,b.count,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
    }
    auto count=[&](VkDescriptorType type){const auto it=result->counts.find(type);return it==result->counts.end()?0:it->second;};
    const auto sampled=count(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)+count(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
    const auto storage=count(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)+count(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
    require(total-count(VK_DESCRIPTOR_TYPE_SAMPLER)<=limits.maxPerStageResources &&
        sampled<=std::min(limits.maxPerStageDescriptorSampledImages,limits.maxDescriptorSetSampledImages) &&
        storage<=std::min(limits.maxPerStageDescriptorStorageImages,limits.maxDescriptorSetStorageImages) &&
        count(VK_DESCRIPTOR_TYPE_SAMPLER)<=std::min(limits.maxPerStageDescriptorSamplers,limits.maxDescriptorSetSamplers) &&
        count(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)<=std::min(limits.maxPerStageDescriptorUniformBuffers,limits.maxDescriptorSetUniformBuffers) &&
        count(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)<=std::min(limits.maxPerStageDescriptorStorageBuffers,limits.maxDescriptorSetStorageBuffers),"Shader descriptor usage exceeds device limits");
    const auto dev=d->resource.device;VkDescriptorSetLayoutCreateInfo descriptor={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptor.bindingCount=uint32_t(bindings.size());descriptor.pBindings=bindings.data();
    checked(d->CreateDescriptorSetLayout(dev,&descriptor,nullptr,&result->descriptors),"Create shader descriptor layout");
    VkPipelineLayoutCreateInfo layout={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&result->descriptors;
    checked(d->CreatePipelineLayout(dev,&layout,nullptr,&result->layout),"Create compute pipeline layout");
    VkShaderModuleCreateInfo module={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};module.codeSize=program.spirv.size()*4;module.pCode=program.spirv.data();
    checked(d->CreateShaderModule(dev,&module,nullptr,&result->module),"Create compute shader module");
    VkComputePipelineCreateInfo pipeline={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pipeline.layout=result->layout;
    pipeline.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=result->module;pipeline.stage.pName="main";
    checked(d->CreateComputePipelines(dev,VK_NULL_HANDLE,1,&pipeline,nullptr,&result->pipeline),"Create compute pipeline");
    return std::make_shared<ComputePipeline>(std::move(result));
}
Sampler ShaderCommands::sampler(const VkSamplerCreateInfo& info){
    const auto d=impl_->device;const auto& limits=d->host.properties.limits;
    require(info.sType==VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO && !info.pNext && !info.flags && !info.unnormalizedCoordinates,
        "Sampler extensions/unnormalized coordinates need another adapter");
    require((info.magFilter==VK_FILTER_NEAREST || info.magFilter==VK_FILTER_LINEAR) &&
        (info.minFilter==VK_FILTER_NEAREST || info.minFilter==VK_FILTER_LINEAR) &&
        (info.mipmapMode==VK_SAMPLER_MIPMAP_MODE_NEAREST || info.mipmapMode==VK_SAMPLER_MIPMAP_MODE_LINEAR),"Invalid core sampler filter");
    for(auto mode:{info.addressModeU,info.addressModeV,info.addressModeW})require(mode>=VK_SAMPLER_ADDRESS_MODE_REPEAT && mode<=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,"Sampler address mode needs an enabled extension");
    require(std::isfinite(info.mipLodBias) && std::abs(info.mipLodBias)<=limits.maxSamplerLodBias &&
        std::isfinite(info.minLod) && std::isfinite(info.maxLod) && info.minLod<=info.maxLod &&
        info.borderColor>=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK && info.borderColor<=VK_BORDER_COLOR_INT_OPAQUE_WHITE &&
        info.compareOp>=VK_COMPARE_OP_NEVER && info.compareOp<=VK_COMPARE_OP_ALWAYS,"Invalid core sampler parameters");
    require(!info.anisotropyEnable || (d->resource.enabled_features.samplerAnisotropy && std::isfinite(info.maxAnisotropy) &&
        info.maxAnisotropy>=1 && info.maxAnisotropy<=limits.maxSamplerAnisotropy),"Sampler anisotropy is not enabled or exceeds device limits");
    auto result=std::make_shared<ShaderSampler::Impl>(d);result->info=info;
    checked(d->CreateSampler(d->resource.device,&info,nullptr,&result->handle),"Create shader sampler");return std::make_shared<ShaderSampler>(std::move(result));
}
void ShaderCommands::dispatch(const Compute& pipeline,const std::vector<ShaderDescriptor>& descriptors,std::array<uint32_t,3> groups){
    auto& commands=impl_->commands;const auto d=impl_->device;const auto& limits=d->host.properties.limits;
    require(pipeline && pipeline->impl_ && pipeline->impl_->device->domain==d->domain,"Compute pipeline belongs to another execution domain");
    for(unsigned i=0;i<3;++i)require(groups[i]<=limits.maxComputeWorkGroupCount[i],"Dispatch group count exceeds device limits");
    // Zero dispatch still has already flushed INI parameters in the frontend,
    // but accesses no descriptors and records no GPU work.
    if(!groups[0] || !groups[1] || !groups[2])return;
    struct Invocation {
        std::shared_ptr<ComputePipeline::Impl> pipeline;
        ObjectLease lease;
        VkDescriptorPool pool=VK_NULL_HANDLE;
        std::vector<VkBufferView> views;
        std::vector<ShaderDescriptor> owners;
        Invocation(std::shared_ptr<ComputePipeline::Impl> p):pipeline(std::move(p)),lease(pipeline->device){}
        ~Invocation(){const auto& d=*pipeline->device;const auto dev=d.resource.device;
            if(pool)d.DestroyDescriptorPool(dev,pool,nullptr);for(auto view:views)d.DestroyBufferView(dev,view,nullptr);}
    };
    auto invocation=std::make_shared<Invocation>(pipeline->impl_);invocation->owners=descriptors;
    std::map<std::pair<uint32_t,uint32_t>,const ShaderDescriptor*> provided;
    for(const auto& descriptor:descriptors)require(provided.emplace(std::make_pair(descriptor.binding,descriptor.element),&descriptor).second,"Duplicate shader descriptor");
    size_t expected=0;for(const auto& b:pipeline->program().bindings)expected+=b.count;
    require(descriptors.size()==expected,"Shader descriptor count does not match reflection");
    std::vector<VkDescriptorBufferInfo> buffers;std::vector<VkDescriptorImageInfo> images;std::vector<VkWriteDescriptorSet> writes;
    buffers.reserve(expected);images.reserve(expected);writes.reserve(expected);invocation->views.reserve(expected);
    std::vector<ImageView> sampled_images;std::vector<std::shared_ptr<ShaderSampler::Impl>> samplers;
    for(const auto& binding:pipeline->program().bindings)for(uint32_t element=0;element<binding.count;++element){
        const auto found=provided.find({binding.binding,element});require(found!=provided.end(),"Missing shader descriptor array element");const auto& value=*found->second;
        VkWriteDescriptorSet write={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstBinding=binding.binding;write.dstArrayElement=element;write.descriptorCount=1;write.descriptorType=binding.type;
        if(binding.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || binding.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
           binding.type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || binding.type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER){
            require(value.buffer && !value.image && !value.sampler,"Shader buffer descriptor requires exactly one owned buffer");commands.validate(value.buffer,value.offset,value.size);
            const bool uniform=binding.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,storage=binding.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            if(uniform || storage){const auto usage=uniform?VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT:VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                const auto alignment=uniform?limits.minUniformBufferOffsetAlignment:limits.minStorageBufferOffsetAlignment;
                require((value.buffer->usage()&usage) && alignment && value.offset%alignment==0 &&
                    value.size<=(uniform?limits.maxUniformBufferRange:limits.maxStorageBufferRange) &&
                    (uniform?value.size>=binding.block_size:(!binding.stride || value.size>=binding.stride)),"Shader buffer range/usage/alignment invalid");
                buffers.push_back({value.buffer->buffer(),value.offset,value.size});write.pBufferInfo=&buffers.back();
            }else{const auto& format=format_info(value.format);const bool read=binding.type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
                require(format.aspects==VK_IMAGE_ASPECT_COLOR_BIT && !format.compression && limits.minTexelBufferOffsetAlignment &&
                    value.offset%limits.minTexelBufferOffsetAlignment==0 && value.offset%format.bytes==0 && value.size%format.bytes==0 &&
                    value.size/format.bytes<=limits.maxTexelBufferElements && binding.scalar_width==32 && binding.scalar_type==format.scalar_type &&
                    (value.buffer->usage()&(read?VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT:VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)),"Shader texel-buffer view is incompatible");
                require(read || (binding.image_format && binding.image_format==format.storage_format),"Storage texel-buffer format specialization required");
                VkFormatProperties properties{};require(d->resource.GetFormatProperties,"Shader format query unavailable");d->resource.GetFormatProperties(d->resource.physical,value.format,&properties);
                require(properties.bufferFeatures&(read?VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT:VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT),"Texel-buffer format unsupported");
                VkBufferViewCreateInfo view={VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};view.buffer=value.buffer->buffer();view.offset=value.offset;view.range=value.size;view.format=value.format;
                VkBufferView handle=VK_NULL_HANDLE;checked(d->CreateBufferView(d->resource.device,&view,nullptr,&handle),"Create shader buffer view");
                invocation->views.push_back(handle);write.pTexelBufferView=&invocation->views.back();
            }
        }else if(binding.type==VK_DESCRIPTOR_TYPE_SAMPLER){
            require(value.sampler && value.sampler->impl_ && value.sampler->impl_->device->domain==d->domain && !value.buffer && !value.image,"Shader sampler requires a sampler from this domain");
            samplers.push_back(value.sampler->impl_);images.push_back({value.sampler->impl_->handle,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED});write.pImageInfo=&images.back();
        }else{
            require(value.image && !value.buffer && !value.sampler,"Shader image descriptor requires exactly one owned view");commands.validate(value.image->owner());
            const auto& format=format_info(value.image->format());const auto& description=value.image->owner()->description();const bool sampled=binding.type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            const auto aspects=value.image->range().aspectMask;const auto scalar=aspects==VK_IMAGE_ASPECT_STENCIL_BIT?2:format.scalar_type;
            require(value.image->type()==view_type(binding) && binding.image_multisample==(description.samples!=VK_SAMPLE_COUNT_1_BIT) &&
                (binding.image_depth!=1 || aspects==VK_IMAGE_ASPECT_DEPTH_BIT) && binding.scalar_width==32 && binding.scalar_type==scalar &&
                (value.image->usage()&(sampled?VK_IMAGE_USAGE_SAMPLED_BIT:VK_IMAGE_USAGE_STORAGE_BIT)),"Shader image type/format/usage is incompatible");
            require(sampled || (binding.image_format && binding.image_format==format.storage_format),"Storage image format specialization required");
            if(sampled)sampled_images.push_back(value.image);
            images.push_back({VK_NULL_HANDLE,value.image->view(),VK_IMAGE_LAYOUT_GENERAL});write.pImageInfo=&images.back();
        }
        writes.push_back(write);
    }
    // Until per-instruction sampler/image pairing is retained, conservatively
    // validate every sampled view against every bound sampler.
    for(const auto& sampler:samplers)for(const auto& image:sampled_images){const auto& info=sampler->info;
        if(info.magFilter==VK_FILTER_LINEAR || info.minFilter==VK_FILTER_LINEAR || info.mipmapMode==VK_SAMPLER_MIPMAP_MODE_LINEAR){
            VkFormatProperties properties{};d->resource.GetFormatProperties(d->resource.physical,image->format(),&properties);
            require(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT,"Sampled image does not support the bound linear sampler");}
        require(!info.compareEnable || image->range().aspectMask==VK_IMAGE_ASPECT_DEPTH_BIT,"Comparison sampler requires a depth view");
    }
    std::vector<VkDescriptorPoolSize> sizes;for(const auto& count:pipeline->impl_->counts)sizes.push_back({count.first,count.second});
    VkDescriptorPoolCreateInfo pool={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=1;pool.poolSizeCount=uint32_t(sizes.size());pool.pPoolSizes=sizes.data();
    checked(d->CreateDescriptorPool(d->resource.device,&pool,nullptr,&invocation->pool),"Create dispatch descriptor pool");
    VkDescriptorSet set=VK_NULL_HANDLE;VkDescriptorSetAllocateInfo allocate={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocate.descriptorPool=invocation->pool;allocate.descriptorSetCount=1;allocate.pSetLayouts=&pipeline->impl_->descriptors;
    checked(d->AllocateDescriptorSets(d->resource.device,&allocate,&set),"Allocate dispatch descriptor set");
    for(auto& write:writes)write.dstSet=set;d->UpdateDescriptorSets(d->resource.device,uint32_t(writes.size()),writes.data(),0,nullptr);
    // Retain every descriptor owner before any recorded command refers to it.
    const auto command=commands.command();commands.pending_->shader_owners_.push_back(invocation);
    for(const auto& value:descriptors)if(value.image)commands.transition(value.image->owner(),VK_IMAGE_LAYOUT_GENERAL);
    commands.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_UNIFORM_READ_BIT);
    d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline->impl_->pipeline);
    d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline->impl_->layout,0,1,&set,0,nullptr);
    d->CmdDispatch(command,groups[0],groups[1],groups[2]);
}
}
