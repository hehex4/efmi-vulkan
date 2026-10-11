// Offline tests of tracker lifetimes and descriptor semantics. No Vulkan driver.
#include "../layer/src/state.h"
#include "../layer/src/json.h"
#include "src/spirv.h"

#include <cstdio>

using namespace efvk;

static int passed = 0, failed = 0;
static void check(bool ok, const char* label)
{
    if (ok) passed++;
    else { failed++; printf("FAIL: %s\n", label); }
}
template <typename T> static T h(uintptr_t value) { return reinterpret_cast<T>(value); }

static void layout(Device& d, VkDescriptorSetLayout handle, const std::vector<VkDescriptorSetLayoutBinding>& bindings,
                   const void* next = nullptr)
{
    VkDescriptorSetLayoutCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.pNext = next;
    info.bindingCount = static_cast<uint32_t>(bindings.size()); info.pBindings = bindings.data();
    on_create_set_layout(&d, handle, &info);
}

static void pipeline_layout(Device& d, VkPipelineLayout handle, const std::vector<VkDescriptorSetLayout>& sets,
                            bool push = false)
{
    VkPushConstantRange range = {VK_SHADER_STAGE_VERTEX_BIT, 0, 16};
    VkPipelineLayoutCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = static_cast<uint32_t>(sets.size()); info.pSetLayouts = sets.data();
    info.pushConstantRangeCount = push ? 1 : 0; info.pPushConstantRanges = &range;
    on_create_pipeline_layout(&d, handle, &info);
}

static void allocate(Device& d, VkDescriptorPool pool, VkDescriptorSetLayout sl, VkDescriptorSet set, const void* next = nullptr)
{
    VkDescriptorSetAllocateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.pNext = next; info.descriptorPool = pool; info.descriptorSetCount = 1; info.pSetLayouts = &sl;
    on_allocate_sets(&d, &info, &set);
}

static std::string snapshot(Device& d, VkDescriptorSet set)
{
    SetBind b;
    b.set = set; b.id = d.descriptor_sets.at(set).id;
    JsonWriter w;
    write_descriptor_set_locked(w, &d, b, {});
    return w.out;
}

