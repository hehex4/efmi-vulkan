static std::vector<std::vector<uint32_t>> effective_driver_words;
static uint32_t effective_expected_sets=3;
static VkResult VKAPI_CALL effective_shader(VkDevice,const VkShaderModuleCreateInfo* info,const VkAllocationCallbacks*,VkShaderModule* out){
    effective_driver_words.emplace_back(info->pCode,info->pCode+info->codeSize/4);return allocate(out);
}
static VkResult VKAPI_CALL effective_layout_create(VkDevice,const VkPipelineLayoutCreateInfo* info,const VkAllocationCallbacks*,VkPipelineLayout* out){
    check(info->setLayoutCount==effective_expected_sets,"effective snapshot fixture observes the real pipeline layout");return allocate(out);
}
static VkResult VKAPI_CALL effective_pipeline_create(VkDevice,VkPipelineCache,uint32_t count,const VkGraphicsPipelineCreateInfo* info,const VkAllocationCallbacks*,VkPipeline* out){
    check(count==1 && info->stageCount==2 && info->pVertexInputState->pVertexBindingDescriptions[0].stride==12 &&
        info->pDynamicState->dynamicStateCount==2,"effective pipeline keeps original geometry and dynamic declarations");return allocate(out);
}
static void effective_pipeline_cases(){
    RecipeFixture fixture;VkSpecializationMapEntry entry{7,1,2};uint8_t bytes[]{5,6,7,8};VkSpecializationInfo spec{1,&entry,4,bytes};
    fixture.stages[0].pSpecializationInfo=&spec;auto recipe=fixture.capture();
    auto replacement=std::make_shared<ShaderInfo>();replacement->code=variant_fixture::shader(false);replacement->code[2]^=1;
    const auto expected_replacement=replacement->code;
    auto fixes=std::make_shared<NativeShaderSet>();fixes->revision=73;fixes->modules[(uint64_t(VK_SHADER_STAGE_FRAGMENT_BIT)<<32)|recipe.stages[1].shader->hash]=replacement;
    fixes->compatible=[](const std::vector<uint32_t>&,const std::vector<uint32_t>&,VkShaderStageFlagBits,const std::string&,std::string&){return true;};
    auto funcs=dispatch();funcs.CreateShaderModule=effective_shader;funcs.CreateGraphicsPipelines=effective_pipeline_create;funcs.CreatePipelineLayout=effective_layout_create;
    FragmentPatch patch;patch.keep_vertex=patch.native_only=true;patch.native_shaders=fixes;
    std::shared_ptr<const GraphicsRecipe> retained;std::shared_ptr<const PipelineLayoutInfo> retained_layout;
    for(int mode=0;mode<4;++mode){
        patch.original_stages=mode==1?VK_SHADER_STAGE_FRAGMENT_BIT:0;patch.pink=mode==2;patch.no_dither=mode==3;
        patch.native_only=mode!=3;patch.native_shaders=mode==3?nullptr:fixes;effective_expected_sets=mode==3?4:3;
        auto source=recipe;
        if(mode==3){auto dither=std::make_shared<ShaderInfo>();dither->code=variant_fixture::shader(false,false,6,variant_fixture::Kill::Dither);source.stages[1].shader=dither;}
        allocation=0;fail_at=0;effective_driver_words.clear();
        {
            PipelineVariant variant(h<VkDevice>(2),funcs);const bool built=variant.build(source,fixture.layout,source.vertex,5,patch);
            check(built,"effective snapshot native/replaced/original/pink/dither fixture builds");
            const auto frozen=variant.effective_recipe;bool same=frozen && frozen->stages.size()==effective_driver_words.size();
            if(same)for(size_t i=0;i<frozen->stages.size();++i)same&=frozen->stages[i].shader && frozen->stages[i].shader->code==effective_driver_words[i];
            check(same,"frozen effective stages exactly match every module submitted to the driver");
            check(frozen && variant.effective_layout && variant.effective_layout->sets.size()==fixture.layout.sets.size() &&
                frozen->stages[0].entries[0].constantID==7 && frozen->stages[0].data[1]==6 && frozen->vertex.bindings[0].stride==12 && frozen->dynamic==source.dynamic,
                "effective snapshot owns original layout prefix, specialization, geometry and dynamic state");
            if(mode==0){retained=frozen;retained_layout=variant.effective_layout;}
            if(mode==1)check(frozen && frozen->stages[1].shader->code==recipe.stages[1].shader->code,"hunting original snapshot freezes the original stage");
            if(mode==2)check(frozen && frozen->stages[1].shader->code!=replacement->code && frozen->stages[1].data.empty(),"pink snapshot freezes generated code without stale specialization");
            if(mode==3)check(frozen && efmi::gpu::dither_kills(frozen->stages[1].shader->code)==0 &&
                !frozen->stages[1].shader->retained_total,"pure dither snapshot freezes rewritten words without a borrowed capture-budget owner");
            fail_at=allocation+1;
            check(!variant.build(source,fixture.layout,source.vertex,5,patch) && !variant.effective_recipe && !variant.effective_layout,
                "failed rebuild publishes no previous successful effective snapshot");fail_at=0;
        }
        check(live.empty(),"effective snapshot metadata outlives no GPU handle");
    }
    fixes->modules.clear();patch={};replacement.reset();recipe={};fixture.layout.pushes.clear();
    check(retained && retained_layout && retained->stages[1].shader->code==expected_replacement &&
        retained->stages[0].data==std::vector<uint8_t>({5,6,7,8}) && retained_layout->sets.size()==3,
        "retained replacement and specialization survive source, publication and variant destruction");
    // A mesh variant has a new vertex interface and must not advertise application-geometry cloning.
    allocation=0;fail_at=0;{PipelineVariant mesh(h<VkDevice>(2),dispatch());auto original=fixture.capture();
        check(mesh.build(original,fixture.layout,RecipeFixture::mod_vertex(),4) && !mesh.effective_recipe && !mesh.effective_layout,
            "mesh interface rewrite does not publish a native effective recipe");}
    check(live.empty(),"effective snapshot tests release all Vulkan objects");
}
