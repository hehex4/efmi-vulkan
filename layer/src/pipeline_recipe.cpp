#include "pipeline_recipe.h"
#include "json.h"

#include <algorithm>

namespace efvk {

static uint32_t vertex_format_bytes(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_R8_UNORM: case VK_FORMAT_R8_SNORM: case VK_FORMAT_R8_UINT: case VK_FORMAT_R8_SINT: return 1;
    case VK_FORMAT_R8G8_UNORM: case VK_FORMAT_R8G8_SNORM: case VK_FORMAT_R8G8_UINT: case VK_FORMAT_R8G8_SINT:
    case VK_FORMAT_R16_UNORM: case VK_FORMAT_R16_SNORM: case VK_FORMAT_R16_UINT: case VK_FORMAT_R16_SINT:
    case VK_FORMAT_R16_SFLOAT: return 2;
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SNORM: case VK_FORMAT_R8G8B8A8_UINT: case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_R16G16_UNORM: case VK_FORMAT_R16G16_SNORM: case VK_FORMAT_R16G16_UINT: case VK_FORMAT_R16G16_SINT:
    case VK_FORMAT_R16G16_SFLOAT: case VK_FORMAT_R32_SFLOAT: case VK_FORMAT_R32_UINT: case VK_FORMAT_R32_SINT:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: case VK_FORMAT_A2B10G10R10_UINT_PACK32: return 4;
    case VK_FORMAT_R16G16B16A16_UNORM: case VK_FORMAT_R16G16B16A16_SNORM: case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R16G16B16A16_SINT: case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32_SFLOAT: case VK_FORMAT_R32G32_UINT: case VK_FORMAT_R32G32_SINT: return 8;
    case VK_FORMAT_R32G32B32_SFLOAT: case VK_FORMAT_R32G32B32_UINT: case VK_FORMAT_R32G32B32_SINT: return 12;
    case VK_FORMAT_R32G32B32A32_SFLOAT: case VK_FORMAT_R32G32B32A32_UINT: case VK_FORMAT_R32G32B32A32_SINT: return 16;
    default: return 0;
    }
}

bool patch_vertex_streams(const VertexInputRecipe& source, const std::vector<VertexStreamPatch>& patches,
                          VertexInputRecipe& output, std::string& reason)
{
    reason.clear();
    auto fail = [&](const char* text) { reason = text; return false; };
    if (source.bindings.size() > 256 || source.attributes.size() > 256 || patches.size() > 256)
        return fail("vertex declaration budget exceeded");
    // Validate uniqueness even for preserved streams, before building any replacement.
    for (size_t i = 0; i < source.bindings.size(); ++i)
        for (size_t j = 0; j < i; ++j)
            if (source.bindings[i].binding == source.bindings[j].binding) return fail("duplicate source binding");
    for (size_t i = 0; i < source.attributes.size(); ++i) {
        for (size_t j = 0; j < i; ++j)
            if (source.attributes[i].location == source.attributes[j].location) return fail("duplicate source location");
        if (std::none_of(source.bindings.begin(), source.bindings.end(), [&](const auto& b) {
                return b.binding == source.attributes[i].binding; })) return fail("source attribute binding missing");
    }
    auto next = source;
    for (size_t i = 0; i < patches.size(); ++i) {
        const auto& p = patches[i];
        for (size_t j = 0; j < i; ++j)
            if (p.binding == patches[j].binding) return fail("duplicate stream patch");
        auto b = std::find_if(next.bindings.begin(), next.bindings.end(), [&](const auto& v) { return v.binding == p.binding; });
        if (b == next.bindings.end() || b->stride != p.source_stride || b->inputRate != VK_VERTEX_INPUT_RATE_VERTEX)
            return fail("source stream does not match contract");
        if (!p.source_stride || !p.stride || p.stride > 2048 || p.elements.size() > 256)
            return fail("unsupported replacement stream dimensions");
        for (size_t j = 0; j < p.elements.size(); ++j) {
            const auto& e = p.elements[j];
            for (size_t k = 0; k < j; ++k)
                if (e.source_offset == p.elements[k].source_offset && e.source_format == p.elements[k].source_format)
                    return fail("ambiguous element patch");
            uint32_t from_bytes = vertex_format_bytes(e.source_format), to_bytes = vertex_format_bytes(e.format);
            if (!from_bytes || !to_bytes || uint64_t(e.source_offset) + from_bytes > p.source_stride ||
                uint64_t(e.offset) + to_bytes > p.stride) return fail("unsupported format or element outside stream");
            if (std::none_of(source.attributes.begin(), source.attributes.end(), [&](const auto& a) {
                    return a.binding == p.binding && a.offset == e.source_offset && a.format == e.source_format; }))
                return fail("element patch has no source attribute");
        }
        for (auto& a : next.attributes) {
            if (a.binding != p.binding) continue;
            auto e = std::find_if(p.elements.begin(), p.elements.end(), [&](const auto& v) {
                return v.source_offset == a.offset && v.source_format == a.format; });
            if (e == p.elements.end()) return fail("source element is not covered by stream contract");
            a.offset = e->offset; a.format = e->format;
        }
        b->stride = p.stride;
    }
    output = std::move(next);
    return true;
}

