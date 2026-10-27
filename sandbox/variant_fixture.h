// A small shared VS/FS buffer fixture with intentionally incompatible original/mod vertex formats.
#pragma once

#include "src/spirv.h"
#include "../layer/src/pipeline_variant.h"

namespace variant_fixture {

// Both stages read the same bone block (set 0 binding 7) and instance block (set 2 binding 0).
// VS: position + bones.offset[blend.x] + instance.offset; FS: bones.tint * instance.color.
// Original blend.x is 1 (RGBA8_UINT); replacement is 257 (RGBA16_UINT), beyond an 8-bit index.
// `image_binding`: where the material fragment shader declares its image in set 1. Another
// number gives other bytes and another place for the same texture, as a feature variant of a
// game shader has.
// A fragment kill before the output (D-133): the character dither (interleaved gradient noise against
// the instance window at set 2 binding 0), an alpha test (a value of set 0), a noise test without the
// instance window (another fade).
enum class Kill { None, Dither, Cutout, Noise };
inline std::vector<uint32_t> shader(bool vertex,bool material=false,uint32_t image_binding=6,Kill kill=Kill::None)
{
    using namespace spv;
    Builder b;
    auto main = b.id(), pos_in = b.id(), blend_in = b.id(), output = b.id(), pv = b.id();
    auto bone_var = b.id(), instance_var = b.id(), bone_type = b.id(), instance_type = b.id();
    auto t_void = b.id(), t_fn = b.id(), t_f = b.id(), t_v2 = b.id(), t_v4 = b.id(), t_u = b.id(), t_u4 = b.id();
    auto array = b.id(), len = b.id(), c0 = b.id(), c1 = b.id(), f0 = b.id(), f1 = b.id();
    auto p_in_v2 = b.id(), p_in_u4 = b.id(), p_out = b.id(), p_out_v4 = b.id(), p_bone = b.id(), p_instance = b.id();
    auto p_uni_v4 = b.id(), label = b.id(), ptr_a = b.id(), ptr_b = b.id(), a = b.id(), v = b.id(), sum = b.id();
    auto in_pos = b.id(), x = b.id(), y = b.id(), position = b.id(), blend = b.id(), bone = b.id(), moved = b.id(), out_ptr = b.id();
    auto image_type=b.id(),image_ptr=b.id(),image_var=b.id(),image=b.id(),coord=b.id(),texel=b.id(),t_u2=b.id(),tinted=b.id();
    b.op(OpCapability, {CapabilityShader});
    b.op(OpMemoryModel, {AddressingLogical, MemoryGLSL450});
    if (vertex) b.entry_point(ModelVertex, main, "main", {pos_in, blend_in, output});
    else { b.entry_point(ModelFragment, main, "main", {output}); b.op(OpExecutionMode, {main, ModeOriginUpperLeft}); }
    b.op(OpDecorate, {bone_var, DecorationDescriptorSet, 0});
    b.op(OpDecorate, {bone_var, DecorationBinding, 7});
    b.op(OpDecorate, {instance_var, DecorationDescriptorSet, 2});
    b.op(OpDecorate, {instance_var, DecorationBinding, 0});
    if(material && !vertex){b.op(OpDecorate,{image_var,DecorationDescriptorSet,1});b.op(OpDecorate,{image_var,DecorationBinding,image_binding});}
    b.op(OpDecorate, {bone_type, DecorationBufferBlock});
    b.op(OpMemberDecorate, {bone_type, 0, DecorationOffset, 0});
    b.op(OpMemberDecorate, {bone_type, 1, DecorationOffset, 258 * 16});
    b.op(OpMemberDecorate, {bone_type, 0, DecorationNonWritable});
    b.op(OpMemberDecorate, {bone_type, 1, DecorationNonWritable});
    b.op(OpDecorate, {array, 6, 16}); // ArrayStride
    b.op(OpDecorate, {instance_type, DecorationBlock});
    b.op(OpMemberDecorate, {instance_type, 0, DecorationOffset, 0});
    b.op(OpMemberDecorate, {instance_type, 1, DecorationOffset, 16});
    if (vertex) {
        b.op(OpDecorate, {pos_in, DecorationLocation, 0}); b.op(OpDecorate, {blend_in, DecorationLocation, 1});
        b.op(OpMemberDecorate, {pv, 0, DecorationBuiltIn, BuiltInPosition}); b.op(OpDecorate, {pv, DecorationBlock});
    } else b.op(OpDecorate, {output, DecorationLocation, 0});
    b.op(OpTypeVoid, {t_void}); b.op(OpTypeFunction, {t_fn, t_void}); b.op(OpTypeFloat, {t_f, 32});
    b.op(OpTypeInt, {t_u, 32, 0}); b.op(OpTypeVector, {t_u4, t_u, 4});
    b.op(OpTypeVector, {t_v2, t_f, 2}); b.op(OpTypeVector, {t_v4, t_f, 4});
    b.op(OpConstant, {t_u, c0, 0}); b.op(OpConstant, {t_u, c1, 1}); b.op(OpConstant, {t_u, len, 258});
    b.op(OpConstant, {t_f, f0, 0}); b.op(OpConstant, {t_f, f1, 0x3f800000});
    b.op(OpTypeArray, {array, t_v4, len}); b.op(OpTypeStruct, {bone_type, array, t_v4});
    b.op(OpTypeStruct, {instance_type, t_v4, t_v4});
    b.op(OpTypePointer, {p_bone, StorageUniform, bone_type}); b.op(OpTypePointer, {p_instance, StorageUniform, instance_type});
    b.op(OpTypePointer, {p_uni_v4, StorageUniform, t_v4}); b.op(OpTypePointer, {p_out_v4, StorageOutput, t_v4});
    if(material && !vertex){
        b.op(OpTypeImage,{image_type,t_f,1,2,0,0,1,0});b.op(OpTypePointer,{image_ptr,StorageUniformConstant,image_type});
        b.op(OpTypeVector,{t_u2,t_u,2});b.op(OpConstantComposite,{t_u2,coord,c0,c0});
        b.op(OpVariable,{image_ptr,image_var,StorageUniformConstant});
    }
    if (vertex) {
        b.op(OpTypeStruct, {pv, t_v4}); b.op(OpTypePointer, {p_out, StorageOutput, pv});
        b.op(OpTypePointer, {p_in_v2, StorageInput, t_v2}); b.op(OpTypePointer, {p_in_u4, StorageInput, t_u4});
        b.op(OpVariable, {p_in_v2, pos_in, StorageInput}); b.op(OpVariable, {p_in_u4, blend_in, StorageInput});
        b.op(OpVariable, {p_out, output, StorageOutput});
    } else b.op(OpVariable, {p_out_v4, output, StorageOutput});
    b.op(OpVariable, {p_bone, bone_var, StorageUniform}); b.op(OpVariable, {p_instance, instance_var, StorageUniform});
    // The kill's types and constants: bool, the noise factor, one half.
    auto t_bool = b.id(), k_noise = b.id(), k_half = b.id();
    if (!vertex && kill != Kill::None) {
        b.op(static_cast<Op>(20), {t_bool});
        b.op(OpConstant, {t_f, k_noise, 0x4253EE82u}); // 52.9829189f
        b.op(OpConstant, {t_f, k_half, 0x3f000000u});
    }
    b.op(OpFunction, {t_void, main, 0, t_fn}); b.op(OpLabel, {label});
    if (vertex) {
        b.op(OpLoad, {t_v2, in_pos, pos_in}); b.op(OpCompositeExtract, {t_f, x, in_pos, 0});
        b.op(OpCompositeExtract, {t_f, y, in_pos, 1}); b.op(OpCompositeConstruct, {t_v4, position, x, y, f0, f1});
        b.op(OpLoad, {t_u4, blend, blend_in}); b.op(OpCompositeExtract, {t_u, bone, blend, 0});
        b.op(OpAccessChain, {p_uni_v4, ptr_a, bone_var, c0, bone});
        b.op(OpAccessChain, {p_uni_v4, ptr_b, instance_var, c0});
        b.op(OpLoad, {t_v4, a, ptr_a}); b.op(OpLoad, {t_v4, v, ptr_b});
        b.op(OpFAdd, {t_v4, sum, a, v}); b.op(OpFAdd, {t_v4, moved, position, sum});
        b.op(OpAccessChain, {p_out_v4, out_ptr, output, c0}); b.op(OpStore, {out_ptr, moved});
    } else {
        b.op(OpAccessChain, {p_uni_v4, ptr_a, bone_var, c1}); b.op(OpAccessChain, {p_uni_v4, ptr_b, instance_var, c1});
        b.op(OpLoad, {t_v4, a, ptr_a}); b.op(OpLoad, {t_v4, v, ptr_b}); b.op(OpFMul, {t_v4, sum, a, v});
        if (kill != Kill::None) {
            auto ax = b.id(), vx = b.id(), noise = b.id(), diff = b.id(), cond = b.id(), killed = b.id(), merge = b.id();
            b.op(OpCompositeExtract, {t_f, ax, a, 0}); b.op(OpCompositeExtract, {t_f, vx, v, 0});
            b.op(OpFMul, {t_f, noise, ax, k_noise});
            if (kill == Kill::Dither) {
                b.op(static_cast<Op>(131), {t_f, diff, vx, noise});           // OpFSub: the instance's fade minus the noise
                b.op(static_cast<Op>(184), {t_bool, cond, diff, f0});         // OpFOrdLessThan
            } else if (kill == Kill::Cutout) {
                b.op(static_cast<Op>(184), {t_bool, cond, ax, k_half});
            } else {
                b.op(static_cast<Op>(184), {t_bool, cond, noise, k_half});
            }
            b.op(static_cast<Op>(247), {merge, 0});                          // OpSelectionMerge
            b.op(static_cast<Op>(250), {cond, killed, merge});               // OpBranchConditional
            b.op(OpLabel, {killed}); b.op(static_cast<Op>(252), {});         // OpKill
            b.op(OpLabel, {merge});
        }
        if(material){
            b.op(OpLoad,{image_type,image,image_var});b.op(static_cast<Op>(95),{t_v4,texel,image,coord,2,c0});
            b.op(OpFMul,{t_v4,tinted,sum,texel});b.op(OpStore,{output,tinted});
        }else b.op(OpStore, {output, sum});
    }
    b.op(OpReturn, {}); b.op(OpFunctionEnd, {});
    return b.finish();
}

struct RecipeFixture {
    VkAttachmentDescription atts[2] = {};
    VkAttachmentReference color = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {};
    VkSubpassDependency dep[2] = {};
    VkRenderPassCreateInfo rp = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    VkPipelineShaderStageCreateInfo stages[2] = {};
    std::vector<efvk::ShaderStage> shaders;
    VkVertexInputBindingDescription binding = {0, 12, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R8G8B8A8_UINT, 8}};
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState ba = {};
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkDynamicState dynamic[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    VkGraphicsPipelineCreateInfo gp = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    efvk::PipelineLayoutInfo layout;

    RecipeFixture(bool material=false)
    {
        atts[0] = {0, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
                   VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
        atts[1] = {0, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
                   VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &color; sub.pDepthStencilAttachment = &depth_ref;
        dep[0] = {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0};
        dep[1] = {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 0};
        rp.attachmentCount = 2; rp.pAttachments = atts; rp.subpassCount = 1; rp.pSubpasses = &sub;
        rp.dependencyCount = 2; rp.pDependencies = dep;
        for (uint32_t i = 0; i < 2; i++) {
            auto s = std::make_shared<efvk::ShaderInfo>(); s->id = i + 1; s->code = shader(i == 0,material);
            s->size = static_cast<uint32_t>(s->code.size() * 4);
            stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT; stages[i].pName = "main";
            shaders.push_back({static_cast<uint32_t>(stages[i].stage), "main", s});
        }
        vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = attrs;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; vp.viewportCount = vp.scissorCount = 1;
        ds.depthTestEnable = ds.depthWriteEnable = VK_TRUE; ds.depthCompareOp = VK_COMPARE_OP_LESS;
        ba.blendEnable = VK_TRUE; ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; ba.colorBlendOp = VK_BLEND_OP_ADD;
        ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        ba.alphaBlendOp = VK_BLEND_OP_ADD; ba.colorWriteMask = 15;
        cb.attachmentCount = 1; cb.pAttachments = &ba; dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynamic;
        gp.stageCount = 2; gp.pStages = stages; gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia;
        gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pViewportState = &vp;
        gp.pDepthStencilState = &ds; gp.pColorBlendState = &cb; gp.pDynamicState = &dyn;
        for (uint32_t i = 0; i < 3; i++) {
            auto s = std::make_shared<efvk::DescriptorLayoutInfo>();
            if (i == 0) s->bindings[7] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, 17, 0, {}};
            if (i == 1 && material) s->bindings[6] = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,0,{}};
            if (i == 2) s->bindings[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, 17, 0, {}};
            layout.sets.push_back(s);
        }
    }

    efvk::GraphicsRecipe capture() const
    {
        auto pass = std::make_shared<efvk::RenderPassRecipe>(efvk::RenderPassRecipe::capture(rp));
        return efvk::GraphicsRecipe::capture(gp, shaders, pass);
    }
    static efvk::VertexInputRecipe mod_vertex()
    {
        return {{{0, 16, VK_VERTEX_INPUT_RATE_VERTEX}},
                {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R16G16B16A16_UINT, 8}}};
    }
};

} // namespace variant_fixture
