#include "../layer/src/analysis_targets.h"
static void dynamic_analysis_cases()
{
    RenderTargets targets; targets.known = targets.dynamic = true; targets.layers = 2; targets.view_mask = 5;
    std::vector<AnalysisAttachment> output; std::set<VkImage> used; std::string why;
    auto inspect = [&](uint32_t options = 1) { return analysis_dynamic_targets(targets, options, output, used, why); };
    check(inspect() && output.empty() && used.empty(), "empty dynamic outputs produce no invented capture");
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; color.imageView=h<VkImageView>(9401);
    color.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    targets.dynamic_colors.push_back(color);targets.views.resize(2);
    targets.views[0].known=true;targets.views[0].handle=color.imageView;targets.views[0].view.image=h<VkImage>(9402);
    check(inspect() && output.size()==1 && used.count(h<VkImage>(9402)) && output[0].layout==color.imageLayout &&
          output[0].layers==2 && output[0].view_mask==5, "stored output preserves explicit layout and exact layer/multiview metadata");
    auto& attachment=targets.dynamic_colors[0];attachment.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    check(inspect() && output.empty() && used.size()==1, "discarded output still excludes descriptor aliases from capture");
    attachment.resolveMode=VK_RESOLVE_MODE_AVERAGE_BIT;attachment.resolveImageView=h<VkImageView>(9403);
    attachment.resolveImageLayout=VK_IMAGE_LAYOUT_GENERAL;targets.views[1].known=true;
    targets.views[1].handle=attachment.resolveImageView;targets.views[1].view.image=h<VkImage>(9404);
    check(inspect() && output.size()==1 && output[0].resolve==1 && output[0].layout==VK_IMAGE_LAYOUT_GENERAL && used.size()==2,
          "resolve capture remains stored when its multisample source is discarded");
    attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    check(inspect() && output.size()==2, "stored source and resolve destination create independent requests");
    attachment.resolveMode=VK_RESOLVE_MODE_NONE;targets.views[1].known=false;
    check(inspect() && output.size()==1 && used.size()==1, "disabled resolve ignores an irrelevant unknown destination");
    attachment.imageView=VK_NULL_HANDLE;attachment.resolveMode=VK_RESOLVE_MODE_AVERAGE_BIT;
    check(inspect() && output.empty() && used.empty(), "null source ignores standard resolve and store fields");
    attachment.imageView=color.imageView;
    check(!inspect() && output.empty() && used.empty(), "unknown active resolve refuses without partially publishing requests");
    attachment.resolveMode=VK_RESOLVE_MODE_NONE;targets.views[0].known=false;
    check(!inspect(), "unknown active attachment cannot permit descriptor alias capture");
    targets.views[0].known=true;targets.views.pop_back();
    check(!inspect(), "incomplete attachment snapshots refuse before reading views");
    targets.views.emplace_back();targets.flags=VK_RENDERING_SUSPENDING_BIT;
    check(!inspect(), "suspending passes cannot insert capture commands before resume");
    targets.flags=VK_RENDERING_RESUMING_BIT;
    check(!inspect(), "resumed segments retain the separate complete-pass capture boundary");
    targets.flags=VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
    check(inspect() && output.size()==1, "primary rendering with secondary contents has actual output views");
    targets.flags=0;targets.known=false;
    check(!inspect(), "unknown extension context stays excluded");
    targets.known=true;attachment.resolveMode=VK_RESOLVE_MODE_EXTERNAL_FORMAT_DOWNSAMPLE_BIT_ANDROID;attachment.imageView=VK_NULL_HANDLE;
    check(!inspect(), "external-format null resolve exception needs its own adapter");
    targets.dynamic_colors.clear();targets.has_depth=true;targets.depth=color;
    check(inspect() && output.empty() && used.size()==1, "depth output is not selected by dump_rt");
    check(inspect(2) && output.size()==1 && output[0].slot==8, "depth selection retains its role without granting unsupported depth readback");
}
