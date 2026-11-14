static bool native_dynamic_input = false;
static uint32_t native_dynamic_creations = 0;
static VKAPI_ATTR VkResult VKAPI_CALL create_native_dynamic(VkDevice, VkPipelineCache, uint32_t count,
    const VkGraphicsPipelineCreateInfo* info, const VkAllocationCallbacks*, VkPipeline* output)
{
    ++native_dynamic_creations;
    check(count==1 && info->pDynamicState && info->pDynamicState->dynamicStateCount==4 &&
          info->pDynamicState->pDynamicStates[0]==VK_DYNAMIC_STATE_VIEWPORT &&
          info->pDynamicState->pDynamicStates[1]==VK_DYNAMIC_STATE_SCISSOR &&
          info->pDynamicState->pDynamicStates[2]==(native_dynamic_input?VK_DYNAMIC_STATE_VERTEX_INPUT_EXT:VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE) &&
          info->pDynamicState->pDynamicStates[3]==VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
          "native twins preserve the exact dynamic state declaration");
    check(native_dynamic_input ? info->pVertexInputState==nullptr :
          info->pVertexInputState && info->pVertexInputState->vertexBindingDescriptionCount==1 &&
          info->pVertexInputState->pVertexBindingDescriptions[0].stride==12 &&
          info->pVertexInputState->vertexAttributeDescriptionCount==2,
          "native twins preserve static geometry or omit fully dynamic vertex input");
    *output=h<VkPipeline>(97);return VK_SUCCESS;
}
static void native_dynamic_recipe_tests()
{
    RecipeFixture f;
    VkDynamicState dynamics[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE,VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY};
    f.dyn.dynamicStateCount=4;f.dyn.pDynamicStates=dynamics;
    auto recipe=f.capture();VkPipeline output=VK_NULL_HANDLE;
    const std::vector<VkShaderModule> modules={h<VkShaderModule>(4),h<VkShaderModule>(5)};
    VertexInputRecipe changed=recipe.vertex;changed.bindings[0].stride=32;
    check(recipe.create(h<VkDevice>(1),create_native_dynamic,h<VkPipelineLayout>(2),h<VkRenderPass>(3),modules,&output,&changed,true)!=VK_SUCCESS && !output && !native_dynamic_creations,
          "native geometry mode cannot authorize a vertex-stream override");
    native_dynamic_input=false;
    check(recipe.create(h<VkDevice>(1),create_native_dynamic,h<VkPipelineLayout>(2),h<VkRenderPass>(3),modules,&output,nullptr,true)==VK_SUCCESS && output,
          "known dynamic stride and topology permit an unchanged-geometry native twin");
    VertexRepoint rewrite;std::string reason;
    check(!prepare_vertex_variant(recipe,f.layout,8,rewrite,reason),"dynamic native twin support does not authorize a mesh rewrite");
    dynamics[2]=VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;f.gp.pVertexInputState=nullptr;recipe=f.capture();native_dynamic_input=true;
    check(recipe.create(h<VkDevice>(1),create_native_dynamic,h<VkPipelineLayout>(2),h<VkRenderPass>(3),modules,&output,nullptr,true)==VK_SUCCESS && output,
          "full dynamic vertex input can be recreated without inventing a static layout");
    dynamics[2]=VK_DYNAMIC_STATE_LINE_STIPPLE_EXT;recipe=f.capture();const auto before=native_dynamic_creations;
    check(recipe.create(h<VkDevice>(1),create_native_dynamic,h<VkPipelineLayout>(2),h<VkRenderPass>(3),modules,&output,nullptr,true)!=VK_SUCCESS && !output && native_dynamic_creations==before,
          "unknown extended dynamics still refuse native creation before reaching the driver");
}
