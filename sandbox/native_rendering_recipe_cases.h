static VKAPI_ATTR VkResult VKAPI_CALL create_rendering_native(VkDevice,VkPipelineCache,uint32_t count,
    const VkGraphicsPipelineCreateInfo* info,const VkAllocationCallbacks*,VkPipeline* output)
{
    const auto* rendering=static_cast<const VkPipelineRenderingCreateInfo*>(info->pNext);
    check(count==1 && !info->renderPass && rendering && rendering->sType==VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO && !rendering->pNext,
          "native dynamic creation supplies owned rendering info without a render pass");
    check(rendering && rendering->viewMask==5 && rendering->colorAttachmentCount==2 &&
          rendering->pColorAttachmentFormats[0]==VK_FORMAT_R8G8B8A8_UNORM && rendering->pColorAttachmentFormats[1]==VK_FORMAT_UNDEFINED &&
          rendering->depthAttachmentFormat==VK_FORMAT_D32_SFLOAT && rendering->stencilAttachmentFormat==VK_FORMAT_UNDEFINED,
          "native dynamic creation retains exact multiview and used/unused attachment formats");
    check(info->pDepthStencilState && info->pDepthStencilState->depthCompareOp==VK_COMPARE_OP_LESS &&
          info->pColorBlendState && info->pColorBlendState->attachmentCount==2 && info->pColorBlendState->pAttachments[1].colorWriteMask==0 &&
          info->pVertexInputState->pVertexBindingDescriptions[0].stride==12 && info->pDynamicState->dynamicStateCount==2,
          "native dynamic creation retains depth, blending, geometry and dynamic declarations");
    return allocate(output);
}
static void native_rendering_recipe_tests()
{
    RecipeFixture f;f.gp.renderPass=VK_NULL_HANDLE;f.gp.subpass=42;
    VkFormat formats[]={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_UNDEFINED};
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.viewMask=5;rendering.colorAttachmentCount=2;rendering.pColorAttachmentFormats=formats;
    rendering.depthAttachmentFormat=VK_FORMAT_D32_SFLOAT;f.gp.pNext=&rendering;
    VkPipelineColorBlendAttachmentState blend[]={f.ba,{}};f.cb.attachmentCount=2;f.cb.pAttachments=blend;
    auto capture=[&]{return GraphicsRecipe::capture(f.gp,f.shaders,nullptr);};
    auto recipe=capture();
    check(recipe.geometry_complete && !recipe.complete && recipe.dynamic_rendering && !recipe.render_pass && recipe.subpass==0 &&
          recipe.reason=="dynamic_rendering_native_only","dynamic recipes permit native twins while preserving mesh replacement restrictions");
    formats[0]=VK_FORMAT_R16_SFLOAT;rendering.viewMask=0;blend[1].colorWriteMask=15;
    auto copy=recipe;auto moved=std::move(copy);recipe={};
    VkPipeline output=VK_NULL_HANDLE;const std::vector<VkShaderModule> modules={h<VkShaderModule>(4),h<VkShaderModule>(5)};
    check(moved.create(h<VkDevice>(1),create_rendering_native,h<VkPipelineLayout>(2),VK_NULL_HANDLE,modules,&output,nullptr,true)==VK_SUCCESS,
          "copied and moved dynamic recipe survives caller-array mutation");
    destroy_pipe(h<VkDevice>(1),output,nullptr);
    check(moved.create(h<VkDevice>(1),create_rendering_native,h<VkPipelineLayout>(2),VK_NULL_HANDLE,modules,&output)!=VK_SUCCESS && !output,
          "a dynamic recipe does not authorize ordinary mesh recreation");
    check(moved.create(h<VkDevice>(1),create_rendering_native,h<VkPipelineLayout>(2),h<VkRenderPass>(3),modules,&output,nullptr,true)!=VK_SUCCESS && !output,
          "native dynamic recreation rejects an accidental classic render pass");
    check(moved.create(h<VkDevice>(1),create_rendering_native,h<VkPipelineLayout>(2),VK_NULL_HANDLE,modules,&output,&moved.vertex,true)!=VK_SUCCESS,
          "dynamic native recreation cannot override geometry");
    JsonWriter json;moved.write(json);
    check(json.out.find("\"dynamic_rendering\":true")!=std::string::npos && json.out.find("\"rendering_view_mask\":5")!=std::string::npos,
          "diagnostics retain rendering context without claiming a mesh recipe is complete");
    {
        auto d=dispatch();d.CreateGraphicsPipelines=create_rendering_native;d.CreatePipelineLayout=create_compute_layout;d.CreateRenderPass=nullptr;
        FragmentPatch patch;patch.native_only=true;patch.keep_vertex=true;
        PipelineVariant variant(h<VkDevice>(1),d);PipelineLayoutInfo layout;layout.complete=true;
        check(variant.build(moved,layout,{},8,patch),"native variant construction creates no classic render pass for dynamic rendering");
    }
    VkBaseInStructure unknown{VK_STRUCTURE_TYPE_MAX_ENUM};rendering.pNext=&unknown;
    check(!capture().geometry_complete,"unknown rendering extensions cannot masquerade as complete pipeline state");
    rendering.pNext=&rendering;
    check(!capture().geometry_complete,"duplicate rendering chains refuse without traversal");
    rendering.pNext=nullptr;rendering.pColorAttachmentFormats=nullptr;
    check(!capture().geometry_complete,"nonempty rendering format arrays must be present");
    rendering.colorAttachmentCount=UINT32_MAX;
    check(!capture().geometry_complete,"rendering format retention is bounded");
    rendering.colorAttachmentCount=0;rendering.depthAttachmentFormat=VK_FORMAT_UNDEFINED;rendering.stencilAttachmentFormat=VK_FORMAT_S8_UINT;
    f.gp.pDepthStencilState=nullptr;
    check(!capture().geometry_complete && capture().reason=="depth_state_missing","stencil-only rendering still requires depth/stencil state");
    f.gp.pDepthStencilState=&f.ds;
    check(capture().geometry_complete && capture().has_depth && !capture().has_blend,"stencil-only rendering retains only applicable attachment state");
    f.gp.pNext=nullptr;f.gp.pDepthStencilState=h<const VkPipelineDepthStencilStateCreateInfo*>(1);
    f.gp.pColorBlendState=h<const VkPipelineColorBlendStateCreateInfo*>(1);
    check(capture().geometry_complete && !capture().has_depth && !capture().has_blend,
          "omitted rendering info uses Vulkan's empty attachment defaults and ignores irrelevant pointers");
    f.gp.pNext=&rendering;rendering.colorAttachmentCount=1;rendering.pColorAttachmentFormats=formats;
    f.rs.rasterizerDiscardEnable=VK_TRUE;f.gp.pMultisampleState=h<const VkPipelineMultisampleStateCreateInfo*>(1);
    check(capture().geometry_complete,"static discarded rasterization does not read ignored dynamic-rendering state pointers");
    f.gp.flags=VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;f.gp.pNext=h<const void*>(1);
    check(!capture().geometry_complete,"library flags refuse before any possibly ignored extension pointer is read");
    RecipeFixture classic;classic.gp.pNext=&rendering;rendering.pColorAttachmentFormats=h<const VkFormat*>(1);rendering.colorAttachmentCount=UINT32_MAX;
    check(classic.capture().complete,"classic pipelines ignore dynamic-rendering format fields");
}