static void descriptor_writes()
{
    Device d;
    auto pool = h<VkDescriptorPool>(1);
    auto a = h<VkDescriptorSet>(2), b = h<VkDescriptorSet>(3);
    auto la = h<VkDescriptorSetLayout>(4), lb = h<VkDescriptorSetLayout>(5);
    auto sampler_a = h<VkSampler>(6), sampler_b = h<VkSampler>(7);
    auto buffer = h<VkBuffer>(8);
    VkSamplerCreateInfo si = {};
    on_create_sampler(&d, sampler_a, &si); on_create_sampler(&d, sampler_b, &si);
    VkBufferCreateInfo bi = {};
    bi.size = 4096; bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    on_create_buffer(&d, buffer, &bi);
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {8, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &sampler_a},
    };
    layout(d, la, bindings);
    bindings[3].pImmutableSamplers = &sampler_b;
    layout(d, lb, bindings);
    allocate(d, pool, la, a); allocate(d, pool, lb, b);
    VkDescriptorBufferInfo infos[] = {{buffer, 64, 16}, {buffer, 128, 16}, {buffer, 192, VK_WHOLE_SIZE}};
    VkWriteDescriptorSet write = {};
    write.dstSet = a; write.dstBinding = 3; write.dstArrayElement = 1; write.descriptorCount = 3;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo = infos;
    VkCopyDescriptorSet copy = {};
    copy.srcSet = a; copy.srcBinding = 3; copy.srcArrayElement = 1;
    copy.dstSet = b; copy.dstBinding = 3; copy.descriptorCount = 3;
    on_update_sets(&d, 1, &write, 1, &copy);
    auto& av = d.descriptor_sets.at(a).values;
    auto& bv = d.descriptor_sets.at(b).values;
    check(av.at({3, 1}).offset == 64 && av.at({5, 0}).offset == 128 && av.at({5, 1}).offset == 192,
          "write spans array boundary and skips a zero-sized binding");
    check(bv.at({3, 0}).offset == 64 && bv.at({3, 1}).offset == 128 && bv.at({5, 0}).offset == 192,
          "copies see writes from the same call and span destination bindings");
    check(bv.at({5, 0}).range == VK_WHOLE_SIZE, "whole-size descriptor survives a copy");
    check(bv.at({8, 0}).known && bv.at({8, 0}).sampler == sampler_b, "immutable sampler initialized at allocation");
    copy.srcBinding = copy.dstBinding = 8; copy.srcArrayElement = 0; copy.descriptorCount = 1;
    on_update_sets(&d, 0, nullptr, 1, &copy);
    check(bv.at({8, 0}).sampler == sampler_b, "copy cannot replace destination immutable sampler");
    write.dstSet = b; write.dstBinding = 5; write.dstArrayElement = 1; write.descriptorCount = 1;
    on_update_sets(&d, 1, &write, 0, nullptr);
    check(bv.at({5, 1}).known, "destination is defined before the undefined copy");
    copy.srcBinding = 3; copy.dstBinding = 5; copy.dstArrayElement = 1;
    on_update_sets(&d, 0, nullptr, 1, &copy);
    check(!bv.at({5, 1}).known, "copying an undefined descriptor clears previous knowledge");
    std::string before = snapshot(d, b);
    check(before.find("\"whole_size\":true,\"range\":3904") != std::string::npos,
          "whole size resolves against the tracked buffer size");
    uint32_t old_buffer_id = bv.at({3, 0}).buffer_id;
    on_destroy_buffer(&d, buffer); on_create_buffer(&d, buffer, &bi);
    check(d.bufs.at(buffer).id != old_buffer_id, "buffer ids change on handle reuse");
    check(snapshot(d, b).find("\"resource_known\":false") != std::string::npos, "reused buffer is not attributed to an old descriptor");
    on_unsupported_set_update(&d, b);
    check(bv.empty() && snapshot(d, b).find("descriptor_update_template") != std::string::npos,
          "template updates clear stale ordinary descriptors and identify the gap");
    check(before.find("descriptor_update_template") == std::string::npos, "previous capture is immutable after an update");
    on_free_sets(&d, 1, &a);
    check(d.descriptor_sets.count(a) == 0 && d.descriptor_sets.count(b) == 1, "free removes only the requested set");
    SetBind old;
    old.set = b; old.id = d.descriptor_sets.at(b).id;
    on_clear_descriptor_pool(&d, pool);
    check(d.descriptor_sets.empty(), "pool reset/destroy removes every owned set");
    allocate(d, pool, lb, b);
    JsonWriter stale;
    write_descriptor_set_locked(stale, &d, old, {});
    check(stale.out.find("unknown_or_freed_set") != std::string::npos, "set handle reuse cannot alter an old recording");

    VkDescriptorBindingFlags flag = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo flags = {};
    flags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flags.bindingCount = 1; flags.pBindingFlags = &flag;
    layout(d, la, {{7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10000, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}}, &flags);
    uint32_t count = 3;
    VkDescriptorSetVariableDescriptorCountAllocateInfo variable = {};
    variable.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    variable.descriptorSetCount = 1; variable.pDescriptorCounts = &count;
    allocate(d, pool, la, a, &variable);
    check(d.descriptor_sets.at(a).counts.at(7) == 3, "variable count comes from allocation");
    check(d.descriptor_sets.at(a).values.empty(), "unused bindless elements allocate no values");
    on_destroy_set_layout(&d, la);
    check(snapshot(d, a).find("\"count\":3") != std::string::npos, "set retains its layout after layout handle destruction");

    layout(d, la, {{0, VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK, 16, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
    allocate(d, pool, la, a);
    write = {};
    write.dstSet = a; write.descriptorType = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK; write.descriptorCount = 16;
    on_update_sets(&d, 1, &write, 0, nullptr);
    check(snapshot(d, a).find("\"unknown_elements\":16") != std::string::npos,
          "unsupported descriptor types remain unknown without dereferencing ignored pointers");
}

static void descriptor_binding()
{
    Device d;
    auto a = h<VkDescriptorSetLayout>(1), b = h<VkDescriptorSetLayout>(2), twin = h<VkDescriptorSetLayout>(3);
    auto set_a = h<VkDescriptorSet>(4), set_b = h<VkDescriptorSet>(5);
    auto pool = h<VkDescriptorPool>(6);
    auto pl = h<VkPipelineLayout>(7), same = h<VkPipelineLayout>(8), changed = h<VkPipelineLayout>(9);
    layout(d, a, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}});
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 2, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    layout(d, b, bindings); layout(d, twin, bindings);
    pipeline_layout(d, pl, {a, b}); pipeline_layout(d, same, {a, twin}); pipeline_layout(d, changed, {a, b}, true);
    allocate(d, pool, a, set_a); allocate(d, pool, b, set_b);
    CmdBuf cb;
    VkDescriptorSet sets[] = {set_a, set_b};
    uint32_t dyn[] = {10, 20, 30, 40};
    on_bind_sets(&d, &cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 2, sets, 4, dyn);
    check(cb.sets[0].dyn_count == 1 && cb.sets[1].dyn_count == 3, "dynamic offsets split by set");
    check(cb.dyn[cb.sets[1].dyn_first] == 20 && cb.dyn[cb.sets[1].dyn_first + 2] == 40,
          "second set starts after first set's dynamic elements");
    on_bind_sets(&d, &cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, sets, 1, dyn + 1);
    check(cb.dyn[cb.comp_sets[0].dyn_first] == 20 && cb.dyn[cb.sets[0].dyn_first] == 10,
          "compute and graphics descriptor bindings are independent");
    on_bind_sets(&d, &cb, VK_PIPELINE_BIND_POINT_GRAPHICS, same, 1, 1, sets + 1, 3, dyn + 1);
    check(cb.sets[0].id != 0, "equivalent layout with a different handle preserves lower set");
    on_bind_sets(&d, &cb, VK_PIPELINE_BIND_POINT_GRAPHICS, changed, 1, 1, sets + 1, 3, dyn + 1);
    check(cb.sets[0].id == 0 && cb.sets[0].unsupported, "incompatible push ranges disturb lower set");
    on_bind_sets(&d, &cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, sets, 1, dyn);
    check(cb.sets[1].id == 0 && cb.sets[1].unsupported, "incompatible lower rebind disturbs higher set");
    on_bind_sets(&d, &cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 2, sets, 4, dyn);
    on_destroy_set_layout(&d, a); on_destroy_set_layout(&d, b); on_destroy_pipeline_layout(&d, pl);
    check(cb.sets[0].layout && cb.sets[0].layout->sets[1]->bindings.size() == 2,
          "recording keeps pipeline and descriptor layout definitions alive");
    on_unsupported_descriptor_bind(&cb, VK_PIPELINE_BIND_POINT_GRAPHICS, "push_descriptors");
    check(cb.sets[0].set == VK_NULL_HANDLE && cb.comp_sets[0].id != 0,
          "unsupported graphics binding clears stale sets but preserves compute");
    cb.reset_recording();
    check(!cb.comp_sets[0].set && !cb.sets[0].layout && cb.dyn.empty(), "reset releases both bind points and dynamic data");
}

