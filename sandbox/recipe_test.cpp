// CPU tests for ownership, ignored pointers, fail-closed rewrites and partial GPU-build cleanup.
#include "variant_fixture.h"
#include "../layer/src/state.h"
#include "../layer/src/json.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>

using namespace efvk;
using variant_fixture::RecipeFixture;
static int passed = 0, failed = 0;
static void check(bool ok, const char* label)
{
    if (ok) passed++; else { failed++; printf("FAIL: %s\n", label); }
}
template <typename T> static T h(uintptr_t v) { return reinterpret_cast<T>(v); }

static int allocation = 0, fail_at = 0;
static uintptr_t next_handle = 100;
static std::set<uintptr_t> live;
static bool original_fragment = false, vertex_rewritten = false;
template <typename T> static VkResult allocate(T* out)
{
    *out = VK_NULL_HANDLE;
    if (++allocation == fail_at) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *out = h<T>(++next_handle); live.insert(next_handle); return VK_SUCCESS;
}
template <typename T> static void release(T handle)
{
    check(live.erase(reinterpret_cast<uintptr_t>(handle)) == 1, "each private handle destroyed exactly once");
}
static VKAPI_ATTR VkResult VKAPI_CALL create_set(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout* out) { return allocate(out); }
static VKAPI_ATTR void VKAPI_CALL destroy_set(VkDevice, VkDescriptorSetLayout s, const VkAllocationCallbacks*) { release(s); }
static VKAPI_ATTR VkResult VKAPI_CALL create_layout(VkDevice, const VkPipelineLayoutCreateInfo* p, const VkAllocationCallbacks*, VkPipelineLayout* out)
{
    check(p->setLayoutCount == 4 && p->pSetLayouts[3], "variant appends exactly one own set"); return allocate(out);
}
static VKAPI_ATTR void VKAPI_CALL destroy_layout(VkDevice, VkPipelineLayout p, const VkAllocationCallbacks*) { release(p); }
static VKAPI_ATTR VkResult VKAPI_CALL create_pass(VkDevice, const VkRenderPassCreateInfo* p, const VkAllocationCallbacks*, VkRenderPass* out)
{
    check(p->attachmentCount == 2 && p->pSubpasses[0].pDepthStencilAttachment->attachment == 1 &&
          p->pDependencies[1].dstStageMask == VK_PIPELINE_STAGE_TRANSFER_BIT, "render pass arrays survive their source");
    return allocate(out);
}
static VKAPI_ATTR void VKAPI_CALL destroy_pass(VkDevice, VkRenderPass p, const VkAllocationCallbacks*) { release(p); }
static VKAPI_ATTR VkResult VKAPI_CALL create_shader(VkDevice, const VkShaderModuleCreateInfo* p, const VkAllocationCallbacks*, VkShaderModule* out)
{
    std::vector<uint32_t> code(p->pCode, p->pCode + p->codeSize / 4);
    original_fragment |= code == variant_fixture::shader(false);
    VertexRepoint r; repoint_vertex(variant_fixture::shader(true), 3, r);
    vertex_rewritten |= code == r.words;
    return allocate(out);
}
static VKAPI_ATTR void VKAPI_CALL destroy_shader(VkDevice, VkShaderModule p, const VkAllocationCallbacks*) { release(p); }
static VKAPI_ATTR VkResult VKAPI_CALL create_pipe(VkDevice, VkPipelineCache, uint32_t count, const VkGraphicsPipelineCreateInfo* p,
                                                const VkAllocationCallbacks*, VkPipeline* out)
{
    check(count == 1 && p->basePipelineIndex == -1 && p->pStages[0].pName && !strcmp(p->pStages[0].pName, "main"), "creation owns stage entries");
    check(p->pVertexInputState->pVertexBindingDescriptions[0].stride == 16 &&
          p->pVertexInputState->pVertexAttributeDescriptions[1].format == VK_FORMAT_R16G16B16A16_UINT, "variant changes stride and integer format");
    check(p->pDepthStencilState->depthCompareOp == VK_COMPARE_OP_LESS &&
          p->pColorBlendState->pAttachments[0].dstColorBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
          "depth and blending are reproduced");
    check(p->pViewportState->pViewports == nullptr && p->pViewportState->pScissors == nullptr &&
          p->pDynamicState->dynamicStateCount == 2, "ignored dynamic arrays are not borrowed");
    // Vulkan can return a live pipeline along with failure from a batch. It must still be destroyed.
    const bool partial_failure = allocation + 1 == fail_at;
    const int saved = fail_at; if (partial_failure) fail_at = 0;
    VkResult result = allocate(out); fail_at = saved;
    return partial_failure ? VK_ERROR_OUT_OF_DEVICE_MEMORY : result;
}
static VKAPI_ATTR void VKAPI_CALL destroy_pipe(VkDevice, VkPipeline p, const VkAllocationCallbacks*) { release(p); }