namespace {
// These are retention limits, not Vulkan limits. Larger valid objects keep working unchanged.
constexpr uint32_t kMaxAttachments = 256, kMaxSubpasses = 256, kMaxDependencies = 4096;
constexpr uint32_t kMaxVertex = 256, kMaxViewports = 64, kMaxDynamic = 64;
constexpr size_t kMaxSpecialization = 1u << 20;

template <typename T> void copy_array(std::vector<T>& out, uint32_t count, const T* data)
{
    if (count) out.assign(data, data + count);
}

VkAttachmentReference2 ref2(const VkAttachmentReference& r)
{
    VkAttachmentReference2 out = {VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
    out.attachment = r.attachment;
    out.layout = r.layout;
    return out;
}

std::vector<VkAttachmentReference> refs1(const std::vector<VkAttachmentReference2>& input)
{
    std::vector<VkAttachmentReference> out;
    for (const auto& r : input) out.push_back({r.attachment, r.layout});
    return out;
}

bool good_subpass_counts(uint32_t inputs, uint32_t colors, uint32_t preserves)
{
    return inputs <= kMaxAttachments && colors <= kMaxAttachments && preserves <= kMaxAttachments;
}

bool copy_refs2(std::vector<VkAttachmentReference2>& out, uint32_t count, const VkAttachmentReference2* refs)
{
    if (!refs) return count == 0;
    for (uint32_t i = 0; i < count; i++) {
        if (refs[i].pNext) return false;
        out.push_back(refs[i]);
    }
    return true;
}

bool dynamic_has(const GraphicsRecipe& p, VkDynamicState state)
{
    return std::find(p.dynamic.begin(), p.dynamic.end(), state) != p.dynamic.end();
}

// A lossless encoding also accepts ignored float fields whose bits need not describe finite values.
void float_bits(JsonWriter& w, const char* name, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    w.kn(name, bits);
}

void write_refs(JsonWriter& w, const char* name, const std::vector<VkAttachmentReference2>& refs)
{
    w.key(name); w.begin_array();
    for (const auto& r : refs) {
        w.begin_object(); w.kn("attachment", r.attachment); w.kn("layout", r.layout);
        w.kn("aspect_mask", r.aspectMask); w.end_object();
    }
    w.end_array();
}

void write_stencil(JsonWriter& w, const char* name, const VkStencilOpState& s)
{
    w.key(name); w.begin_object();
    w.kn("fail", s.failOp); w.kn("pass", s.passOp); w.kn("depth_fail", s.depthFailOp);
    w.kn("compare", s.compareOp); w.kn("compare_mask", s.compareMask);
    w.kn("write_mask", s.writeMask); w.kn("reference", s.reference); w.end_object();
}
} // namespace

bool RenderSubpass::uses_color() const
{
    for (const auto& r : colors) if (r.attachment != VK_ATTACHMENT_UNUSED) return true;
    return false;
}

bool RenderSubpass::uses_depth() const
{
    return has_depth && depth.attachment != VK_ATTACHMENT_UNUSED;
}

RenderPassRecipe RenderPassRecipe::capture(const VkRenderPassCreateInfo& info)
{
    RenderPassRecipe r;
    r.flags = info.flags;
    if (info.pNext || info.flags) { r.reason = "render_pass_extension"; return r; }
    if (info.attachmentCount > kMaxAttachments || info.subpassCount > kMaxSubpasses ||
        info.dependencyCount > kMaxDependencies) { r.reason = "render_pass_retention_limit"; return r; }
    for (uint32_t i = 0; i < info.attachmentCount; i++) {
        const auto& a = info.pAttachments[i];
        VkAttachmentDescription2 d = {VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
        d.flags = a.flags; d.format = a.format; d.samples = a.samples; d.loadOp = a.loadOp;
        d.storeOp = a.storeOp; d.stencilLoadOp = a.stencilLoadOp; d.stencilStoreOp = a.stencilStoreOp;
        d.initialLayout = a.initialLayout; d.finalLayout = a.finalLayout;
        r.attachments.push_back(d);
    }
    for (uint32_t i = 0; i < info.subpassCount; i++) {
        const auto& s = info.pSubpasses[i];
        if (s.flags) { r.reason = "subpass_flags"; return r; }
        if (!good_subpass_counts(s.inputAttachmentCount, s.colorAttachmentCount, s.preserveAttachmentCount)) {
            r.reason = "subpass_retention_limit"; return r;
        }
        RenderSubpass d;
        d.flags = s.flags; d.point = s.pipelineBindPoint;
        for (uint32_t j = 0; j < s.inputAttachmentCount; j++) d.inputs.push_back(ref2(s.pInputAttachments[j]));
        for (uint32_t j = 0; j < s.colorAttachmentCount; j++) {
            d.colors.push_back(ref2(s.pColorAttachments[j]));
            if (s.pResolveAttachments) d.resolves.push_back(ref2(s.pResolveAttachments[j]));
        }
        if (s.pDepthStencilAttachment) { d.has_depth = true; d.depth = ref2(*s.pDepthStencilAttachment); }
        copy_array(d.preserves, s.preserveAttachmentCount, s.pPreserveAttachments);
        r.subpasses.push_back(std::move(d));
    }
    for (uint32_t i = 0; i < info.dependencyCount; i++) {
        const auto& s = info.pDependencies[i];
        VkSubpassDependency2 d = {VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2};
        d.srcSubpass = s.srcSubpass; d.dstSubpass = s.dstSubpass; d.srcStageMask = s.srcStageMask;
        d.dstStageMask = s.dstStageMask; d.srcAccessMask = s.srcAccessMask; d.dstAccessMask = s.dstAccessMask;
        d.dependencyFlags = s.dependencyFlags;
        r.dependencies.push_back(d);
    }
    r.complete = true;
    return r;
}

RenderPassRecipe RenderPassRecipe::capture(const VkRenderPassCreateInfo2& info)
{
    RenderPassRecipe r;
    r.version2 = true; r.flags = info.flags;
    if (info.pNext || info.flags) { r.reason = "render_pass_extension"; return r; }
    if (info.attachmentCount > kMaxAttachments || info.subpassCount > kMaxSubpasses ||
        info.dependencyCount > kMaxDependencies || info.correlatedViewMaskCount > kMaxSubpasses) {
        r.reason = "render_pass_retention_limit"; return r;
    }
    for (uint32_t i = 0; i < info.attachmentCount; i++) {
        if (info.pAttachments[i].pNext) { r.reason = "attachment_extension"; return r; }
        r.attachments.push_back(info.pAttachments[i]);
    }
    for (uint32_t i = 0; i < info.subpassCount; i++) {
        const auto& s = info.pSubpasses[i];
        if (s.pNext || s.flags) { r.reason = "subpass_extension"; return r; }
        if (!good_subpass_counts(s.inputAttachmentCount, s.colorAttachmentCount, s.preserveAttachmentCount)) {
            r.reason = "subpass_retention_limit"; return r;
        }
        RenderSubpass d;
        d.flags = s.flags; d.point = s.pipelineBindPoint; d.view_mask = s.viewMask;
        if (!copy_refs2(d.inputs, s.inputAttachmentCount, s.pInputAttachments) ||
            !copy_refs2(d.colors, s.colorAttachmentCount, s.pColorAttachments) ||
            (s.pResolveAttachments && !copy_refs2(d.resolves, s.colorAttachmentCount, s.pResolveAttachments))) {
            r.reason = "attachment_reference_extension"; return r;
        }
        if (s.pDepthStencilAttachment) {
            if (s.pDepthStencilAttachment->pNext) { r.reason = "depth_reference_extension"; return r; }
            d.has_depth = true; d.depth = *s.pDepthStencilAttachment;
        }
        copy_array(d.preserves, s.preserveAttachmentCount, s.pPreserveAttachments);
        r.subpasses.push_back(std::move(d));
    }
    for (uint32_t i = 0; i < info.dependencyCount; i++) {
        if (info.pDependencies[i].pNext) { r.reason = "dependency_extension"; return r; }
        r.dependencies.push_back(info.pDependencies[i]);
    }
    copy_array(r.correlated, info.correlatedViewMaskCount, info.pCorrelatedViewMasks);
    r.complete = true;
    return r;
}

VkResult RenderPassRecipe::create(VkDevice dev, PFN_vkCreateRenderPass create1,
                                  PFN_vkCreateRenderPass2 create2, VkRenderPass* out) const
{
    *out = VK_NULL_HANDLE;
    if (!complete) return VK_ERROR_FEATURE_NOT_PRESENT;
    if (version2) {
        if (!create2) return VK_ERROR_FEATURE_NOT_PRESENT;
        std::vector<VkSubpassDescription2> subs;
        for (const auto& s : subpasses) {
            VkSubpassDescription2 d = {VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2};
            d.flags = s.flags; d.pipelineBindPoint = s.point; d.viewMask = s.view_mask;
            d.inputAttachmentCount = static_cast<uint32_t>(s.inputs.size()); d.pInputAttachments = s.inputs.data();
            d.colorAttachmentCount = static_cast<uint32_t>(s.colors.size()); d.pColorAttachments = s.colors.data();
            d.pResolveAttachments = s.resolves.empty() ? nullptr : s.resolves.data();
            d.pDepthStencilAttachment = s.has_depth ? &s.depth : nullptr;
            d.preserveAttachmentCount = static_cast<uint32_t>(s.preserves.size()); d.pPreserveAttachments = s.preserves.data();
            subs.push_back(d);
        }
        VkRenderPassCreateInfo2 info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};
        info.flags = flags; info.attachmentCount = static_cast<uint32_t>(attachments.size());
        info.pAttachments = attachments.data(); info.subpassCount = static_cast<uint32_t>(subs.size());
        info.pSubpasses = subs.data(); info.dependencyCount = static_cast<uint32_t>(dependencies.size());
        info.pDependencies = dependencies.data(); info.correlatedViewMaskCount = static_cast<uint32_t>(correlated.size());
        info.pCorrelatedViewMasks = correlated.data();
        return create2(dev, &info, nullptr, out);
    }
    if (!create1) return VK_ERROR_FEATURE_NOT_PRESENT;
    std::vector<VkAttachmentDescription> atts;
    for (const auto& a : attachments)
        atts.push_back({a.flags, a.format, a.samples, a.loadOp, a.storeOp, a.stencilLoadOp, a.stencilStoreOp,
                        a.initialLayout, a.finalLayout});
    struct Refs { std::vector<VkAttachmentReference> inputs, colors, resolves; VkAttachmentReference depth; };
    std::vector<Refs> refs(subpasses.size());
    std::vector<VkSubpassDescription> subs;
    for (size_t i = 0; i < subpasses.size(); i++) {
        const auto& s = subpasses[i]; auto& arrays = refs[i];
        arrays.inputs = refs1(s.inputs); arrays.colors = refs1(s.colors); arrays.resolves = refs1(s.resolves);
        arrays.depth = {s.depth.attachment, s.depth.layout};
        VkSubpassDescription d = {};
        d.flags = s.flags; d.pipelineBindPoint = s.point;
        d.inputAttachmentCount = static_cast<uint32_t>(arrays.inputs.size()); d.pInputAttachments = arrays.inputs.data();
        d.colorAttachmentCount = static_cast<uint32_t>(arrays.colors.size()); d.pColorAttachments = arrays.colors.data();
        d.pResolveAttachments = arrays.resolves.empty() ? nullptr : arrays.resolves.data();
        d.pDepthStencilAttachment = s.has_depth ? &arrays.depth : nullptr;
        d.preserveAttachmentCount = static_cast<uint32_t>(s.preserves.size()); d.pPreserveAttachments = s.preserves.data();
        subs.push_back(d);
    }
    std::vector<VkSubpassDependency> deps;
    for (const auto& d : dependencies)
        deps.push_back({d.srcSubpass, d.dstSubpass, d.srcStageMask, d.dstStageMask, d.srcAccessMask,
                       d.dstAccessMask, d.dependencyFlags});
    VkRenderPassCreateInfo info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.flags = flags; info.attachmentCount = static_cast<uint32_t>(atts.size()); info.pAttachments = atts.data();
    info.subpassCount = static_cast<uint32_t>(subs.size()); info.pSubpasses = subs.data();
    info.dependencyCount = static_cast<uint32_t>(deps.size()); info.pDependencies = deps.data();
    return create1(dev, &info, nullptr, out);
}

GraphicsRecipe GraphicsRecipe::capture(const VkGraphicsPipelineCreateInfo& info,
                                       const std::vector<ShaderStage>& shaders,
                                       std::shared_ptr<const RenderPassRecipe> pass)
{
    GraphicsRecipe r;
    r.render_pass = std::move(pass); r.flags = info.flags; r.subpass = info.subpass;
    // Reject libraries and extended dynamic state before touching any possibly ignored pointers.
    if (info.pNext || info.flags) { r.reason = "pipeline_extension_or_flags"; return r; }
    if (!r.render_pass || !r.render_pass->complete) { r.reason = "render_pass_unavailable"; return r; }
    if (info.subpass >= r.render_pass->subpasses.size()) { r.reason = "subpass_unavailable"; return r; }
    if (info.stageCount == 0 || info.stageCount > 2 || shaders.size() != info.stageCount) {
        r.reason = "shader_stages_unsupported"; return r;
    }
    bool vertex = false;
    for (uint32_t i = 0; i < info.stageCount; i++) {
        const auto& s = info.pStages[i];
        if (s.stage != VK_SHADER_STAGE_VERTEX_BIT && s.stage != VK_SHADER_STAGE_FRAGMENT_BIT) {
            r.reason = "shader_stage_unsupported"; return r;
        }
        vertex |= s.stage == VK_SHADER_STAGE_VERTEX_BIT;
        if (s.pNext || s.flags) { r.reason = "shader_stage_extension"; return r; }
        if (!shaders[i].shader || shaders[i].shader->code.empty()) { r.reason = "shader_code_unavailable"; return r; }
        RecipeStage d;
        d.stage = s.stage; d.entry = shaders[i].entry; d.shader = shaders[i].shader;
        if (s.pSpecializationInfo) {
            const auto& spec = *s.pSpecializationInfo;
            if (spec.mapEntryCount > 4096 || spec.dataSize > kMaxSpecialization) {
                r.reason = "specialization_retention_limit"; return r;
            }
            copy_array(d.entries, spec.mapEntryCount, spec.pMapEntries);
            copy_array(d.data, static_cast<uint32_t>(spec.dataSize), static_cast<const uint8_t*>(spec.pData));
        }
        r.stages.push_back(std::move(d));
    }
    if (!vertex) { r.reason = "vertex_stage_missing"; return r; }
    if (info.pDynamicState) {
        const auto& d = *info.pDynamicState;
        if (d.pNext || d.flags || d.dynamicStateCount > kMaxDynamic) { r.reason = "dynamic_state_extension"; return r; }
        for (uint32_t i = 0; i < d.dynamicStateCount; i++) {
            if (d.pDynamicStates[i] < VK_DYNAMIC_STATE_VIEWPORT || d.pDynamicStates[i] > VK_DYNAMIC_STATE_STENCIL_REFERENCE) {
                r.reason = "extended_dynamic_state"; return r;
            }
            r.dynamic.push_back(d.pDynamicStates[i]);
        }
    }
    if (!info.pVertexInputState || !info.pInputAssemblyState || !info.pRasterizationState) {
        r.reason = "required_state_missing"; return r;
    }
    const auto& vi = *info.pVertexInputState;
    if (vi.pNext || vi.flags || vi.vertexBindingDescriptionCount > kMaxVertex || vi.vertexAttributeDescriptionCount > kMaxVertex) {
        r.reason = "vertex_input_extension_or_limit"; return r;
    }
    copy_array(r.vertex.bindings, vi.vertexBindingDescriptionCount, vi.pVertexBindingDescriptions);
    copy_array(r.vertex.attributes, vi.vertexAttributeDescriptionCount, vi.pVertexAttributeDescriptions);
    if (info.pInputAssemblyState->pNext || info.pInputAssemblyState->flags) { r.reason = "assembly_extension"; return r; }
    r.assembly = *info.pInputAssemblyState;
    if (info.pRasterizationState->pNext || info.pRasterizationState->flags) { r.reason = "raster_extension"; return r; }
    r.raster = *info.pRasterizationState;
    // No tessellation stages: pTessellationState is ignored and must not be dereferenced.
    if (!r.raster.rasterizerDiscardEnable) {
        if (!info.pViewportState || !info.pMultisampleState) { r.reason = "raster_state_missing"; return r; }
        const auto& vp = *info.pViewportState;
        if (vp.pNext || vp.flags || vp.viewportCount > kMaxViewports || vp.scissorCount > kMaxViewports) {
            r.reason = "viewport_extension_or_limit"; return r;
        }
        r.viewport_count = vp.viewportCount; r.scissor_count = vp.scissorCount;
        if (!dynamic_has(r, VK_DYNAMIC_STATE_VIEWPORT)) copy_array(r.viewports, vp.viewportCount, vp.pViewports);
        if (!dynamic_has(r, VK_DYNAMIC_STATE_SCISSOR)) copy_array(r.scissors, vp.scissorCount, vp.pScissors);
        const auto& ms = *info.pMultisampleState;
        if (ms.pNext || ms.flags) { r.reason = "multisample_extension"; return r; }
        r.multisample = ms; r.multisample.pSampleMask = nullptr;
        if (ms.pSampleMask) copy_array(r.sample_mask, (static_cast<uint32_t>(ms.rasterizationSamples) + 31) / 32, ms.pSampleMask);
        const auto& sub = r.render_pass->subpasses[info.subpass];
        if (sub.uses_depth()) {
            if (!info.pDepthStencilState) { r.reason = "depth_state_missing"; return r; }
            if (info.pDepthStencilState->pNext || info.pDepthStencilState->flags) { r.reason = "depth_extension"; return r; }
            r.has_depth = true; r.depth = *info.pDepthStencilState;
        }
        if (sub.uses_color()) {
            if (!info.pColorBlendState) { r.reason = "blend_state_missing"; return r; }
            const auto& cb = *info.pColorBlendState;
            if (cb.pNext || cb.flags || cb.attachmentCount > kMaxAttachments) { r.reason = "blend_extension_or_limit"; return r; }
            r.has_blend = true; r.blend = cb; r.blend.pAttachments = nullptr;
            copy_array(r.blend_attachments, cb.attachmentCount, cb.pAttachments);
        }
    }
    r.complete = true;
    return r;
}

VkResult GraphicsRecipe::create(VkDevice dev, PFN_vkCreateGraphicsPipelines create_fn,
                                VkPipelineLayout layout, VkRenderPass pass,
                                const std::vector<VkShaderModule>& modules, VkPipeline* out,
                                const VertexInputRecipe* override_vertex) const
{
    *out = VK_NULL_HANDLE;
    if (!complete || !create_fn || !layout || !pass || modules.size() != stages.size())
        return VK_ERROR_FEATURE_NOT_PRESENT;
    std::vector<VkSpecializationInfo> specs(stages.size());
    std::vector<VkPipelineShaderStageCreateInfo> ss;
    for (size_t i = 0; i < stages.size(); i++) {
        if (!modules[i]) return VK_ERROR_FEATURE_NOT_PRESENT;
        const auto& s = stages[i]; auto& spec = specs[i];
        spec.mapEntryCount = static_cast<uint32_t>(s.entries.size()); spec.pMapEntries = s.entries.data();
        spec.dataSize = s.data.size(); spec.pData = s.data.data();
        VkPipelineShaderStageCreateInfo stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.stage = s.stage; stage.module = modules[i]; stage.pName = s.entry.c_str();
        if (!s.entries.empty()) stage.pSpecializationInfo = &spec;
        ss.push_back(stage);
    }
    const auto& v = override_vertex ? *override_vertex : vertex;
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = static_cast<uint32_t>(v.bindings.size()); vi.pVertexBindingDescriptions = v.bindings.data();
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(v.attributes.size()); vi.pVertexAttributeDescriptions = v.attributes.data();
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = viewport_count; vp.scissorCount = scissor_count;
    vp.pViewports = viewports.empty() ? nullptr : viewports.data(); vp.pScissors = scissors.empty() ? nullptr : scissors.data();
    VkPipelineMultisampleStateCreateInfo ms = multisample;
    ms.pSampleMask = sample_mask.empty() ? nullptr : sample_mask.data();
    VkPipelineColorBlendStateCreateInfo cb = blend;
    cb.pAttachments = blend_attachments.data();
    VkPipelineDynamicStateCreateInfo dyn = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = static_cast<uint32_t>(dynamic.size()); dyn.pDynamicStates = dynamic.data();
    VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.flags = flags; info.stageCount = static_cast<uint32_t>(ss.size()); info.pStages = ss.data();
    info.pVertexInputState = &vi; info.pInputAssemblyState = &assembly; info.pRasterizationState = &raster;
    info.pViewportState = raster.rasterizerDiscardEnable ? nullptr : &vp;
    info.pMultisampleState = raster.rasterizerDiscardEnable ? nullptr : &ms;
    info.pDepthStencilState = has_depth ? &depth : nullptr; info.pColorBlendState = has_blend ? &cb : nullptr;
    info.pDynamicState = dynamic.empty() ? nullptr : &dyn;
    info.layout = layout; info.renderPass = pass; info.subpass = subpass; info.basePipelineIndex = -1;
    return create_fn(dev, VK_NULL_HANDLE, 1, &info, nullptr, out);
}

void RenderPassRecipe::write(JsonWriter& w) const
{
    w.begin_object(); w.kn("id", id); w.kb("complete", complete); w.kv("reason", reason);
    w.kn("version", version2 ? 2 : 1); w.kn("flags", flags);
    w.key("attachments"); w.begin_array();
    for (const auto& a : attachments) {
        w.begin_object(); w.kn("flags", a.flags); w.kn("format", a.format); w.kn("samples", a.samples);
        w.kn("load", a.loadOp); w.kn("store", a.storeOp); w.kn("stencil_load", a.stencilLoadOp);
        w.kn("stencil_store", a.stencilStoreOp); w.kn("initial_layout", a.initialLayout); w.kn("final_layout", a.finalLayout);
        w.end_object();
    }
    w.end_array(); w.key("subpasses"); w.begin_array();
    for (const auto& s : subpasses) {
        w.begin_object(); w.kn("flags", s.flags); w.kn("bind_point", s.point); w.kn("view_mask", s.view_mask);
        write_refs(w, "inputs", s.inputs); write_refs(w, "colors", s.colors); write_refs(w, "resolves", s.resolves);
        write_refs(w, "depth", s.has_depth ? std::vector<VkAttachmentReference2>{s.depth} : std::vector<VkAttachmentReference2>{});
        w.key("preserves"); w.begin_array(); for (uint32_t a : s.preserves) w.num(a); w.end_array(); w.end_object();
    }
    w.end_array(); w.key("dependencies"); w.begin_array();
    for (const auto& d : dependencies) {
        w.begin_object(); w.kn("src_subpass", d.srcSubpass); w.kn("dst_subpass", d.dstSubpass);
        w.kn("src_stage", d.srcStageMask); w.kn("dst_stage", d.dstStageMask);
        w.kn("src_access", d.srcAccessMask); w.kn("dst_access", d.dstAccessMask);
        w.kn("flags", d.dependencyFlags); w.ki("view_offset", d.viewOffset); w.end_object();
    }
    w.end_array(); w.key("correlated_view_masks"); w.begin_array();
    for (uint32_t mask : correlated) w.num(mask);
    w.end_array(); w.end_object();
}

void GraphicsRecipe::write(JsonWriter& w) const
{
    w.begin_object(); w.kb("complete", complete); w.kv("reason", reason); w.kv("float_encoding", "ieee754_bits");
    w.key("render_pass"); if (render_pass) render_pass->write(w); else w.null();
    // Partial fields must never masquerade as a usable recipe.
    if (!complete) { w.end_object(); return; }
    w.kn("flags", flags); w.kn("subpass", subpass);
    w.key("stages"); w.begin_array();
    for (const auto& s : stages) {
        w.begin_object(); w.kn("stage", s.stage); w.kv("entry", s.entry); w.kn("shader", s.shader->id);
        w.key("specialization_entries"); w.begin_array();
        for (const auto& e : s.entries) {
            w.begin_object(); w.kn("id", e.constantID); w.kn("offset", e.offset); w.kn("size", e.size); w.end_object();
        }
        w.end_array(); w.key("specialization_bytes"); w.begin_array();
        for (uint8_t b : s.data) w.num(b);
        w.end_array(); w.end_object();
    }
    w.end_array();
    w.key("assembly"); w.begin_object(); w.kn("topology", assembly.topology);
    w.kb("primitive_restart", assembly.primitiveRestartEnable != 0); w.end_object();
    w.key("raster"); w.begin_object(); w.kb("depth_clamp", raster.depthClampEnable != 0);
    w.kb("discard", raster.rasterizerDiscardEnable != 0); w.kn("polygon", raster.polygonMode);
    w.kn("cull", raster.cullMode); w.kn("front_face", raster.frontFace);
    w.kb("depth_bias", raster.depthBiasEnable != 0);
    float_bits(w, "bias_constant_bits", raster.depthBiasConstantFactor); float_bits(w, "bias_clamp_bits", raster.depthBiasClamp);
    float_bits(w, "bias_slope_bits", raster.depthBiasSlopeFactor); float_bits(w, "line_width_bits", raster.lineWidth); w.end_object();
    w.kn("viewport_count", viewport_count); w.kn("scissor_count", scissor_count);
    w.key("viewports"); w.begin_array();
    for (const auto& v : viewports) {
        w.begin_object(); float_bits(w, "x_bits", v.x); float_bits(w, "y_bits", v.y);
        float_bits(w, "width_bits", v.width); float_bits(w, "height_bits", v.height);
        float_bits(w, "min_depth_bits", v.minDepth); float_bits(w, "max_depth_bits", v.maxDepth); w.end_object();
    }
    w.end_array(); w.key("scissors"); w.begin_array();
    for (const auto& s : scissors) {
        w.begin_object(); w.ki("x", s.offset.x); w.ki("y", s.offset.y);
        w.kn("width", s.extent.width); w.kn("height", s.extent.height); w.end_object();
    }
    w.end_array(); w.key("multisample");
    if (raster.rasterizerDiscardEnable) w.null();
    else {
        w.begin_object(); w.kn("samples", multisample.rasterizationSamples);
        w.kb("sample_shading", multisample.sampleShadingEnable != 0);
        float_bits(w, "min_sample_shading_bits", multisample.minSampleShading);
        w.kb("alpha_to_coverage", multisample.alphaToCoverageEnable != 0);
        w.kb("alpha_to_one", multisample.alphaToOneEnable != 0);
        w.key("mask"); w.begin_array(); for (uint32_t m : sample_mask) w.num(m); w.end_array(); w.end_object();
    }
    w.key("depth_stencil");
    if (!has_depth) w.null();
    else {
        w.begin_object(); w.kb("test", depth.depthTestEnable != 0); w.kb("write", depth.depthWriteEnable != 0);
        w.kn("compare", depth.depthCompareOp); w.kb("bounds_test", depth.depthBoundsTestEnable != 0);
        w.kb("stencil_test", depth.stencilTestEnable != 0);
        write_stencil(w, "front", depth.front); write_stencil(w, "back", depth.back);
        float_bits(w, "min_bounds_bits", depth.minDepthBounds); float_bits(w, "max_bounds_bits", depth.maxDepthBounds); w.end_object();
    }
    w.key("blend");
    if (!has_blend) w.null();
    else {
        w.begin_object(); w.kb("logic_enable", blend.logicOpEnable != 0); w.kn("logic_op", blend.logicOp);
        w.key("constants_bits"); w.begin_array();
        for (float v : blend.blendConstants) { uint32_t b; memcpy(&b, &v, 4); w.num(b); }
        w.end_array(); w.key("attachments"); w.begin_array();
        for (const auto& a : blend_attachments) {
            w.begin_object(); w.kb("enable", a.blendEnable != 0); w.kn("src_color", a.srcColorBlendFactor);
            w.kn("dst_color", a.dstColorBlendFactor); w.kn("color_op", a.colorBlendOp);
            w.kn("src_alpha", a.srcAlphaBlendFactor); w.kn("dst_alpha", a.dstAlphaBlendFactor);
            w.kn("alpha_op", a.alphaBlendOp); w.kn("write_mask", a.colorWriteMask); w.end_object();
        }
        w.end_array(); w.end_object();
    }
    w.end_object();
}

} // namespace efvk
