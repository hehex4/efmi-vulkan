#include "../layer/src/native_targets.h"
static void native_absent_output_cases()
{
    using Binding=efmi::frontend::NativeBinding;
    using Operation=efmi::assets::FileOperation;
    Device d;CmdBuf cb;DrawRec draw;NativeTargetPlan prepared;std::string why;
    efmi::assets::FilePlan plan;
    for(uint32_t slot=0;slot<9;++slot){efmi::assets::FilePlanItem item;item.operation=Operation::NativeBind;
        item.native_target.kind=slot==8?Binding::DepthTarget:Binding::ColorTarget;item.native_target.slot=slot==8?0:slot;plan.items.push_back(item);}
    auto prepare=[&]{prepared={};return prepare_native_targets(&d,&cb,draw,plan,prepared,why);};
    check(prepare() && !prepared.state && !prepared.owner,"absent out-of-pass outputs need no private pass, queue or retained resource");
    for(size_t i=0;i<plan.items.size();++i)record_native_target(&d,&cb,prepared,i);
    check(!cb.native_target_state && cb.target_contexts.empty() && cb.owners.empty(),"proven absent output recording changes no command state");
    cb.secondary=true;check(prepare(),"non-inherited compute secondary can unbind already absent outputs");
    cb.inherited_rp=true;check(!prepare(),"inherited parent output presence is not assumed absent");cb.inherited_rp=false;cb.secondary=false;
    cb.active_targets=1;check(!prepare(),"stale out-of-pass output context cannot prove absence");cb.active_targets=0;
    cb.in_rp=true;check(!prepare(),"missing in-pass context cannot prove output absence");
    RenderTargets targets;targets.known=targets.dynamic=true;targets.dynamic_colors.resize(2);targets.views.resize(4);
    cb.target_contexts={targets};cb.active_targets=1;
    check(prepare() && !prepared.state,"complete null dynamic outputs need no split or enabled unused-attachment feature");
    cb.target_contexts[0].dynamic_colors[0].resolveMode=VK_RESOLVE_MODE_EXTERNAL_FORMAT_DOWNSAMPLE_BIT_ANDROID;
    check(!prepare(),"external-format null-source resolve does not prove absent output semantics");cb.target_contexts[0]=targets;
    cb.execution_scopes.known=false;check(!prepare(),"unknown attachment mappings cannot prove a requested location absent");cb.execution_scopes.reset();
    cb.target_contexts[0].known=false;check(!prepare(),"unknown extension context cannot prove null attachment semantics");cb.target_contexts[0].known=true;
    cb.target_contexts[0].views.pop_back();check(!prepare(),"incomplete dynamic context cannot prove all outputs absent");cb.target_contexts[0]=targets;
    cb.target_contexts[0].has_stencil=true;cb.target_contexts[0].stencil.imageView=h<VkImageView>(9601);cb.target_contexts[0].views.resize(6);
    check(!prepare(),"stencil-only actual target prevents depth-output no-op");cb.target_contexts[0]=targets;
    cb.target_contexts[0].dynamic_colors[1].imageView=h<VkImageView>(9602);
    check(!prepare(),"any bound color prevents classifying the whole unbind plan as a no-op");cb.target_contexts[0]=targets;
    auto recipe=std::make_shared<RenderPassRecipe>();recipe->complete=true;recipe->subpasses.emplace_back();
    VkAttachmentReference2 unused{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};unused.attachment=VK_ATTACHMENT_UNUSED;
    recipe->subpasses[0].colors.push_back(unused);targets.dynamic=false;targets.recipe=recipe;cb.target_contexts[0]=targets;
    check(prepare() && !prepared.state,"known classic unused references are no-op outputs without pass recreation");
    recipe->subpasses[0].colors[0].attachment=0;check(!prepare(),"bound classic color cannot be dropped as a null no-op");recipe->subpasses[0].colors[0]=unused;
    recipe->complete=false;check(!prepare(),"incomplete classic subpass cannot prove absence");recipe->complete=true;
    auto descriptor=plan.items[0];descriptor.native_target.kind=Binding::Access;plan.items.push_back(descriptor);
    check(prepare() && !prepared.state,"proven absent outputs do not create a mixed-descriptor hazard");plan.items.pop_back();
    plan.items[0].native_target.slot=8;check(!prepare(),"invalid color slot is not an absent valid output");plan.items[0].native_target.slot=0;
    auto resource=std::make_shared<efmi::frontend::NativeResource>();resource->bound=true;
    auto write=plan.items[0];write.native_resource=resource;plan.items.insert(plan.items.begin(),write);
    check(!prepare() && !prepared.state,"plan-wide nonnull output writes prevent later null from being erased as initially absent");
}
