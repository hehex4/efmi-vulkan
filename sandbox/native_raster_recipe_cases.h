static bool raster_count_dynamic = false;
static VKAPI_ATTR VkResult VKAPI_CALL create_native_raster(VkDevice,VkPipelineCache,uint32_t count,
    const VkGraphicsPipelineCreateInfo* info,const VkAllocationCallbacks*,VkPipeline* output)
{
    check(count==1 && info->pRasterizationState->rasterizerDiscardEnable && info->pMultisampleState &&
          info->pMultisampleState->rasterizationSamples==VK_SAMPLE_COUNT_1_BIT &&
          info->pDepthStencilState && info->pColorBlendState && info->pColorBlendState->attachmentCount==1,
          "dynamic discard retains all possible rasterization state at driver creation");
    check(info->pViewportState && info->pViewportState->viewportCount==(raster_count_dynamic?0u:1u) &&
          info->pViewportState->scissorCount==(raster_count_dynamic?0u:1u) &&
          !info->pViewportState->pViewports && !info->pViewportState->pScissors,
          "dynamic viewport counts and ignored arrays are reconstructed consistently");
    *output=h<VkPipeline>(98);return VK_SUCCESS;
}
static void native_raster_recipe_tests()
{
    RecipeFixture f;f.rs.rasterizerDiscardEnable=VK_TRUE;
    std::vector<VkDynamicState> dynamic={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE,VK_DYNAMIC_STATE_CULL_MODE,VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE,VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,VK_DYNAMIC_STATE_STENCIL_OP};
    f.dyn.dynamicStateCount=uint32_t(dynamic.size());f.dyn.pDynamicStates=dynamic.data();
    auto recipe=f.capture();
    check(recipe.geometry_complete && !recipe.complete && recipe.has_depth && recipe.has_blend &&
          recipe.multisample.rasterizationSamples==VK_SAMPLE_COUNT_1_BIT,
          "a true static discard placeholder cannot suppress state needed by dynamic rasterization");
    JsonWriter json;recipe.write(json);
    check(json.out.find("\"multisample\":null")==std::string::npos,
          "recipe diagnostics preserve the multisample state behind dynamic discard");
    VkPipeline output=VK_NULL_HANDLE;const std::vector<VkShaderModule> modules={h<VkShaderModule>(4),h<VkShaderModule>(5)};
    check(recipe.create(h<VkDevice>(1),create_native_raster,h<VkPipelineLayout>(2),h<VkRenderPass>(3),modules,&output,nullptr,true)==VK_SUCCESS,
          "native twins can reconstruct extended raster state without a mesh permission");
    dynamic[0]=VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT;dynamic[1]=VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT;
    f.vp.viewportCount=f.vp.scissorCount=0;f.vp.pViewports=h<const VkViewport*>(1);f.vp.pScissors=h<const VkRect2D*>(1);
    recipe=f.capture();raster_count_dynamic=true;
    check(recipe.geometry_complete && recipe.viewports.empty() && recipe.scissors.empty(),
          "WITH_COUNT declarations never read ignored viewport/scissor arrays");
    check(recipe.create(h<VkDevice>(1),create_native_raster,h<VkPipelineLayout>(2),h<VkRenderPass>(3),modules,&output,nullptr,true)==VK_SUCCESS,
          "native recreation keeps dynamic viewport and scissor counts");
    f.gp.pViewportState=nullptr;
    check(f.capture().geometry_complete,"both dynamic counts permit an omitted viewport structure");
    dynamic[1]=VK_DYNAMIC_STATE_SCISSOR;
    check(!f.capture().geometry_complete && f.capture().reason=="raster_state_missing",
          "one dynamic count cannot supply an omitted static scissor count");
    dynamic[1]=VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT;f.gp.pMultisampleState=nullptr;
    check(!f.capture().geometry_complete && f.capture().reason=="raster_state_missing",
          "dynamic discard still requires the fixed multisample state");
}
