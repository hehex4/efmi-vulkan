// Native resource-reference ownership checks, included by tracker_test.
#include "../layer/src/native_resources.h"
namespace {
std::vector<int> resource_destroy_order;
VKAPI_ATTR void VKAPI_CALL retained_memory_free(VkDevice,VkDeviceMemory,const VkAllocationCallbacks*) { resource_destroy_order.push_back(3); }
VKAPI_ATTR void VKAPI_CALL retained_buffer_destroy(VkDevice,VkBuffer,const VkAllocationCallbacks*) { resource_destroy_order.push_back(2); }
VKAPI_ATTR void VKAPI_CALL retained_image_destroy(VkDevice,VkImage,const VkAllocationCallbacks*) { resource_destroy_order.push_back(2); }
VKAPI_ATTR void VKAPI_CALL retained_buffer_view_destroy(VkDevice,VkBufferView,const VkAllocationCallbacks*) { resource_destroy_order.push_back(1); }
VKAPI_ATTR void VKAPI_CALL retained_image_view_destroy(VkDevice,VkImageView,const VkAllocationCallbacks*) { resource_destroy_order.push_back(1); }

void native_resource_lifetimes()
{
    Device d; d.d.FreeMemory=retained_memory_free;d.d.DestroyBuffer=retained_buffer_destroy;
    d.d.DestroyImage=retained_image_destroy;d.d.DestroyBufferView=retained_buffer_view_destroy;d.d.DestroyImageView=retained_image_view_destroy;
    const auto memory=h<VkDeviceMemory>(401),image_memory=h<VkDeviceMemory>(402);
    const auto buffer=h<VkBuffer>(403);const auto image=h<VkImage>(404);
    const auto bv=h<VkBufferView>(405);const auto iv=h<VkImageView>(406);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=4096;
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer_info.size=256;buffer_info.usage=VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image_info.imageType=VK_IMAGE_TYPE_2D;image_info.extent={4,4,1};
    image_info.mipLevels=image_info.arrayLayers=1;image_info.samples=VK_SAMPLE_COUNT_1_BIT;image_info.format=VK_FORMAT_R8G8B8A8_UNORM;image_info.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
    on_alloc_memory(&d,memory,&allocation);on_create_buffer(&d,buffer,&buffer_info);on_bind_buffer(&d,buffer,memory,0);
    VkBufferViewCreateInfo view{VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};view.buffer=buffer;view.range=256;view.format=VK_FORMAT_R32_UINT;
    on_create_buffer_view(&d,bv,&view);auto held=d.buffer_views.at(bv).lifetime;
    resource_destroy_order.clear();
    check(bool(held),"native texel reference retains a view owner");
    check(on_destroy_buffer_view(&d,bv) && on_destroy_buffer(&d,buffer) && on_free_memory(&d,memory),"retained buffer/view/memory destruction is deferred");
    check(resource_destroy_order.empty(),"reference keeps the complete buffer allocation after application destruction");
    check(d.buffer_views.empty() && d.bufs.empty() && d.mems.empty(),"destroyed resources immediately leave logical tracker lookup");
    held.reset();check(resource_destroy_order==std::vector<int>({1,2,3}),"last texel reference releases view, buffer, then memory exactly once");

    on_alloc_memory(&d,image_memory,&allocation);on_create_image(&d,image,&image_info);on_bind_image(&d,image,image_memory,0);
    VkImageViewCreateInfo image_view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};image_view.image=image;image_view.viewType=VK_IMAGE_VIEW_TYPE_2D;
    image_view.format=image_info.format;image_view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    on_create_image_view(&d,iv,&image_view);auto held_image=d.image_views.at(iv).lifetime;
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer.attachmentCount=1;framebuffer.pAttachments=&iv;framebuffer.width=framebuffer.height=4;framebuffer.layers=1;
    on_create_framebuffer(&d,h<VkFramebuffer>(407),&framebuffer);
    auto history=d.framebuffers.at(h<VkFramebuffer>(407));
    check(history->views[0].known && !history->views[0].view.lifetime && !history->views[0].image.lifetime,
        "historical attachment facts retain metadata without resource owners");
    resource_destroy_order.clear();
    check(on_destroy_image_view(&d,iv) && on_destroy_image(&d,image) && on_free_memory(&d,image_memory),"retained image/view/memory destruction is deferred");
    check(resource_destroy_order.empty(),"reference keeps the complete image allocation after application destruction");
    held_image.reset();check(resource_destroy_order==std::vector<int>({1,2,3}),"last image reference releases view, image, then memory despite retained history");

