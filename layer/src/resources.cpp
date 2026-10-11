// Metadata for shader modules, image resources and descriptor bindings.
// No Vulkan calls are made here. Ordinary sets resolve at submit; pushes at record.
#include "state.h"
#include "json.h"

#include <algorithm>
#include <cstddef>

namespace efvk {

template <typename T> static const T* find_next(const void* next, VkStructureType type)
{
    for (auto p = static_cast<const VkBaseInStructure*>(next); p; p = p->pNext)
        if (p->sType == type)
            return reinterpret_cast<const T*>(p);
    return nullptr;
}

template <typename Map, typename Handle> static uint32_t resource_id(const Map& map, Handle handle)
{
    auto it = map.find(handle);
    return it == map.end() ? 0 : it->second.id;
}

static std::shared_ptr<const ShaderInfo> make_shader_locked(Device* dev, const VkShaderModuleCreateInfo* info)
{
    auto s = std::make_shared<ShaderInfo>();
    s->id = dev->next_resource_id++;
    s->code.assign(info->pCode, info->pCode + info->codeSize / 4);
    s->hash = crc32c(0, s->code.data(), s->code.size() * 4);
    dev->shader_by_id[s->id] = s;
    return s;
}

void on_create_shader(Device* dev, VkShaderModule h, const VkShaderModuleCreateInfo* info)
{
    if (dev->passive) return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    dev->shaders[h] = make_shader_locked(dev, info);
}

void prune_shaders_locked(Device* dev)
{
    for (auto it = dev->shader_by_id.begin(); it != dev->shader_by_id.end();)
        if (it->second.expired()) it = dev->shader_by_id.erase(it);
        else ++it;
}

void on_destroy_shader(Device* dev, VkShaderModule h)
{
    if (dev->passive) return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    dev->shaders.erase(h);
    prune_shaders_locked(dev);
}

void track_stages_locked(Device* dev, PipeInfo& pipe, uint32_t count, const VkPipelineShaderStageCreateInfo* stages)
{
    for (uint32_t i = 0; i < count; i++) {
        const auto& in = stages[i];
        ShaderStage s;
        s.stage = in.stage;
        s.entry = in.pName ? in.pName : "";
        auto it = dev->shaders.find(in.module);
        if (it != dev->shaders.end()) s.shader = it->second;
        else if (auto ci = find_next<VkShaderModuleCreateInfo>(in.pNext, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO))
            s.shader = make_shader_locked(dev, ci);
        pipe.shaders.push_back(std::move(s));
    }
}

void on_create_image(Device* dev, VkImage h, const VkImageCreateInfo* info)
{
    if (dev->passive) return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    ImageInfo image;
    image.id = dev->next_resource_id++;
    image.type = info->imageType;
    image.format = info->format;
    image.extent = info->extent;
    image.levels = info->mipLevels;
    image.layers = info->arrayLayers;
    image.samples = info->samples;
    image.usage = info->usage;
    dev->images[h] = image;
}

void on_create_image_view(Device* dev, VkImageView h, const VkImageViewCreateInfo* info)
{
    if (dev->passive) return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    ImageViewInfo view;
    view.id = dev->next_resource_id++;
    view.image = info->image;
    view.image_id = resource_id(dev->images, info->image);
    view.type = info->viewType;
    view.format = info->format;
    view.components = info->components;
    view.range = info->subresourceRange;
    dev->image_views[h] = view;
}

void on_create_buffer_view(Device* dev, VkBufferView h, const VkBufferViewCreateInfo* info)
{
    if (dev->passive) return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    BufferViewInfo view;
    view.id = dev->next_resource_id++;
    view.buffer = info->buffer;
    view.buffer_id = resource_id(dev->bufs, info->buffer);
    view.format = info->format;
    view.offset = info->offset;
    view.range = info->range;
    dev->buffer_views[h] = view;
}

void on_create_sampler(Device* dev, VkSampler h, const VkSamplerCreateInfo* info)
{
    if (dev->passive) return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    SamplerInfo s;
    s.id = dev->next_resource_id++;
    s.mag = info->magFilter;
    s.min = info->minFilter;
    s.mipmap = info->mipmapMode;
    s.address[0] = info->addressModeU;
    s.address[1] = info->addressModeV;
    s.address[2] = info->addressModeW;
    dev->samplers[h] = s;
}

#define DESTROY_RESOURCE(fn, Handle, map) \
    void fn(Device* dev, Handle h) { \
        if (dev->passive) return; \
        std::unique_lock<std::shared_mutex> lock(dev->res_mx); \
        dev->map.erase(h); \
    }
DESTROY_RESOURCE(on_destroy_image, VkImage, images)
DESTROY_RESOURCE(on_destroy_image_view, VkImageView, image_views)
DESTROY_RESOURCE(on_destroy_buffer_view, VkBufferView, buffer_views)
DESTROY_RESOURCE(on_destroy_sampler, VkSampler, samplers)
DESTROY_RESOURCE(on_destroy_set_layout, VkDescriptorSetLayout, set_layouts)
DESTROY_RESOURCE(on_destroy_pipeline_layout, VkPipelineLayout, pipeline_layouts)
DESTROY_RESOURCE(on_destroy_descriptor_template, VkDescriptorUpdateTemplate, descriptor_templates)
#undef DESTROY_RESOURCE

static bool is_dynamic(uint32_t type)
{
    return type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}

static bool is_buffer(uint32_t type)
{
    return is_dynamic(type) || type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
}

void on_create_set_layout(Device* dev, VkDescriptorSetLayout h, const VkDescriptorSetLayoutCreateInfo* info)
{
    if (dev->passive) return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    auto layout = std::make_shared<DescriptorLayoutInfo>();
    layout->id = dev->next_resource_id++;
    layout->flags = info->flags;
    auto flags = find_next<VkDescriptorSetLayoutBindingFlagsCreateInfo>(
        info->pNext, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);
    for (auto p = static_cast<const VkBaseInStructure*>(info->pNext); p; p = p->pNext)
        if (p->sType != VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO)
            layout->complete = false;
    for (uint32_t i = 0; i < info->bindingCount; i++) {
        const auto& in = info->pBindings[i];
        DescriptorBinding b;
        b.type = in.descriptorType;
        b.count = in.descriptorCount;
        b.stages = in.stageFlags;
        if (flags && i < flags->bindingCount) b.flags = flags->pBindingFlags[i];
        if (in.pImmutableSamplers &&
            (b.type == VK_DESCRIPTOR_TYPE_SAMPLER || b.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER))
            b.immutable_samplers.assign(in.pImmutableSamplers, in.pImmutableSamplers + b.count);
        layout->bindings[in.binding] = std::move(b);
    }
    dev->set_layouts[h] = std::move(layout);
}

void on_create_pipeline_layout(Device* dev, VkPipelineLayout h, const VkPipelineLayoutCreateInfo* info)
{
    if (dev->passive) return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    auto layout = std::make_shared<PipelineLayoutInfo>();
    layout->id = dev->next_resource_id++;
    layout->flags = info->flags;
    layout->complete = info->pNext == nullptr;
    for (uint32_t i = 0; i < info->setLayoutCount; i++) {
        auto it = dev->set_layouts.find(info->pSetLayouts[i]);
        layout->sets.push_back(it == dev->set_layouts.end() ? nullptr : it->second);
    }
    if (info->pushConstantRangeCount)
        layout->pushes.assign(info->pPushConstantRanges, info->pPushConstantRanges + info->pushConstantRangeCount);
    std::sort(layout->pushes.begin(), layout->pushes.end(), [](const auto& a, const auto& b) {
        if (a.stageFlags != b.stageFlags) return a.stageFlags < b.stageFlags;
        if (a.offset != b.offset) return a.offset < b.offset;
        return a.size < b.size;
    });
    dev->pipeline_layouts[h] = std::move(layout);
}

static bool same_set_layout(const DescriptorLayoutInfo& a, const DescriptorLayoutInfo& b)
{
    if (&a == &b) return true;
    if (!a.complete || !b.complete || a.flags != b.flags || a.bindings.size() != b.bindings.size()) return false;
    auto j = b.bindings.begin();
    for (const auto& kv : a.bindings) {
        const auto& x = kv.second;
        const auto& y = j->second;
        if (kv.first != j->first || x.type != y.type || x.count != y.count || x.stages != y.stages ||
            x.flags != y.flags || x.immutable_samplers != y.immutable_samplers) return false;
        ++j;
    }
    return true;
}

static bool compatible(const std::shared_ptr<const PipelineLayoutInfo>& a,
                       const std::shared_ptr<const PipelineLayoutInfo>& b, uint32_t slot)
{
    if (!a || !b || slot >= a->sets.size() || slot >= b->sets.size()) return false;
    if (a == b) return true;
    if (!a->complete || !b->complete || a->flags != b->flags || a->pushes.size() != b->pushes.size()) return false;
    for (size_t i = 0; i < a->pushes.size(); i++) {
        const auto& x = a->pushes[i];
        const auto& y = b->pushes[i];
        if (x.stageFlags != y.stageFlags || x.offset != y.offset || x.size != y.size) return false;
    }
    for (uint32_t i = 0; i <= slot; i++)
        if (!a->sets[i] || !b->sets[i] || !same_set_layout(*a->sets[i], *b->sets[i])) return false;
    return true;
}

void on_allocate_sets(Device* dev, const VkDescriptorSetAllocateInfo* info, const VkDescriptorSet* sets)
{
    if (dev->passive) return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    auto variable = find_next<VkDescriptorSetVariableDescriptorCountAllocateInfo>(
        info->pNext, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO);
    for (uint32_t i = 0; i < info->descriptorSetCount; i++) {
        DescriptorSetInfo set;
        set.id = dev->next_resource_id++;
        set.pool = info->descriptorPool;
        auto lit = dev->set_layouts.find(info->pSetLayouts[i]);
        if (lit != dev->set_layouts.end()) {
            set.layout = lit->second;
            for (const auto& kv : set.layout->bindings) {
                const auto& b = kv.second;
                uint32_t count = b.count;
                if (b.flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT)
                    count = variable && i < variable->descriptorSetCount ? variable->pDescriptorCounts[i] : 0;
                set.counts[kv.first] = count;
                // Immutable samplers are initialized by allocation, without a write.
                for (uint32_t k = 0; k < count && k < b.immutable_samplers.size(); k++) {
                    DescriptorElement e;
                    e.type = b.type;
                    e.known = b.type == VK_DESCRIPTOR_TYPE_SAMPLER;
                    e.sampler = b.immutable_samplers[k];
                    e.sampler_id = resource_id(dev->samplers, e.sampler);
                    set.values[{kv.first, k}] = e;
                }
            }
        }
        dev->descriptor_sets[sets[i]] = std::move(set);
    }
}

void on_free_sets(Device* dev, uint32_t count, const VkDescriptorSet* sets)
{
    if (dev->passive) return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    for (uint32_t i = 0; i < count; i++) dev->descriptor_sets.erase(sets[i]);
}

void on_clear_descriptor_pool(Device* dev, VkDescriptorPool pool)
{
    if (dev->passive) return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    for (auto it = dev->descriptor_sets.begin(); it != dev->descriptor_sets.end();)
        if (it->second.pool == pool) it = dev->descriptor_sets.erase(it);
        else ++it;
}

// A write/copy can span consecutive bindings (including zero-sized ones).
static bool advance_element(const DescriptorSetInfo& set, uint32_t& binding, uint32_t& element)
{
    auto it = set.counts.lower_bound(binding);
    while (it != set.counts.end()) {
        if (element < it->second) { binding = it->first; return true; }
        element -= it->second;
        ++it;
    }
    return false;
}

static void set_element(Device* dev, DescriptorSetInfo& set, uint32_t binding, uint32_t element, DescriptorElement value)
{
    const auto& b = set.layout->bindings.at(binding);
    if (element < b.immutable_samplers.size()) {
        value.sampler = b.immutable_samplers[element];
        value.sampler_id = resource_id(dev->samplers, value.sampler);
        if (b.type == VK_DESCRIPTOR_TYPE_SAMPLER) value.known = true;
    }
    set.values[{binding, element}] = value;
}

static size_t descriptor_data_size(uint32_t type)
{
    if (is_buffer(type)) return sizeof(VkDescriptorBufferInfo);
    if (type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)
        return sizeof(VkBufferView);
    if (type <= VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
        return sizeof(VkDescriptorImageInfo);
    return 0;
}

// The application controls template alignment and stride. Read only fields that
// Vulkan consumes, using memcpy rather than dereferencing an unaligned structure.
static DescriptorElement decode_element(Device* dev, const DescriptorBinding& binding, uint32_t type, const uint8_t* data)
{
    DescriptorElement e;
    e.type = type;
    if (!descriptor_data_size(type)) return e;
    const bool immutable = !binding.immutable_samplers.empty();
    if (type == VK_DESCRIPTOR_TYPE_SAMPLER && immutable) { e.known = true; return e; }
    if (!data) return e;
    e.known = true;
    if (is_buffer(type)) {
        VkDescriptorBufferInfo b;
        memcpy(&b, data, sizeof(b));
        e.buffer = b.buffer; e.offset = b.offset; e.range = b.range;
        e.buffer_id = resource_id(dev->bufs, e.buffer);
    } else if (type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) {
        memcpy(&e.buffer_view, data, sizeof(e.buffer_view));
        e.buffer_view_id = resource_id(dev->buffer_views, e.buffer_view);
    } else {
        if ((type == VK_DESCRIPTOR_TYPE_SAMPLER || type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) && !immutable) {
            memcpy(&e.sampler, data + offsetof(VkDescriptorImageInfo, sampler), sizeof(e.sampler));
            e.sampler_id = resource_id(dev->samplers, e.sampler);
        }
        if (type != VK_DESCRIPTOR_TYPE_SAMPLER) {
            memcpy(&e.image_view, data + offsetof(VkDescriptorImageInfo, imageView), sizeof(e.image_view));
            memcpy(&e.image_layout, data + offsetof(VkDescriptorImageInfo, imageLayout), sizeof(e.image_layout));
            e.image_view_id = resource_id(dev->image_views, e.image_view);
        }
    }
    return e;
}

static void apply_elements_locked(Device* dev, DescriptorSetInfo& set, uint32_t binding, uint32_t element,
                                  uint32_t count, uint32_t type, const void* data, size_t offset, size_t stride)
{
    if (!set.layout) return;
    size_t size = descriptor_data_size(type);
    for (uint32_t i = 0; i < count && advance_element(set, binding, element); i++, element++) {
        const auto& b = set.layout->bindings.at(binding);
        const uint8_t* source = nullptr;
        // Unknown descriptor types and immutable sampler-only writes consume no data.
        if (size && data && !(type == VK_DESCRIPTOR_TYPE_SAMPLER && !b.immutable_samplers.empty()) &&
            offset <= SIZE_MAX - size && (!i || stride <= (SIZE_MAX - size - offset) / i))
            source = static_cast<const uint8_t*>(data) + offset + static_cast<size_t>(i) * stride;
        set_element(dev, set, binding, element, decode_element(dev, b, type, source));
    }
    set.generation++;
}

static void apply_write_locked(Device* dev, DescriptorSetInfo& set, const VkWriteDescriptorSet& w)
{
    const void* data = nullptr;
    if (is_buffer(w.descriptorType)) data = w.pBufferInfo;
    else if (w.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || w.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)
        data = w.pTexelBufferView;
    else if (descriptor_data_size(w.descriptorType)) data = w.pImageInfo;
    apply_elements_locked(dev, set, w.dstBinding, w.dstArrayElement, w.descriptorCount,
                          w.descriptorType, data, 0, descriptor_data_size(w.descriptorType));
}

void on_update_sets(Device* dev, uint32_t write_count, const VkWriteDescriptorSet* writes,
                    uint32_t copy_count, const VkCopyDescriptorSet* copies)
{
    if (dev->passive) return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    for (uint32_t n = 0; n < write_count; n++) {
        const auto& w = writes[n];
        auto it = dev->descriptor_sets.find(w.dstSet);
        if (it == dev->descriptor_sets.end() || !it->second.layout) continue;
        apply_write_locked(dev, it->second, w);
    }
    // Vulkan applies all writes first, then copies in array order.
    for (uint32_t n = 0; n < copy_count; n++) {
        const auto& c = copies[n];
        auto src = dev->descriptor_sets.find(c.srcSet);
        auto dst = dev->descriptor_sets.find(c.dstSet);
        if (dst == dev->descriptor_sets.end() || !dst->second.layout) continue;
        uint32_t sb = c.srcBinding, se = c.srcArrayElement, db = c.dstBinding, de = c.dstArrayElement;
        for (uint32_t i = 0; i < c.descriptorCount && advance_element(dst->second, db, de); i++, se++, de++) {
            DescriptorElement e;
            e.type = dst->second.layout->bindings.at(db).type;
            if (src != dev->descriptor_sets.end() && advance_element(src->second, sb, se)) {
                auto v = src->second.values.find({sb, se});
                if (v != src->second.values.end()) e = v->second;
            }
            set_element(dev, dst->second, db, de, e);
        }
        dst->second.generation++;
    }
}

void on_create_descriptor_template(Device* dev, VkDescriptorUpdateTemplate h, const VkDescriptorUpdateTemplateCreateInfo* info)
{
    if (dev->passive) return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    DescriptorTemplateInfo t;
    t.type = info->templateType;
    t.complete = info->pNext == nullptr;
    if (info->descriptorUpdateEntryCount)
        t.entries.assign(info->pDescriptorUpdateEntries, info->pDescriptorUpdateEntries + info->descriptorUpdateEntryCount);
    if (t.type == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET) {
        auto it = dev->set_layouts.find(info->descriptorSetLayout);
        if (it != dev->set_layouts.end()) t.layout = it->second;
    } else if (t.type == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR) {
        t.point = info->pipelineBindPoint; t.set = info->set;
        auto it = dev->pipeline_layouts.find(info->pipelineLayout);
        if (it != dev->pipeline_layouts.end()) t.pipeline_layout = it->second;
    }
    dev->descriptor_templates[h] = std::move(t);
}

static void apply_template_locked(Device* dev, DescriptorSetInfo& set, const DescriptorTemplateInfo& t, const void* data)
{
    for (const auto& e : t.entries)
        apply_elements_locked(dev, set, e.dstBinding, e.dstArrayElement, e.descriptorCount,
                              e.descriptorType, data, e.offset, e.stride);
}

void on_update_template(Device* dev, VkDescriptorSet h, VkDescriptorUpdateTemplate update, const void* data)
{
    if (dev->passive) return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    auto s = dev->descriptor_sets.find(h);
    if (s == dev->descriptor_sets.end()) return;
    auto t = dev->descriptor_templates.find(update);
    if (t == dev->descriptor_templates.end() || !t->second.complete || !t->second.layout || !s->second.layout ||
        t->second.type != VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET ||
        !same_set_layout(*t->second.layout, *s->second.layout)) {
        s->second.values.clear();
        s->second.unsupported = "unknown_descriptor_template";
        s->second.generation++;
        return;
    }
    apply_template_locked(dev, s->second, t->second, data);
}

static void disturb_sets(SetBind* bound, const std::shared_ptr<const PipelineLayoutInfo>& layout, uint32_t slot)
{
    bool disturb_higher = slot < kMaxSets && bound[slot].layout && !compatible(bound[slot].layout, layout, slot);
    for (uint32_t k = 0; k < kMaxSets; k++) {
        if ((k < slot && bound[k].layout && !compatible(bound[k].layout, layout, k)) || (k > slot && disturb_higher)) {
            bound[k] = SetBind();
            bound[k].layout = layout;
            bound[k].unsupported = "disturbed_pipeline_layout";
        }
    }
}

static std::shared_ptr<DescriptorSetInfo> prepare_push_locked(CmdBuf* cb, VkPipelineBindPoint point,
    const std::shared_ptr<const PipelineLayoutInfo>& layout, uint32_t slot)
{
    auto* bound = point == VK_PIPELINE_BIND_POINT_GRAPHICS ? cb->sets : cb->comp_sets;
    if (!layout || slot >= layout->sets.size() || !layout->sets[slot] ||
        !(layout->sets[slot]->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR)) {
        on_unsupported_descriptor_bind(cb, point, "unknown_push_layout");
        return nullptr;
    }
    disturb_sets(bound, layout, slot);
    if (slot >= kMaxSets) return nullptr;
    auto previous = bound[slot].pushed;
    auto pushed = previous && same_set_layout(*previous->layout, *layout->sets[slot])
        ? std::make_shared<DescriptorSetInfo>(*previous) : std::make_shared<DescriptorSetInfo>();
    pushed->layout = layout->sets[slot];
    if (!previous || !same_set_layout(*previous->layout, *pushed->layout))
        for (const auto& b : pushed->layout->bindings) pushed->counts[b.first] = b.second.count;
    // In contrast to allocated sets, even immutable samplers are undefined until pushed.
    bound[slot] = SetBind();
    bound[slot].layout = layout;
    bound[slot].pushed = pushed;
    return pushed;
}

void on_push_descriptors(Device* dev, CmdBuf* cb, VkPipelineBindPoint point, VkPipelineLayout handle,
                         uint32_t slot, uint32_t count, const VkWriteDescriptorSet* writes)
{
    if (dev->passive || (point != VK_PIPELINE_BIND_POINT_GRAPHICS && point != VK_PIPELINE_BIND_POINT_COMPUTE)) return;
    std::shared_lock<std::shared_mutex> lock(dev->res_mx);
    auto it = dev->pipeline_layouts.find(handle);
    auto pushed = prepare_push_locked(cb, point, it == dev->pipeline_layouts.end() ? nullptr : it->second, slot);
    if (pushed) for (uint32_t i = 0; i < count; i++) apply_write_locked(dev, *pushed, writes[i]);
}

void on_push_template(Device* dev, CmdBuf* cb, VkDescriptorUpdateTemplate update, VkPipelineLayout handle,
                       uint32_t slot, const void* data)
{
    if (dev->passive) return;
    std::shared_lock<std::shared_mutex> lock(dev->res_mx);
    auto t = dev->descriptor_templates.find(update);
    if (t == dev->descriptor_templates.end() || t->second.type != VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR) {
        on_unsupported_descriptor_bind(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, "unknown_push_template");
        on_unsupported_descriptor_bind(cb, VK_PIPELINE_BIND_POINT_COMPUTE, "unknown_push_template");
        return;
    }
    const auto& info = t->second;
    if (info.point != VK_PIPELINE_BIND_POINT_GRAPHICS && info.point != VK_PIPELINE_BIND_POINT_COMPUTE) return;
    auto lit = dev->pipeline_layouts.find(handle);
    if (!info.complete || info.set != slot || lit == dev->pipeline_layouts.end() || !compatible(info.pipeline_layout, lit->second, slot)) {
        on_unsupported_descriptor_bind(cb, info.point, "unknown_push_template_layout");
        return;
    }
    auto pushed = prepare_push_locked(cb, info.point, lit->second, slot);
    if (pushed) apply_template_locked(dev, *pushed, info, data);
}

void on_bind_sets(Device* dev, CmdBuf* cb, VkPipelineBindPoint point, VkPipelineLayout handle,
                  uint32_t first, uint32_t count, const VkDescriptorSet* sets, uint32_t dyn_count, const uint32_t* dyn)
{
    if (point != VK_PIPELINE_BIND_POINT_GRAPHICS && point != VK_PIPELINE_BIND_POINT_COMPUTE) return;
    auto* bound = point == VK_PIPELINE_BIND_POINT_GRAPHICS ? cb->sets : cb->comp_sets;
    std::shared_lock<std::shared_mutex> lock(dev->res_mx);
    auto lit = dev->pipeline_layouts.find(handle);
    auto layout = lit == dev->pipeline_layouts.end() ? nullptr : lit->second;
    uint32_t base = static_cast<uint32_t>(cb->dyn.size()), cursor = 0;
    if (dyn_count) cb->dyn.insert(cb->dyn.end(), dyn, dyn + dyn_count);
    bool offsets_known = true;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t slot = first + i;
        // Disturb lower sets and, when replacing an incompatible binding, higher sets.
        disturb_sets(bound, layout, slot);
        auto sit = dev->descriptor_sets.find(sets[i]);
        uint32_t n = 0;
        if (sit == dev->descriptor_sets.end() || !sit->second.layout) offsets_known = false;
        else for (const auto& b : sit->second.layout->bindings)
            if (is_dynamic(b.second.type)) n += sit->second.counts.at(b.first);
        if (cursor > dyn_count || n > dyn_count - cursor) offsets_known = false;
        if (slot < kMaxSets) {
            auto& s = bound[slot];
            s = SetBind();
            s.set = sets[i];
            s.id = sit == dev->descriptor_sets.end() ? 0 : sit->second.id;
            s.layout = layout;
            if (offsets_known) { s.dyn_first = base + cursor; s.dyn_count = n; }
            else s.unsupported = "unknown_dynamic_offset_layout";
        }
        cursor += n;
    }
}

void on_unsupported_descriptor_bind(CmdBuf* cb, VkPipelineBindPoint point, const char* reason)
{
    // Conservative: an untracked binding may also disturb other sets.
    if (point != VK_PIPELINE_BIND_POINT_GRAPHICS && point != VK_PIPELINE_BIND_POINT_COMPUTE) return;
    auto* bound = point == VK_PIPELINE_BIND_POINT_GRAPHICS ? cb->sets : cb->comp_sets;
    for (uint32_t i = 0; i < kMaxSets; i++) { bound[i] = SetBind(); bound[i].unsupported = reason; }
}

static void write_buffer_range(JsonWriter& w, Device* dev, VkBuffer handle, uint32_t id,
                               uint64_t offset, uint64_t range, uint32_t dynamic)
{
    auto it = dev->bufs.find(handle);
    bool alive = id && it != dev->bufs.end() && it->second.id == id;
    w.kn("buf", id);
    w.kb("resource_known", alive);
    w.kn("offset", offset);
    w.kn("dynamic_offset", dynamic);
    uint64_t effective = offset + dynamic;
    w.kn("effective_offset", effective);
    w.kb("whole_size", range == VK_WHOLE_SIZE);
    if (range == VK_WHOLE_SIZE) range = alive && offset <= it->second.size ? it->second.size - offset : 0;
    w.kn("range", range);
    const char* src = "none";
    if (alive && effective >= offset && effective <= it->second.size && range <= it->second.size - effective) {
        const auto& b = it->second;
        auto m = dev->mems.find(b.mem);
        if (m != dev->mems.end() && m->second.mapped) {
            uint64_t at = b.mem_off + effective;
            if (at >= m->second.map_off && at - m->second.map_off <= m->second.map_size &&
                range <= m->second.map_size - (at - m->second.map_off)) src = "mapped";
        }
        if (!strcmp(src, "none") && b.known.covers(effective, range)) src = "shadow";
    }
    w.kv("src", src);
}

static void write_element(JsonWriter& w, Device* dev, const DescriptorElement& e, uint32_t dynamic)
{
    w.kb("known", e.known);
    if (!e.known) return;
    if (is_buffer(e.type)) write_buffer_range(w, dev, e.buffer, e.buffer_id, e.offset, e.range, dynamic);
    if (e.type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || e.type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) {
        w.kn("buffer_view", e.buffer_view_id);
        auto it = dev->buffer_views.find(e.buffer_view);
        bool alive = e.buffer_view_id && it != dev->buffer_views.end() && it->second.id == e.buffer_view_id;
        w.kb("view_known", alive);
        if (alive) {
            const auto& v = it->second;
            w.kn("format", v.format);
            write_buffer_range(w, dev, v.buffer, v.buffer_id, v.offset, v.range, 0);
        }
    }
    if (e.type == VK_DESCRIPTOR_TYPE_SAMPLER || e.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
        w.kn("sampler", e.sampler_id);
        auto s = dev->samplers.find(e.sampler);
        w.kb("sampler_known", e.sampler_id && s != dev->samplers.end() && s->second.id == e.sampler_id);
    }
    if (e.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || e.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
        e.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || e.type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT) {
        w.kn("image_view", e.image_view_id);
        w.kn("image_layout", e.image_layout);
        auto v = dev->image_views.find(e.image_view);
        bool alive = e.image_view_id && v != dev->image_views.end() && v->second.id == e.image_view_id;
        w.kb("view_known", alive);
        if (alive) {
            const auto& view = v->second;
            w.kn("view_type", view.type);
            w.kn("format", view.format);
            w.key("subresource"); w.begin_object();
            w.kn("aspect", view.range.aspectMask);
            w.kn("base_mip", view.range.baseMipLevel); w.kn("levels", view.range.levelCount);
            w.kn("base_layer", view.range.baseArrayLayer); w.kn("layers", view.range.layerCount);
            w.end_object();
            w.kn("image", view.image_id);
            auto img = dev->images.find(view.image);
            bool image_known = view.image_id && img != dev->images.end() && img->second.id == view.image_id;
            w.kb("image_known", image_known);
            if (image_known) {
                const auto& image = img->second;
                w.key("extent"); w.begin_array();
                w.num(image.extent.width); w.num(image.extent.height); w.num(image.extent.depth); w.end_array();
                w.kn("image_format", image.format); w.kn("image_type", image.type);
                w.kn("mip_levels", image.levels); w.kn("array_layers", image.layers);
                w.kn("samples", image.samples); w.kn("usage", image.usage);
            }
        }
    }
}

void write_descriptor_set_locked(JsonWriter& w, Device* dev, const SetBind& bind, const std::vector<uint32_t>& dyn,
                                 VkPipeline pipeline, uint32_t slot)
{
    w.begin_object();
    w.kv("snapshot", bind.pushed ? "record" : "submit");
    w.kv("resource_snapshot", "submit");
    w.kv("kind", bind.pushed ? "push" : "set");
    w.kn("id", bind.id);
    if (pipeline) {
        auto p = dev->pipes.find(pipeline);
        w.kb("pipeline_compatible", p != dev->pipes.end() && compatible(bind.layout, p->second.layout, slot));
    }
    auto it = dev->descriptor_sets.find(bind.set);
    const DescriptorSetInfo* contents = bind.pushed.get();
    if (!contents && bind.id && it != dev->descriptor_sets.end() && it->second.id == bind.id) contents = &it->second;
    bool tracked = !bind.unsupported && contents && contents->layout;
    w.kb("tracked", tracked);
    if (!tracked) {
        w.kv("reason", bind.unsupported ? bind.unsupported : (bind.set ? "unknown_or_freed_set" : "unbound"));
        w.end_object();
        return;
    }
    const auto& set = *contents;
    w.kn("layout", set.layout->id);
    w.kn("generation", set.generation);
    if (set.unsupported) w.kv("unsupported_update", set.unsupported);
    w.key("bindings"); w.begin_array();
    uint32_t dynamic = 0, emitted = 0;
    constexpr uint32_t limit = 256;
    for (const auto& kv : set.layout->bindings) {
        uint32_t binding = kv.first;
        const auto& b = kv.second;
        uint32_t count = set.counts.at(binding);
        w.begin_object();
        w.kn("binding", binding); w.kn("type", b.type); w.kn("count", count);
        w.kn("stages", b.stages); w.kn("flags", b.flags);
        w.kb("immutable_sampler", !b.immutable_samplers.empty());
        w.key("elements"); w.begin_array();
        uint32_t defined = 0, shown = 0;
        auto v = set.values.lower_bound({binding, 0});
        for (; v != set.values.end() && v->first.first == binding; ++v) {
            if (v->second.known) defined++;
            if (emitted >= limit) continue;
            uint32_t element = v->first.second;
            uint32_t offset = is_dynamic(b.type) && dynamic + element < dyn.size() ? dyn[dynamic + element] : 0;
            w.begin_object(); w.kn("element", element);
            write_element(w, dev, v->second, offset);
            w.end_object(); emitted++; shown++;
        }
        w.end_array();
        w.kn("known_elements", defined);
        w.kn("unknown_elements", count - std::min(count, defined));
        w.kn("shown_elements", shown);
        w.end_object();
        if (is_dynamic(b.type)) dynamic += count;
    }
    w.end_array();
    w.kb("truncated", set.values.size() > emitted);
    w.kn("element_limit", limit);
    w.end_object();
}

void write_stages(JsonWriter& w, const PipeInfo& p)
{
    w.kn("layout", p.layout ? p.layout->id : 0);
    w.key("shaders"); w.begin_array();
    for (const auto& s : p.shaders) {
        w.begin_object(); w.kn("stage", s.stage); w.kv("entry", s.entry);
        w.kb("known", s.shader != nullptr);
        if (s.shader) {
            w.kn("id", s.shader->id); w.kn("size", s.shader->code.size() * 4);
            w.key("hash"); w.hex32(s.shader->hash);
        }
        w.end_object();
    }
    w.end_array();
}

std::string probe_shader(uint32_t id, bool with_code, std::vector<uint8_t>& payload)
{
    DeviceRef ref;
    std::shared_ptr<const ShaderInfo> shader;
    if (ref.dev) {
        std::shared_lock<std::shared_mutex> lock(ref.dev->res_mx);
        auto it = ref.dev->shader_by_id.find(id);
        if (it != ref.dev->shader_by_id.end()) shader = it->second.lock();
    }
    JsonWriter w;
    w.begin_object();
    if (!shader) {
        w.kb("ok", false); w.kv("error", "unknown shader id (module and all referencing pipelines may have been destroyed)");
    } else if (with_code && shader->code.size() * 4 > (64ull << 20)) {
        w.kb("ok", false); w.kv("error", "shader exceeds the 64 MiB payload limit");
    } else {
        w.kb("ok", true); w.key("result"); w.begin_object();
        w.kn("id", shader->id); w.kn("size", shader->code.size() * 4);
        w.key("hash"); w.hex32(shader->hash); w.kv("hash_algorithm", "crc32c");
        w.end_object();
        if (with_code) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(shader->code.data());
            payload.assign(bytes, bytes + shader->code.size() * 4);
        }
    }
    w.end_object();
    return w.out;
}

} // namespace efvk