static VariantDispatch dispatch()
{
    VariantDispatch d;
    d.CreateDescriptorSetLayout = create_set; d.DestroyDescriptorSetLayout = destroy_set;
    d.CreatePipelineLayout = create_layout; d.DestroyPipelineLayout = destroy_layout;
    d.CreateRenderPass = create_pass; d.DestroyRenderPass = destroy_pass;
    d.CreateShaderModule = create_shader; d.DestroyShaderModule = destroy_shader;
    d.CreateGraphicsPipelines = create_pipe; d.DestroyPipeline = destroy_pipe;
    return d;
}

static void rewrite_tests()
{
    auto original = variant_fixture::shader(true);
    VertexRepoint out;
    check(repoint_vertex(original, 3, out) && out.bone_set == 0 && out.bone_binding == 7, "locate the two unnamed vertex buffers");
    size_t changed = 0;
    for (size_t i = 0; i < original.size(); i++) changed += original[i] != out.words[i];
    check(changed == 4, "rewrite changes exactly four decoration literals");
    auto fragment = variant_fixture::shader(false);
    check(!repoint_vertex(fragment, 3, out) && out.words.empty(), "reject fragment rewrite");
    check(!repoint_vertex(original, 2, out) && out.reason == "spirv_target_collision", "reject occupied target binding");
    auto broken = original; broken[5] = 0;
    check(!repoint_vertex(broken, 3, out), "reject zero instruction size");
    broken = original; broken.pop_back(); broken.push_back(0xffff0001);
    check(!repoint_vertex(broken, 3, out), "reject instruction past module end");
    broken = original; broken.push_back((2u << 16) | 73); broken.push_back(1);
    check(!repoint_vertex(broken, 3, out) && out.reason == "spirv_indirect_decoration", "reject grouped decorations without guessing");
    for (size_t i = 5; i < original.size(); i += original[i] >> 16) {
        if ((original[i] & 0xffff) == 71 && original[i + 2] == 34) {
            broken = original; broken.insert(broken.end(), original.begin() + i, original.begin() + i + 4); break;
        }
    }
    check(!repoint_vertex(broken, 3, out) && out.reason == "spirv_duplicate_decoration", "reject duplicate descriptor decorations");
}