static void shaders_and_views()
{
    // Only the dispatch key is inspected; no function is called on this fake device.
    void* dispatch = nullptr;
    Device* d = dev_create(reinterpret_cast<VkDevice>(&dispatch));
    auto module = h<VkShaderModule>(1), module2 = h<VkShaderModule>(2);
    auto pipe = h<VkPipeline>(3);
    auto code = spv::vertex_shader();
    VkShaderModuleCreateInfo sm = {};
    sm.codeSize = code.size() * 4; sm.pCode = code.data();
    on_create_shader(d, module, &sm);
    uint32_t shader_id = d->shaders.at(module)->id;
    VkPipelineShaderStageCreateInfo stage = {};
    stage.stage = VK_SHADER_STAGE_VERTEX_BIT; stage.module = module; stage.pName = "main";
    VkGraphicsPipelineCreateInfo info = {};
    info.stageCount = 1; info.pStages = &stage;
    on_create_graphics_pipeline(d, pipe, &info);
    on_destroy_shader(d, module);
    std::vector<uint8_t> payload;
    check(probe_shader(shader_id, true, payload).find("\"ok\":true") != std::string::npos &&
          payload.size() == code.size() * 4 && !memcmp(payload.data(), code.data(), payload.size()),
          "pipeline retains exact SPIR-V bytes after module destruction");
    on_create_shader(d, module, &sm);
    check(d->shaders.at(module)->id != shader_id && d->pipes.at(pipe).shaders[0].shader->id == shader_id,
          "shader module handle reuse does not change old pipeline stages");
    on_destroy_pipeline(d, pipe);
    check(d->shader_by_id.count(shader_id) == 0, "last pipeline destruction reclaims old shader bytes and id");
    sm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    stage.module = VK_NULL_HANDLE; stage.pNext = &sm;
    on_create_graphics_pipeline(d, pipe, &info);
    check(d->pipes.at(pipe).shaders[0].shader != nullptr, "inline pipeline SPIR-V is retained");
    on_destroy_pipeline(d, pipe); on_destroy_shader(d, module); on_destroy_shader(d, module2);
    check(d->shader_by_id.empty(), "all unreferenced shader metadata is reclaimed");

    auto buffer = h<VkBuffer>(11);
    auto view = h<VkBufferView>(12);
    VkBufferCreateInfo bi = {}; bi.size = 1024; bi.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    on_create_buffer(d, buffer, &bi);
    VkBufferViewCreateInfo bv = {}; bv.buffer = buffer; bv.format = VK_FORMAT_R32_SFLOAT; bv.offset = 64; bv.range = 128;
    on_create_buffer_view(d, view, &bv);
    check(d->buffer_views.at(view).buffer_id == d->bufs.at(buffer).id && d->buffer_views.at(view).range == 128,
          "texel view keeps buffer identity and range");
    on_destroy_buffer_view(d, view);
    check(d->buffer_views.empty(), "buffer view destruction is tracked");
    auto image = h<VkImage>(13);
    auto image_view = h<VkImageView>(14);
    VkImageCreateInfo im = {}; im.extent = {64, 32, 1}; im.format = VK_FORMAT_R8G8B8A8_UNORM; im.mipLevels = 3; im.arrayLayers = 2;
    on_create_image(d, image, &im);
    VkImageViewCreateInfo iv = {}; iv.image = image; iv.format = im.format; iv.subresourceRange = {1, 1, 2, 0, 2};
    on_create_image_view(d, image_view, &iv);
    check(d->image_views.at(image_view).image_id == d->images.at(image).id && d->image_views.at(image_view).range.baseMipLevel == 1,
          "image view keeps image identity and subresource range");
    on_destroy_image(d, image); on_destroy_image_view(d, image_view);
    check(d->images.empty() && d->image_views.empty(), "image and view destruction are tracked");
    dev_destroy(d);
}

int main()
{
    util_init(GetModuleHandleW(nullptr));
    descriptor_writes();
    descriptor_binding();
    shaders_and_views();
    printf("tracker_test: %d/%d checks passed\n", passed, passed + failed);
    return failed ? 1 : 0;
}
