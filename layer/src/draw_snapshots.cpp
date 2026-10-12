#include "draw_snapshots.h"
#include <algorithm>

namespace efvk {
namespace {
VkDeviceSize aligned(VkDeviceSize value, VkDeviceSize alignment)
{
    alignment = std::max<VkDeviceSize>(alignment, 1);
    return (value + alignment - 1) / alignment * alignment;
}
}
DrawSnapshots::~DrawSnapshots() { clear(); }
void DrawSnapshots::clear()
{
    if (pool_) d_.DestroyDescriptorPool(device_, pool_, nullptr);
    if (layout_) d_.DestroyDescriptorSetLayout(device_, layout_, nullptr);
    if (mapped_) d_.UnmapMemory(device_, memory_);
    if (buffer_) d_.DestroyBuffer(device_, buffer_, nullptr);
    if (memory_) d_.FreeMemory(device_, memory_, nullptr);
    pool_ = VK_NULL_HANDLE; layout_ = VK_NULL_HANDLE; buffer_ = VK_NULL_HANDLE; memory_ = VK_NULL_HANDLE;
    mapped_ = nullptr; sets_.clear(); bytes_ = 0; capacity_ = used_ = 0;
}

bool DrawSnapshots::create(const VkPhysicalDeviceMemoryProperties& memory, const VkPhysicalDeviceLimits& limits,
                           uint32_t capacity, uint32_t bone_bytes, uint32_t uniform_range)
{
    reason.clear();
    if (buffer_ || sealed_) { reason = "snapshot storage cannot be recreated"; return false; }
    if (!capacity || capacity > 128 || !bone_bytes || bone_bytes > limits.maxStorageBufferRange ||
        uniform_range < 256 || uniform_range > limits.maxUniformBufferRange || uniform_range > 65536 ||
        limits.minStorageBufferOffsetAlignment > (1u<<20) || limits.minUniformBufferOffsetAlignment > (1u<<20)) {
        reason = "snapshot dimensions exceed supported limits"; return false;
    }
    bone_stride_ = aligned(bone_bytes, limits.minStorageBufferOffsetAlignment);
    entry_base_ = aligned(bone_stride_ * capacity, limits.minUniformBufferOffsetAlignment);
    entry_stride_ = aligned(256, limits.minUniformBufferOffsetAlignment);
    bytes_ = entry_base_ + entry_stride_ * (capacity - 1) + uniform_range;
    if (bytes_ > (64ull<<20)) { reason = "snapshot byte budget exceeded"; bytes_ = 0; return false; }
    auto good = [&](VkResult result, const char* label) {
        if (result == VK_SUCCESS) return true;
        reason = label; clear(); return false;
    };
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = bytes_; bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (!good(d_.CreateBuffer(device_, &bi, nullptr, &buffer_), "create snapshot buffer")) return false;
    VkMemoryRequirements req; d_.GetBufferMemoryRequirements(device_, buffer_, &req);
    if (req.size > (128ull<<20)) { reason = "snapshot allocation budget exceeded"; clear(); return false; }
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) &&
            (memory.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { type = i; break; }
    if (type == UINT32_MAX) { reason = "host-coherent snapshot memory unavailable"; clear(); return false; }
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size; ai.memoryTypeIndex = type;
    if (!good(d_.AllocateMemory(device_, &ai, nullptr, &memory_), "allocate snapshot memory")) return false;
    if (!good(d_.BindBufferMemory(device_, buffer_, memory_, 0), "bind snapshot memory")) return false;
    void* mapped = nullptr;
    if (!good(d_.MapMemory(device_, memory_, 0, VK_WHOLE_SIZE, 0, &mapped), "map snapshot memory")) return false;
    mapped_ = static_cast<uint8_t*>(mapped);
    std::memset(mapped_, 0, static_cast<size_t>(bytes_));
    VkDescriptorSetLayoutBinding bindings[] = {
        {0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr}
    };
    VkDescriptorSetLayoutCreateInfo li = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 2; li.pBindings = bindings;
    if (!good(d_.CreateDescriptorSetLayout(device_, &li, nullptr, &layout_), "create snapshot layout")) return false;
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,capacity},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,capacity}};
    VkDescriptorPoolCreateInfo pi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = capacity; pi.poolSizeCount = 2; pi.pPoolSizes = sizes;
    if (!good(d_.CreateDescriptorPool(device_, &pi, nullptr, &pool_), "create snapshot pool")) return false;
    std::vector<VkDescriptorSetLayout> layouts(capacity,layout_); sets_.resize(capacity);
    VkDescriptorSetAllocateInfo si = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    si.descriptorPool = pool_; si.descriptorSetCount = capacity; si.pSetLayouts = layouts.data();
    if (!good(d_.AllocateDescriptorSets(device_, &si, sets_.data()), "allocate snapshot sets")) return false;
    for (uint32_t i = 0; i < capacity; ++i) {
        VkDescriptorBufferInfo infos[] = {{buffer_,bone_stride_*i,bone_bytes},{buffer_,entry_base_,uniform_range}};
        VkWriteDescriptorSet writes[2] = {};
        for (uint32_t j = 0; j < 2; ++j) {
            writes[j].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[j].dstSet=sets_[i];
            writes[j].dstBinding=j; writes[j].descriptorCount=1; writes[j].descriptorType=bindings[j].descriptorType;
            writes[j].pBufferInfo=&infos[j];
        }
        d_.UpdateDescriptorSets(device_,2,writes,0,nullptr);
    }
    capacity_ = capacity; bone_bytes_ = bone_bytes;
    return true;
}

bool DrawSnapshots::append(const std::vector<uint8_t>& bones, const InstanceEntry& entry, SnapshotSlot& output)
{
    reason.clear();
    if (sealed_ || used_ >= capacity_ || bones.size() != bone_bytes_ || !mapped_) {
        reason = "snapshot is sealed, full, uncreated or has mismatched bone bytes"; return false;
    }
    std::memcpy(mapped_ + bone_stride_ * used_, bones.data(), bones.size());
    std::memcpy(mapped_ + entry_base_ + entry_stride_ * used_, entry.data(), entry.size());
    output = {sets_[used_], static_cast<uint32_t>(entry_stride_ * used_)};
    ++used_;
    return true;
}
} // namespace efvk