    resource_destroy_order.clear();VkAllocationCallbacks allocator{};
    on_alloc_memory(&d,memory,&allocation,&allocator);on_create_buffer(&d,buffer,&buffer_info,&allocator);
    on_create_image(&d,image,&image_info,&allocator);
    check(!d.mems.at(memory).lifetime && !d.bufs.at(buffer).lifetime && !d.images.at(image).lifetime,"custom allocator resources cannot escape allocator lifetime");
    check(!on_destroy_buffer(&d,buffer) && !on_destroy_image(&d,image) && !on_free_memory(&d,memory),"custom allocator destruction stays synchronous in hooks");
    on_create_image(&d,image,&image_info,nullptr,false);
    check(!d.images.at(image).lifetime && !on_destroy_image(&d,image),"swapchain-owned images never acquire a DestroyImage owner");
    on_alloc_memory(&d,memory,&allocation);on_create_buffer(&d,buffer,&buffer_info);on_bind_buffer(&d,buffer,memory,0);
    d.bufs.clear();d.mems.clear();check(resource_destroy_order.empty(),"metadata teardown never destroys objects still owned by the application");
}

void native_descriptor_references()
{
    Device d;d.d.FreeMemory=retained_memory_free;d.d.DestroyBuffer=retained_buffer_destroy;
    const auto memory=h<VkDeviceMemory>(451);const auto buffer=h<VkBuffer>(452);const auto set=h<VkDescriptorSet>(453);const auto pipe=h<VkPipeline>(454);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=1024;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=512;info.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    on_alloc_memory(&d,memory,&allocation);on_create_buffer(&d,buffer,&info);on_bind_buffer(&d,buffer,memory,0);
    auto layout=std::make_shared<DescriptorLayoutInfo>();layout->bindings[0]={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,2,VK_SHADER_STAGE_COMPUTE_BIT};
    auto pipeline=std::make_shared<PipelineLayoutInfo>();pipeline->sets.push_back(layout);pipeline->prefix.push_back(100);
    d.pipes[pipe].layout=pipeline;
    DescriptorSetInfo source;source.id=455;source.layout=layout;source.counts[0]=2;source.generation=1;
    DescriptorElement value;value.known=true;value.type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    value.buffer=buffer;value.buffer_id=d.bufs.at(buffer).id;value.offset=16;value.range=32;
    source.values[{0,0}]=value;source.values[{0,1}]=value;d.descriptor_sets[set]=source;
    CmdBuf cb;cb.dyn={99,64,128};DrawRec draw;draw.pipeline=pipe;
    auto& bound=draw.sets[0];bound.set=set;bound.id=455;bound.layout=pipeline;bound.dyn_first=1;bound.dyn_count=2;
    efmi::frontend::NativeBinding alias;alias.stage=5;alias.kind=efmi::frontend::NativeBinding::Access;
    efmi::frontend::NativeResource reference;std::string why;
    check(native_resource(&d,&cb,draw,alias,reference,why) && reference.keep && reference.buffer==uint64_t(buffer) && reference.offset==80 && reference.range==32,
          "native reference retains its allocation and resolved dynamic range");
    auto prior=reference;prior.descriptor_type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;prior.stride=16;prior.format=42;prior.buffer_view=123;
    auto& rebound=d.descriptor_sets[set].values[{0,0}];rebound.native=std::make_shared<const efmi::frontend::NativeResource>(prior);rebound.offset=256;
    cb.dyn[1]=cb.dyn[2]=0; // Private copies normalize offsets into the descriptors and bind zero dynamic offsets.
    efmi::frontend::NativeResource rebound_query;
    check(native_resource(&d,&cb,draw,alias,rebound_query,why) && rebound_query.descriptor_type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC &&
          rebound_query.offset==256 && !rebound_query.stride && !rebound_query.format && !rebound_query.buffer_view,
          "rebound native metadata follows the destination descriptor instead of the captured source view");
    rebound.native.reset();prior={};rebound_query={};
    resource_destroy_order.clear();on_destroy_buffer(&d,buffer);on_free_memory(&d,memory);
    check(resource_destroy_order.empty() && reference.range==32,"retained native descriptor remains owned after logical application destruction");
    reference={};check(resource_destroy_order==std::vector<int>({2,3}),"native descriptor releases its buffer then allocation after the last reference");
}
}
