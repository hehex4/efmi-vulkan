#include "../layer/src/native_image_phases.h"
static void native_image_phase_cases()
{
    using Binding=efmi::frontend::NativeBinding;
    using Plan=efmi::assets::FilePlan;
    using Resource=efmi::frontend::NativeResource;
    RenderTargets initial;initial.known=true;initial.dynamic=true;initial.dynamic_colors.resize(2);initial.views.resize(4);
    initial.dynamic_colors[0].imageView=h<VkImageView>(9900);initial.views[0].view.image=h<VkImage>(9901);
    auto bind=[](bool output,uint64_t image,uint32_t slot=0){
        efmi::assets::FilePlanItem item;item.operation=efmi::assets::FileOperation::NativeBind;
        item.native_target.kind=output?Binding::ColorTarget:Binding::Resource;
        item.native_target.stage=4;
        item.native_target.slot=slot;item.native_target.set=1;item.native_target.binding=slot;
        auto resource=std::make_shared<Resource>();resource->bound=image!=0;resource->image=image;item.native_resource=resource;
        return item;
    };
    const auto read=bind(false,9902),write=bind(true,9902),leave=bind(true,9901),clear=bind(false,0);
    auto phases=[&](std::initializer_list<efmi::assets::FilePlanItem> items){Plan plan;plan.items=items;return native_image_read_phases(initial,plan);};
    check(phases({write,leave,read})[2],"sampled binding follows a completed output interval");
    check(phases({read,clear,write})[0],"sampled binding ends before a later output interval");
    check(!phases({write,read,leave})[1],"sampling an active output is not a disjoint phase");
    check(!phases({read,write})[0],"a later output invalidates a still-bound sampled image");
    check(!phases({read,bind(false,0,1),write})[0],"overwriting another descriptor does not end this input interval");
    auto alias=clear;alias.native_target.stage=0;
    check(phases({read,alias,write})[0],"another stage alias of the same Vulkan descriptor ends its input interval");
    const auto repeat=phases({read,clear,write,leave,read});
    check(repeat[0]&&repeat[4],"two separated reads surround one output interval");
    const auto overwritten=phases({read,read,write});
    check(overwritten[0]&&!overwritten[1],"repeated assignment closes one interval while the later interval still conflicts");
    auto other_slot=write;other_slot.native_target.slot=1;
    check(!phases({write,other_slot,leave,read})[3],"an image still attached to a different output slot remains active");
    check(!phases({bind(false,9901)})[0],"initial application outputs participate in phase classification");
    auto element=clear;element.native_target.element=1;
    check(!phases({read,element,write})[0],"another array element does not end the sampled binding interval");
    initial.known=false;check(!phases({read})[0],"unknown attachment context cannot grant a read phase");
}
