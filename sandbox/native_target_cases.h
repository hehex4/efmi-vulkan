// Native OM source queries map output locations and acquire live allocation owners on demand.
static void native_target_cases()
{
    using B=efmi::frontend::NativeBinding;using R=efmi::frontend::NativeResource;
    Device d;CmdBuf cb;d.shader_draws=true;cb.in_rp=true;
    d.d.FreeMemory=retained_memory_free;d.d.DestroyImage=retained_image_destroy;d.d.DestroyImageView=retained_image_view_destroy;
    const auto memory=h<VkDeviceMemory>(8801);const auto image=h<VkImage>(8802);const auto color=h<VkImageView>(8803),depth=h<VkImageView>(8804);
    const auto pass=h<VkRenderPass>(8805);const auto fb=h<VkFramebuffer>(8806);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=4096;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;info.extent={64,32,1};
    info.mipLevels=2;info.arrayLayers=3;info.samples=VK_SAMPLE_COUNT_1_BIT;info.format=VK_FORMAT_R8G8B8A8_UNORM;
    info.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    on_alloc_memory(&d,memory,&allocation);on_create_image(&d,image,&info);on_bind_image(&d,image,memory,0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;
    vi.format=info.format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,1,1,2,1};on_create_image_view(&d,color,&vi);
    const auto depth_image=h<VkImage>(8807);auto di=info;di.format=VK_FORMAT_D32_SFLOAT;di.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    on_create_image(&d,depth_image,&di);vi.image=depth_image;vi.format=di.format;vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
    on_create_image_view(&d,depth,&vi);
    VkAttachmentDescription attachments[2]{};attachments[0].format=di.format;attachments[1].format=info.format;
    for(auto& a:attachments)a.samples=VK_SAMPLE_COUNT_1_BIT;
    VkAttachmentReference colors[]={{1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},{VK_ATTACHMENT_UNUSED,VK_IMAGE_LAYOUT_UNDEFINED}};
    VkAttachmentReference ds{0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};VkSubpassDescription subs[2]{};
    for(auto& sub:subs)sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    subs[0].colorAttachmentCount=2;subs[0].pColorAttachments=colors;subs[0].pDepthStencilAttachment=&ds;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.attachmentCount=2;rp.pAttachments=attachments;rp.subpassCount=2;rp.pSubpasses=subs;
    on_create_render_pass(&d,pass,&rp);
    VkImageView views[]={depth,color};VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass=pass;fi.attachmentCount=2;fi.pAttachments=views;fi.width=32;fi.height=16;fi.layers=1;on_create_framebuffer(&d,fb,&fi);
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass;begin.framebuffer=fb;on_begin_targets(&d,&cb,&begin);
    check(cb.active_targets && !d.capture_targets,"active native shader hooks record output context without a hunting capture");
    DrawRec draw;draw.targets=cb.active_targets;B alias;alias.kind=B::ColorTarget;R out;std::string why;
    check(native_resource(&d,&cb,draw,alias,out,why) && out.bound && out.image_view==uint64_t(color) && out.format==28 &&
          out.width==64 && out.height==32 && out.levels==2 && out.layers==3 && out.base_mip==1 && out.base_layer==2 && out.bind_flags==40,
          "native o0 resolves subpass output index rather than framebuffer index and keeps allocation/view descriptions");
    check(out.keep && out.image_safe && !out.hash_known && out.descriptor_type==VK_DESCRIPTOR_TYPE_MAX_ENUM &&
          out.image_layout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,"owned output acquires full lifetime without inventing a descriptor or writable hash");
    const auto saved=out;out={};
    alias.slot=1;check(native_resource(&d,&cb,draw,alias,out,why) && !out.bound,"unused color output is unbound");
    alias.slot=7;check(native_resource(&d,&cb,draw,alias,out,why) && !out.bound,"color output beyond this subpass is unbound");
    alias.slot=8;check(!native_resource(&d,&cb,draw,alias,out,why),"color output beyond original slot limits is refused");
    alias.slot=0;alias.kind=B::DepthTarget;
    check(native_resource(&d,&cb,draw,alias,out,why) && out.bound && out.image_view==uint64_t(depth) && out.format==40 && out.resource_format==40 && out.bind_flags==64 && !out.keep,
          "depth source keeps actual format and metadata but no fabricated allocation owner");
    d.images.at(depth_image).usage|=VK_IMAGE_USAGE_SAMPLED_BIT;
    check(native_resource(&d,&cb,draw,alias,out,why) && out.format==40 && out.resource_format==39,
          "Sampled D32 source retains its typeless allocation with a typed target view");
    for(const auto& row:std::vector<std::array<uint32_t,5>>{{VK_FORMAT_D16_UNORM,53,55,56,0},
          {VK_FORMAT_D32_SFLOAT,39,40,41,0},{VK_FORMAT_D24_UNORM_S8_UINT,44,45,46,47},
          {VK_FORMAT_D32_SFLOAT_S8_UINT,19,20,21,22}}){
        const auto format=VkFormat(row[0]);
        check(dxgi_application_format(format,VK_IMAGE_USAGE_SAMPLED_BIT)==row[1] &&
              dxgi_application_format(format,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)==row[2] &&
              dxgi_depth_target_format(format)==row[2] && dxgi_view_format(format,VK_IMAGE_ASPECT_DEPTH_BIT)==row[3] &&
              dxgi_view_format(format,VK_IMAGE_ASPECT_STENCIL_BIT)==row[4] &&
              !dxgi_view_format(format,VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT),
              "Application depth allocation, DSV and individual SRV aspects use the original DXGI family");
    }
    on_next_targets(&d,&cb);draw.targets=cb.active_targets;
    check(native_resource(&d,&cb,draw,alias,out,why) && !out.bound,"next subpass without depth is unbound");
    alias.kind=B::ColorTarget;check(native_resource(&d,&cb,draw,alias,out,why) && !out.bound,"next subpass without color is unbound");
    draw.targets=1;check(native_resource(&d,&cb,draw,alias,out,why) && out.image_view==uint64_t(color),"earlier draw retains its own attachment mapping");
    fi.flags=VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;fi.pAttachments=nullptr;on_create_framebuffer(&d,fb,&fi);
    VkRenderPassAttachmentBeginInfo late{VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO};late.attachmentCount=2;late.pAttachments=views;begin.pNext=&late;
    on_begin_targets(&d,&cb,&begin);draw.targets=cb.active_targets;
    check(native_resource(&d,&cb,draw,alias,out,why) && out.image_view==uint64_t(color),"imageless target uses runtime views and subpass output order");
    VkRenderingAttachmentInfo color_info{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};color_info.imageView=color;color_info.imageLayout=VK_IMAGE_LAYOUT_GENERAL;
    color_info.resolveImageView=depth; color_info.resolveMode=VK_RESOLVE_MODE_AVERAGE_BIT;
    VkRenderingAttachmentInfo depth_info{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};depth_info.imageView=depth;depth_info.imageLayout=VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};rendering.colorAttachmentCount=1;rendering.pColorAttachments=&color_info;
    rendering.pDepthAttachment=&depth_info;on_begin_targets(&d,&cb,&rendering);draw.targets=cb.active_targets;
    check(native_resource(&d,&cb,draw,alias,out,why) && out.image_view==uint64_t(color) && out.image_layout==VK_IMAGE_LAYOUT_GENERAL,
          "dynamic target source selects color rather than its resolve destination");
    alias.kind=B::DepthTarget;check(native_resource(&d,&cb,draw,alias,out,why) && out.image_view==uint64_t(depth),"dynamic depth source follows its own view");
    rendering.pStencilAttachment=&depth_info;on_begin_targets(&d,&cb,&rendering);draw.targets=cb.active_targets;
    check(native_resource(&d,&cb,draw,alias,out,why) && out.image_view==uint64_t(depth),"shared depth/stencil source can be represented as one view");
    auto stencil=depth_info;stencil.imageView=color;rendering.pStencilAttachment=&stencil;on_begin_targets(&d,&cb,&rendering);draw.targets=cb.active_targets;
    check(!native_resource(&d,&cb,draw,alias,out,why),"separate depth/stencil sources cannot be silently collapsed");
    rendering.pDepthAttachment=nullptr;rendering.pStencilAttachment=&depth_info;on_begin_targets(&d,&cb,&rendering);draw.targets=cb.active_targets;
    check(native_resource(&d,&cb,draw,alias,out,why) && out.image_view==uint64_t(depth),"stencil-only source uses its actual attachment");
    depth_info.imageView=VK_NULL_HANDLE;rendering.pDepthAttachment=&depth_info;on_begin_targets(&d,&cb,&rendering);draw.targets=cb.active_targets;
    check(native_resource(&d,&cb,draw,alias,out,why) && !out.bound,"null dynamic attachment views are unbound");
    draw.targets=1;cb.inherited_rp=true;check(!native_resource(&d,&cb,draw,alias,out,why) && why.find("inherited")!=std::string::npos,
          "inherited secondary never borrows an unavailable parent output context");cb.inherited_rp=false;
    draw.targets=0;check(!native_resource(&d,&cb,draw,alias,out,why),"unobserved active pass is unknown rather than unbound");
    cb.in_rp=false;check(native_resource(&d,&cb,draw,alias,out,why) && !out.bound,"outside a rendering scope the output source is unbound");
    cb.in_rp=true;draw.targets=1;alias.kind=B::ColorTarget;out={};resource_destroy_order.clear();
    on_destroy_image_view(&d,color);on_destroy_image(&d,image);on_free_memory(&d,memory);
    check(resource_destroy_order.empty() && saved.keep,"held target reference keeps view, image and memory alive after application destruction");
    check(!native_resource(&d,&cb,draw,alias,out,why),"new target query refuses logically destroyed objects despite retained historical metadata");
    on_create_image(&d,image,&info);vi.image=image;vi.format=info.format;vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;on_create_image_view(&d,color,&vi);
    check(!native_resource(&d,&cb,draw,alias,out,why),"reused view and image handles cannot satisfy an old output snapshot");
}