static void ownership_tests()
{
    GraphicsRecipe owned;
    PipelineLayoutInfo layout;
    {
        RecipeFixture f;
        owned = f.capture(); layout = f.layout;
        check(owned.complete, "capture the complete fixture");
        f.atts[0].format = VK_FORMAT_UNDEFINED; f.ba.blendEnable = false; f.binding.stride = 123;
        f.dynamic[0] = VK_DYNAMIC_STATE_LINE_WIDTH;
    }
    auto copied = owned; owned = GraphicsRecipe(); auto moved = std::move(copied);
    check(moved.render_pass->attachments[0].format == VK_FORMAT_R8G8B8A8_UNORM && moved.blend_attachments[0].blendEnable &&
          moved.vertex.bindings[0].stride == 12 && moved.dynamic[0] == VK_DYNAMIC_STATE_VIEWPORT,
          "nested state survives source destruction plus recipe copy/move");
    for (int failure = 1; failure <= 10; failure++) {
        allocation = 0; fail_at = failure == 10 ? 0 : failure;
        {
            PipelineVariant variant(h<VkDevice>(2), dispatch());
            bool ok = variant.build(moved, layout, RecipeFixture::mod_vertex(), 4);
            check(ok == (failure == 10), "variant reports every injected allocation failure");
            if (!ok) check(!variant.pipeline && !variant.layout && live.empty(), "failed build publishes no resources");
        }
        check(live.empty(), "build/destruction releases all private resources");
    }
    check(original_fragment && vertex_rewritten, "builder passes unchanged FS and rewritten VS to the driver");
    RecipeFixture f;
    VkSpecializationMapEntry entry = {7, 1, 2}; uint8_t bytes[4] = {5, 6, 7, 8};
    VkSpecializationInfo spec = {1, &entry, sizeof(bytes), bytes}; f.stages[0].pSpecializationInfo = &spec;
    auto specialized = f.capture(); entry.offset = 0; bytes[1] = 0;
    check(specialized.stages[0].entries[0].offset == 1 && specialized.stages[0].data[1] == 6,
          "specialization offsets and bytes are owned");
    VkViewport vp = {3, 4, 55, 66, .1f, .9f}; VkRect2D sc = {{7, 8}, {55, 66}};
    f.gp.pDynamicState = nullptr; f.vp.pViewports = &vp; f.vp.pScissors = &sc;
    VkSampleMask mask[2] = {0x1234, 0x9876}; f.ms.rasterizationSamples = VK_SAMPLE_COUNT_64_BIT; f.ms.pSampleMask = mask;
    auto stat = f.capture(); vp.width = 0; sc.offset.x = 0; mask[1] = 0;
    check(stat.viewports[0].width == 55 && stat.scissors[0].offset.x == 7 && stat.sample_mask[1] == 0x9876,
          "static viewport/scissor and two-word sample mask are owned");
    JsonWriter w; moved.write(w);
    check(w.out.find("\"complete\":true") != std::string::npos && w.out.find("\"line_width_bits\":1065353216") != std::string::npos,
          "recipe export preserves floats losslessly");
}

static void ignored_pointer_tests()
{
    RecipeFixture f;
    f.gp.pTessellationState = h<const VkPipelineTessellationStateCreateInfo*>(1);
    f.vp.pViewports = h<const VkViewport*>(1); f.vp.pScissors = h<const VkRect2D*>(1);
    check(f.capture().complete, "never dereference ignored tessellation or dynamic viewport arrays");
    f.sub.pDepthStencilAttachment = nullptr; f.gp.pDepthStencilState = h<const VkPipelineDepthStencilStateCreateInfo*>(1);
    check(f.capture().complete && !f.capture().has_depth, "ignore depth state without a depth attachment");
    f.color.attachment = VK_ATTACHMENT_UNUSED; f.gp.pColorBlendState = h<const VkPipelineColorBlendStateCreateInfo*>(1);
    check(f.capture().complete && !f.capture().has_blend, "ignore blend state when every color slot is unused");
    f.rs.rasterizerDiscardEnable = VK_TRUE; f.gp.pViewportState = h<const VkPipelineViewportStateCreateInfo*>(1);
    f.gp.pMultisampleState = h<const VkPipelineMultisampleStateCreateInfo*>(1);
    check(f.capture().complete, "discarded rasterization ignores viewport and multisample pointers");
    f.dynamic[0] = VK_DYNAMIC_STATE_VERTEX_INPUT_EXT; f.gp.pVertexInputState = h<const VkPipelineVertexInputStateCreateInfo*>(1);
    check(!f.capture().complete && f.capture().reason == "extended_dynamic_state", "reject extended dynamics before reading ignored structs");
    f.gp.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR; f.gp.pStages = h<const VkPipelineShaderStageCreateInfo*>(1);
    check(!f.capture().complete, "reject pipeline library before touching absent stage state");
    VkRenderPassCreateInfo2 r2 = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};
    VkAttachmentReference2 ar = {VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2}; ar.attachment = VK_ATTACHMENT_UNUSED;
    VkSubpassDescription2 s2 = {VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2}; s2.colorAttachmentCount = 1; s2.pColorAttachments = &ar;
    s2.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; s2.viewMask = 3;
    uint32_t mask = 3; r2.subpassCount = 1; r2.pSubpasses = &s2; r2.correlatedViewMaskCount = 1; r2.pCorrelatedViewMasks = &mask;
    auto rp2 = RenderPassRecipe::capture(r2); mask = 0; s2.viewMask = 0;
    check(rp2.complete && rp2.version2 && rp2.correlated[0] == 3 && rp2.subpasses[0].view_mask == 3,
          "render pass 2 multiview state is owned");
    ar.pNext = h<const void*>(1);
    check(!RenderPassRecipe::capture(r2).complete, "unknown attachment extension is not dropped");
}

