static void dynamic_depth_cache_cases()
{
    variant_fixture::RecipeFixture fixture;
    auto recipe = std::make_shared<GraphicsRecipe>(fixture.capture());
    auto layout = std::make_shared<PipelineLayoutInfo>(fixture.layout);
    recipe->render_pass.reset(); recipe->dynamic_rendering = true; recipe->complete = false;
    recipe->rendering_colors = {VK_FORMAT_R8G8B8A8_UNORM}; recipe->rendering_depth = VK_FORMAT_D32_SFLOAT;
    for (int filter : {1, 2}) {
        auto fixes = std::make_shared<NativeShaderSet>(); fixes->revision = 4;
        for (const auto& stage : recipe->stages) if (stage.stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
            fixes->modules[(uint64_t(stage.stage) << 32) | stage.shader->hash] = stage.shader;
            fixes->depth_filters[stage.shader->hash] = filter;
        }
        fixes->compatible = [](const std::vector<uint32_t>&, const std::vector<uint32_t>&, VkShaderStageFlagBits, const std::string&, std::string&) { return true; };
        VariantCache cache(handle<VkDevice>(1), dispatch(), 4); cache.native_shaders(fixes);
        std::vector<std::shared_ptr<const PipelineVariant>> held;
        for (int depth : {-1, 0, 1}) {
            FragmentPatch patch; patch.keep_vertex = patch.native_only = true; patch.dynamic_depth = depth;
            auto ready = await(cache, 95, recipe, layout, recipe->vertex, patch);
            const bool expected = depth >= 0 && (filter == 1 ? depth == 1 : depth == 0);
            check(ready.variant && bool(ready.variant->native_replaced) == expected,
                  "dynamic native filtering follows actual view state, never pipeline depth format");
            check(ready.variant && std::find(held.begin(), held.end(), ready.variant) == held.end(),
                  "unknown, absent and present depth contexts have separate cached pipelines");
            held.push_back(ready.variant);
            auto again = await(cache, 95, recipe, layout, recipe->vertex, patch);
            check(again.variant == ready.variant, "same immutable depth context reuses its cached pipeline");
        }
        cache.stop(); held.clear();
        check(driver.count() == 0, "dynamic filtered twins release all retained owners after completion");
    }
}
