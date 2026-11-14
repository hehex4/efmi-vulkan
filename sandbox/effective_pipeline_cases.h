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
            check(frozen && variant.effective_layout && variant.effective_layout->sets.size()==effective_expected_sets &&
                frozen->stages[0].entries[0].constantID==7 && frozen->stages[0].data[1]==6 && frozen->vertex.bindings[0].stride==12 && frozen->dynamic==source.dynamic,
                "effective snapshot owns the complete layout, specialization, geometry and dynamic state");
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
    // Rebuilt passes must retain every final shader interface, including private images and bones.
    for(int mode=0;mode<4;++mode){
        RecipeFixture mesh_fixture(mode>=2);auto source=mesh_fixture.capture();
        auto input=mode==2?source.vertex:RecipeFixture::mod_vertex();
        auto dispatches=dispatch();dispatches.CreateShaderModule=effective_shader;
        dispatches.CreateGraphicsPipelines=[](VkDevice,VkPipelineCache,uint32_t,const VkGraphicsPipelineCreateInfo*,const VkAllocationCallbacks*,VkPipeline* out){return allocate(out);};
        FragmentPatch mesh_patch;mesh_patch.keep_vertex=mode==1 || mode==2;mesh_patch.own_geometry=mode==1;
        if(mode>=2)mesh_patch.images.push_back({1,6});
        allocation=0;fail_at=0;effective_driver_words.clear();
        {PipelineVariant mesh(h<VkDevice>(2),dispatches);
            VkPhysicalDeviceLimits limits{};limits.maxDescriptorSetSampledImages=limits.maxPerStageDescriptorSampledImages=32;limits.maxPerStageResources=128;
            const bool built=mesh.build(source,mesh_fixture.layout,input,5,mesh_patch,limits);
            check(built,"merged, own-mesh, texture-only and combined effective fixtures build");
            const auto frozen=mesh.effective_recipe;bool same=frozen && frozen->stages.size()==effective_driver_words.size();
            if(same)for(size_t i=0;i<frozen->stages.size();++i)same&=frozen->stages[i].shader->code==effective_driver_words[i];
            check(same,"complete Mod snapshot freezes both rewritten driver stages exactly");
            check(frozen && frozen->vertex.attributes[1].format==input.attributes[1].format && frozen->dynamic==source.dynamic,
                "complete Mod snapshot retains actual vertex input and dynamic state");
            const auto layout=mesh.effective_layout;
            bool complete=layout && layout->sets.size()==4 && mesh.own_set==3;
            if(complete){const auto& bindings=layout->sets[3]->bindings;const auto expected=private_draw_bindings(mode>=2?1:0);
                complete=bindings.size()==expected.size();
                for(const auto& binding:expected){const auto found=bindings.find(binding.binding);
                    complete&=found!=bindings.end() && found->second.type==binding.descriptorType && found->second.count==binding.descriptorCount && found->second.stages==binding.stageFlags;}}
            check(complete,"complete Mod snapshot owns exact private bones, instance and image bindings");
            check(frozen && (mesh_patch.keep_vertex || !frozen->stages[0].shader->retained_total),
                "rewritten vertex words have no borrowed capture-budget owner");
        }
        check(live.empty(),"Mod effective metadata owns no unretained Vulkan handles");
    }
    check(live.empty(),"effective snapshot tests release all Vulkan objects");
}