static void tracked_lifetime_tests()
{
    Device d; RecipeFixture f;
    auto rp = h<VkRenderPass>(10); auto pipe = h<VkPipeline>(11);
    on_create_render_pass(&d, rp, &f.rp); f.gp.renderPass = rp;
    for (uint32_t i = 0; i < 2; i++) {
        VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = f.shaders[i].shader->code.size() * 4; ci.pCode = f.shaders[i].shader->code.data();
        f.stages[i].module = h<VkShaderModule>(20 + i); on_create_shader(&d, f.stages[i].module, &ci);
    }
    on_create_graphics_pipeline(&d, pipe, &f.gp);
    auto recipe = d.pipes.at(pipe).recipe;
    uint32_t old_id = recipe->render_pass->id;
    on_destroy_render_pass(&d, rp); on_create_render_pass(&d, rp, &f.rp);
    for (auto& stage : f.stages) on_destroy_shader(&d, stage.module);
    check(recipe->complete && recipe->render_pass->id == old_id && d.render_passes.at(rp)->id != old_id,
          "pipeline retains the old render pass when an application handle is reused");
    check(d.shaders.empty() && d.shader_bytes->load() > 0, "shader bytes survive module destruction");
    on_destroy_pipeline(&d, pipe); recipe.reset();
    check(d.shader_bytes->load() == 0, "recipe shares the shader budget and returns it on final release");
    const bool saved = g_cfg.track_resources; g_cfg.track_resources = false;
    on_create_render_pass(&d, h<VkRenderPass>(30), &f.rp);
    check(d.render_passes.count(h<VkRenderPass>(30)) == 0, "resources off does not retain recipes");
    g_cfg.track_resources = saved;
}

int main(int argc, char** argv)
{
    // Cross-check the production rewriter on previously exported, local SPIR-V modules.
    if (argc == 4 && !strcmp(argv[1], "--repoint-file")) {
        std::ifstream input(argv[2], std::ios::binary | std::ios::ate);
        if (!input) return 2;
        auto size = input.tellg();
        if (size < 20 || size > (64 << 20) || static_cast<size_t>(size) % 4) return 2;
        std::vector<uint32_t> words(static_cast<size_t>(size) / 4);
        input.seekg(0); input.read(reinterpret_cast<char*>(words.data()), size);
        if (!input) return 2;
        VertexRepoint out;
        if (!repoint_vertex(words, 3, out)) { printf("repoint rejected: %s\n", out.reason.c_str()); return 1; }
        std::ofstream output(argv[3], std::ios::binary);
        output.write(reinterpret_cast<const char*>(out.words.data()), out.words.size() * 4);
        return output ? 0 : 2;
    }
    util_init(GetModuleHandleW(nullptr));
    rewrite_tests(); ownership_tests(); ignored_pointer_tests(); tracked_lifetime_tests();
    printf("recipe_test: %d/%d checks passed\n", passed, passed + failed);
    return failed ? 1 : 0;
}
