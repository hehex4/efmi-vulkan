// Native allocation-failure and byte-range checks for private draw snapshots.
#include "../layer/src/draw_snapshots.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

using namespace efvk;
static int passed=0,failed=0;
static void check(bool ok,const char* s) { if(ok) ++passed; else {++failed;printf("FAIL: %s\n",s);} }
template<class T> static T h(uintptr_t n) { return reinterpret_cast<T>(n); }
static std::set<uintptr_t> live;
static uintptr_t next=1;
static uint32_t step=0,fail_step=0;
static VkDeviceSize buffer_size=0,allocation_size=0;
static uint32_t type_bits=1,last_type=0;
static std::vector<uint8_t> mapped;
static std::map<VkDescriptorSet,std::array<VkDescriptorBufferInfo,2>> descriptors;
static std::map<VkDescriptorSet,std::vector<VkImageView>> image_descriptors;
static bool fail() { return ++step==fail_step; }
template<class T> static VkResult create(T* out) {
    if(fail()) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    live.insert(next);*out=h<T>(next++);return VK_SUCCESS;
}
template<class T> static void destroy(T object) { if(live.erase(reinterpret_cast<uintptr_t>(object))!=1) std::abort(); }
static VKAPI_ATTR VkResult VKAPI_CALL buffer(VkDevice,const VkBufferCreateInfo* p,const VkAllocationCallbacks*,VkBuffer* out)
{ buffer_size=p->size; return create(out); }
static VKAPI_ATTR void VKAPI_CALL destroy_buffer(VkDevice,VkBuffer b,const VkAllocationCallbacks*) {destroy(b);}
static VKAPI_ATTR void VKAPI_CALL requirements(VkDevice,VkBuffer,VkMemoryRequirements* out)
{ *out={((buffer_size+4095)/4096)*4096,256,type_bits}; }
static VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice,const VkMemoryAllocateInfo* p,const VkAllocationCallbacks*,VkDeviceMemory* out)
{ allocation_size=p->allocationSize;last_type=p->memoryTypeIndex;return create(out); }
static VKAPI_ATTR void VKAPI_CALL free_memory(VkDevice,VkDeviceMemory m,const VkAllocationCallbacks*) {destroy(m);}
static VKAPI_ATTR VkResult VKAPI_CALL bind(VkDevice,VkBuffer,VkDeviceMemory,VkDeviceSize)
{ return fail()?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS; }
static VKAPI_ATTR VkResult VKAPI_CALL map(VkDevice,VkDeviceMemory,VkDeviceSize,VkDeviceSize,VkMemoryMapFlags,void** out)
{ if(fail())return VK_ERROR_MEMORY_MAP_FAILED;mapped.assign(size_t(allocation_size),0xa5);*out=mapped.data();return VK_SUCCESS; }
static VKAPI_ATTR void VKAPI_CALL unmap(VkDevice,VkDeviceMemory) {}
static VKAPI_ATTR VkResult VKAPI_CALL layout(VkDevice,const VkDescriptorSetLayoutCreateInfo*,const VkAllocationCallbacks*,VkDescriptorSetLayout* out)
{ return create(out); }
static VKAPI_ATTR void VKAPI_CALL destroy_layout(VkDevice,VkDescriptorSetLayout s,const VkAllocationCallbacks*) {destroy(s);}
static VKAPI_ATTR VkResult VKAPI_CALL pool(VkDevice,const VkDescriptorPoolCreateInfo*,const VkAllocationCallbacks*,VkDescriptorPool* out)
{ return create(out); }
static VKAPI_ATTR void VKAPI_CALL destroy_pool(VkDevice,VkDescriptorPool p,const VkAllocationCallbacks*) {destroy(p);descriptors.clear();}
static VKAPI_ATTR VkResult VKAPI_CALL sets(VkDevice,const VkDescriptorSetAllocateInfo* p,VkDescriptorSet* out)
{ if(fail())return VK_ERROR_OUT_OF_POOL_MEMORY;for(uint32_t i=0;i<p->descriptorSetCount;++i)out[i]=h<VkDescriptorSet>(next++);return VK_SUCCESS; }
static VKAPI_ATTR void VKAPI_CALL update(VkDevice,uint32_t count,const VkWriteDescriptorSet* p,uint32_t,const VkCopyDescriptorSet*)
{ for(uint32_t i=0;i<count;++i){
    if(p[i].descriptorType==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)image_descriptors[p[i].dstSet].push_back(p[i].pImageInfo->imageView);
    else descriptors[p[i].dstSet][p[i].dstBinding]=*p[i].pBufferInfo;
} }
static SnapshotDispatch dispatch()
{
    SnapshotDispatch d;
    d.CreateBuffer=buffer;d.DestroyBuffer=destroy_buffer;d.GetBufferMemoryRequirements=requirements;
    d.AllocateMemory=allocate;d.FreeMemory=free_memory;d.BindBufferMemory=bind;d.MapMemory=map;d.UnmapMemory=unmap;
    d.CreateDescriptorSetLayout=layout;d.DestroyDescriptorSetLayout=destroy_layout;
    d.CreateDescriptorPool=pool;d.DestroyDescriptorPool=destroy_pool;d.AllocateDescriptorSets=sets;d.UpdateDescriptorSets=update;
    return d;
}
int main()
{
    VkPhysicalDeviceMemoryProperties memory={};memory.memoryTypeCount=1;
    memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkPhysicalDeviceLimits limits={};limits.maxStorageBufferRange=1<<20;limits.maxUniformBufferRange=65536;
    limits.minStorageBufferOffsetAlignment=128;limits.minUniformBufferOffsetAlignment=1024;
    for(uint32_t f=1;f<=7;++f) {
        step=0;fail_step=f;
        { DrawSnapshots s(h<VkDevice>(1),dispatch());check(!s.create(memory,limits,3,145) && !s.reason.empty(),"allocation stage failure is explicit"); }
        check(live.empty(),"partial creation failure leaves no GPU object");
    }
    fail_step=0;step=0;
    {
        DrawSnapshots s(h<VkDevice>(1),dispatch());
        check(!s.create(memory,limits,0,145) && !s.create(memory,limits,3,145,128) && !s.create(memory,limits,129,145),
              "invalid capacity and small uniform range rejected before allocation");
        auto unavailable=memory;unavailable.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        check(!s.create(unavailable,limits,3,145) && live.empty(),"noncoherent-only memory rejected and buffer released");
        check(s.create(memory,limits,3,145) && s.size()==1024+2048+65536,"combined buffer includes last full uniform window");
        std::vector<uint8_t> bones(145,0x6c);InstanceEntry entry;entry.fill(0x38);
        SnapshotSlot a,b,c,out{h<VkDescriptorSet>(999),999};
        check(!s.append({},entry,out) && out.set==h<VkDescriptorSet>(999) && out.offset==999 && s.used()==0,
              "failed append preserves output and slot count");
        check(s.append(bones,entry,a) && s.append(bones,entry,b) && s.append(bones,entry,c),"three snapshots appended");
        check(a.offset==0 && b.offset==1024 && c.offset==2048,"dynamic offsets respect device alignment");
        bool ranges=true;
        for(const auto slot:{a,b,c}) {
            const auto& ds=descriptors.at(slot.set);
            ranges &= ds[0].offset%128==0 && ds[0].range==145 && ds[1].offset%1024==0 && ds[1].range==65536 &&
                      ds[1].offset+slot.offset+ds[1].range<=buffer_size;
        }
        check(ranges,"every descriptor range including final dynamic offset is inside the buffer");
        bones[0]=0;entry[0]=0;
        check(mapped[descriptors.at(a.set)[0].offset]==0x6c && mapped[descriptors.at(c.set)[1].offset+c.offset]==0x38,
              "snapshot bytes are owned independently of source memory");
        auto saved=mapped;s.seal();
        check(!s.append(bones,entry,out) && !s.create(memory,limits,1,145) && mapped==saved,
              "sealed snapshot never rewrites memory");
        check(mapped[buffer_size-1]==0 && mapped[buffer_size]==0xa5,"uniform tail initialized without writing allocation padding");
    }
    check(live.empty(),"snapshot owner releases pool layout buffer and allocation");
    std::weak_ptr<int> retained;
    {
        DrawSnapshots s(h<VkDevice>(1),dispatch());auto owner=std::make_shared<int>(7);retained=owner;
        check(!s.create(memory,limits,2,145,65536,1),"image snapshot refuses absent device descriptor budget");
        limits.maxPerStageDescriptorSampledImages=limits.maxDescriptorSetSampledImages=limits.maxPerStageResources=32;
        check(s.create(memory,limits,2,145,65536,2),"private image slots allocate alongside VS buffer descriptors");
        std::vector<uint8_t> bones(145,1);InstanceEntry entry{};SnapshotSlot a,b,out{h<VkDescriptorSet>(999),999};
        auto before=mapped;std::vector<SnapshotImage> valid={{h<VkImageView>(91),owner},{h<VkImageView>(92),owner}};
        auto bad=valid;bad[1].owner.reset();
        check(!s.append(bones,entry,bad,out) && s.used()==0 && mapped==before && out.offset==999 && image_descriptors.empty(),
              "missing second image owner refuses before any bytes descriptors or slot change");
        check(!s.append(bones,entry,out),"missing image list cannot publish a partly ready snapshot");
        check(s.append(bones,entry,valid,a),"material snapshot publishes all image descriptors");
        std::swap(valid[0],valid[1]);check(s.append(bones,entry,valid,b),"next draw captures its own image order");
        check(image_descriptors[a.set]==std::vector<VkImageView>{h<VkImageView>(91),h<VkImageView>(92)} &&
              image_descriptors[b.set]==std::vector<VkImageView>{h<VkImageView>(92),h<VkImageView>(91)},"per-draw images are independent");
        s.seal();owner.reset();valid.clear();bad.clear();
        check(!retained.expired() && !s.append(bones,entry,{},out),"sealed owner survives caller retirement and refuses mutation");
    }
    check(retained.expired() && live.empty(),"images release after their last command snapshot owner");
    {
        // Shared slices: draws that read the same bones share one copy of them.
        VkPhysicalDeviceMemoryProperties both=memory;both.memoryTypeCount=2;
        both.memoryTypes[1].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        type_bits=3;
        DrawSnapshots s(h<VkDevice>(1),dispatch());
        check(!s.create_shared(both,limits,4,0,145) && !s.create_shared(both,limits,2000,2,145) && live.empty(),
              "shared storage rejects zero slices and excess entries");
        check(s.create_shared(both,limits,4,2,145) && last_type==1 && s.size()==1024+3*1024+65536,
              "shared storage prefers device memory the host can write; two slices, four entries");
        std::vector<uint8_t> first(145,0x11),second(145,0x22);InstanceEntry entry;entry.fill(0x38);
        SnapshotSlot a,b,c,d,out{h<VkDescriptorSet>(999),999};
        check(!s.append(first,entry,out) && out.offset==999,"per-draw append is refused on shared storage");
        check(!s.append_shared(first.data(),144,7,entry,out) && !s.append_shared(nullptr,145,7,entry,out) && s.used()==0 && s.slices_used()==0,
              "wrong bone size or missing bytes consume nothing");
        check(s.append_shared(first.data(),first.size(),7,entry,a) && s.append_shared(first.data(),first.size(),7,entry,b) && s.slices_used()==1,
              "equal keys share one bone copy");
        check(a.set!=b.set && a.offset==0 && b.offset==1024 && descriptors.at(a.set)[0].offset==0 && descriptors.at(b.set)[0].offset==0 &&
              descriptors.at(a.set)[0].range==145,"draws that share bones keep their own set and entry");
        check(s.append_shared(second.data(),second.size(),8,entry,c) && s.slices_used()==2 && descriptors.at(c.set)[0].offset==256 &&
              mapped[0]==0x11 && mapped[256]==0x22 && mapped[1024+2*1024]==0x38,"a new key takes the next slice and leaves the first intact");
        check(!s.append_shared(first.data(),first.size(),9,entry,out) && s.used()==3 && out.offset==999,
              "exhausted slices refuse without consuming an entry");
        check(s.append_shared(second.data(),second.size(),8,entry,d) && !s.append_shared(second.data(),second.size(),8,entry,out),
              "entries are bounded separately from slices");
        const auto last=descriptors.at(d.set)[1];
        check(last.offset+d.offset+last.range<=s.size(),"the last entry keeps its full uniform window inside the buffer");
        s.seal();
        check(!s.append_shared(second.data(),second.size(),8,entry,out),"sealed shared storage refuses mutation");
        type_bits=1;
    }
    check(live.empty(),"shared storage releases every object");
    {
        VkPhysicalDeviceMemoryProperties host_only=memory;type_bits=1;
        DrawSnapshots s(h<VkDevice>(1),dispatch());
        check(s.create_shared(host_only,limits,2,1,145) && last_type==0,"without device memory the host can write, host memory is used");
    }
    {
        DrawSnapshots s(h<VkDevice>(1),dispatch());
        auto owner=std::make_shared<int>(9);retained=owner;
        check(s.create_shared(memory,limits,3,1,145,65536,1),"shared bones support command-owned material images");
        std::vector<uint8_t> bones(145,0x42);InstanceEntry entry{};SnapshotSlot a,b,out{h<VkDescriptorSet>(999),999};
        auto before=mapped;const auto descriptors_before=image_descriptors;
        check(!s.append_shared(bones.data(),bones.size(),1,entry,{{h<VkImageView>(91),{}}},out) &&
              s.used()==0 && s.slices_used()==0 && mapped==before && image_descriptors==descriptors_before && out.offset==999,
              "invalid shared image consumes no bone slice, bytes, descriptors or output");
        check(s.append_shared(bones.data(),bones.size(),1,entry,{{h<VkImageView>(91),owner}},a) &&
              s.append_shared(bones.data(),bones.size(),1,entry,{{h<VkImageView>(92),owner}},b) && s.slices_used()==1 &&
              image_descriptors[a.set][0]==h<VkImageView>(91) && image_descriptors[b.set][0]==h<VkImageView>(92),
              "two shared-bone draws retain different image bindings");
        s.seal();owner.reset();
        check(!retained.expired() && !s.append_shared(bones.data(),bones.size(),1,entry,out),
              "sealed shared material keeps owners and rejects further writes");
    }
    check(retained.expired() && live.empty(),"shared material resources release with the command owner");
    printf("draw snapshots: %d checks passed, %d failed\n",passed,failed);
    return failed?1:0;
}
