// Values set by the application, before an inserted graphics pipeline changes them.
#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <algorithm>

namespace efvk {
struct GraphicsDynamicState {
    static constexpr uint32_t viewport_limit=16;
    std::array<VkViewport,viewport_limit> viewports{};
    std::array<VkRect2D,viewport_limit> scissors{};
    uint32_t viewport_mask=0,scissor_mask=0,known=0;
    bool complete=true;
    float line_width=1;
    std::array<float,3> depth_bias{};
    std::array<float,4> blend_constants{};
    std::array<float,2> depth_bounds{};
    std::array<std::array<uint32_t,2>,3> stencil{};
    std::array<VkStencilFaceFlags,3> stencil_faces{};
    void set_viewports(uint32_t first,uint32_t count,const VkViewport* values){
        if(!count)return;
        if(!values || first>=viewport_limit || count>viewport_limit-first){complete=false;return;}
        std::copy_n(values,count,viewports.begin()+first);
        viewport_mask|=((1u<<count)-1)<<first;
    }
    void set_scissors(uint32_t first,uint32_t count,const VkRect2D* values){
        if(!count)return;
        if(!values || first>=viewport_limit || count>viewport_limit-first){complete=false;return;}
        std::copy_n(values,count,scissors.begin()+first);
        scissor_mask|=((1u<<count)-1)<<first;
    }
    void set_stencil(unsigned kind,VkStencilFaceFlags faces,uint32_t value){
        if(faces&VK_STENCIL_FACE_FRONT_BIT)stencil[kind][0]=value;
        if(faces&VK_STENCIL_FACE_BACK_BIT)stencil[kind][1]=value;
        stencil_faces[kind]|=faces;
    }
};
}
