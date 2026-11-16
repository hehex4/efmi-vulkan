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

    initial.known=true;initial.views[0].image.id=1;
    NativeTargetState::Saved saved;saved.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    saved.view.known=true;saved.view.handle=h<VkImageView>(9911);saved.view.view.id=3;
    saved.view.view.image=h<VkImage>(9910);saved.view.image.id=2;saved.view.image.levels=saved.view.image.layers=1;
    saved.view.view.range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    auto sample=bind(false,9910);auto resource=std::make_shared<Resource>(*sample.native_resource);
    resource->identity=(uint64_t(2)<<2)|2;resource->image_view=9911;resource->image_view_id=3;
    resource->aspect=VK_IMAGE_ASPECT_COLOR_BIT;resource->mip_count=resource->layer_count=resource->levels=resource->layers=1;
    sample.native_resource=resource;const auto paint=bind(true,9910);
    auto layouts=[&](std::initializer_list<efmi::assets::FilePlanItem> items,bool split=true){
        Plan plan;plan.items=items;NativeTargetPlan targets;targets.state=std::make_shared<NativeTargetState>();
        targets.state->restore[saved.view.handle]=saved;targets.events.resize(plan.items.size());
        for(size_t i=0;i<plan.items.size();++i)if(native_output_binding(plan.items[i].native_target)){
            auto event=std::make_shared<NativeTargetPlan::Event>();event->targets=initial;event->change=split;
            if(plan.items[i].native_resource->image==9910){event->targets.views[0]=saved.view;event->targets.dynamic_colors[0].imageView=saved.view.handle;}
            targets.events[i]=event;
        }
        native_image_phase_layouts(initial,plan,native_image_read_phases(initial,plan),targets);return targets;
    };
    auto result=layouts({paint,leave,sample});
    check(result.read_layouts[2]==VK_IMAGE_LAYOUT_GENERAL && result.state->restore.begin()->second.readable_when_detached,
          "an exact color target detached before sampling gets a readable phase and a restoration obligation");
    check(layouts({sample,paint,leave}).read_layouts[0]==VK_IMAGE_LAYOUT_UNDEFINED,
          "a later split cannot justify an earlier unreadable input");
    check(layouts({paint,sample,leave}).read_layouts[1]==VK_IMAGE_LAYOUT_UNDEFINED,
          "active output overlap never receives a phase layout");
    check(layouts({paint,leave,sample},false).read_layouts[2]==VK_IMAGE_LAYOUT_UNDEFINED,
          "an unexecuted output change cannot establish a readable phase");
    result=layouts({paint,leave,sample,clear,paint,leave,sample});
    check(result.read_layouts[2]==VK_IMAGE_LAYOUT_GENERAL && result.read_layouts[6]==VK_IMAGE_LAYOUT_GENERAL,
          "each separated read after a reattached target receives the same readable layout");
    resource->image_view_id=4;
    check(layouts({paint,leave,sample}).read_layouts[2]==VK_IMAGE_LAYOUT_UNDEFINED,"a recycled view identity cannot inherit a phase proof");
    resource->image_view_id=3;resource->mip_count=2;
    check(layouts({paint,leave,sample}).read_layouts[2]==VK_IMAGE_LAYOUT_UNDEFINED,"a wider sampled subresource range needs its own transitions");
    resource->mip_count=1;resource->identity=(uint64_t(4)<<2)|2;
    check(layouts({paint,leave,sample}).read_layouts[2]==VK_IMAGE_LAYOUT_UNDEFINED,"a recycled image identity cannot inherit a detached layout");
    resource->identity=(uint64_t(2)<<2)|2;saved.layout=VK_IMAGE_LAYOUT_GENERAL;
    check(layouts({paint,leave,sample}).read_layouts[2]==VK_IMAGE_LAYOUT_UNDEFINED,"an already readable target keeps its existing transition contract");
}
