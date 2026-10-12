#include "m2_backend.h"
#include "m2_mesh.h"
#include "state.h"
#include "json.h"
#include <algorithm>
#include <chrono>

namespace efvk {
struct M2GpuMesh {
    VkDevice device;
    SnapshotDispatch d;
    VkBuffer buffer=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    void* mapped=nullptr;
    DrawGeometry geometry;
    M2GpuMesh(VkDevice dev,const SnapshotDispatch& dispatch):device(dev),d(dispatch){}
    ~M2GpuMesh(){
        if(mapped)d.UnmapMemory(device,memory);
        if(buffer)d.DestroyBuffer(device,buffer,nullptr);
        if(memory)d.FreeMemory(device,memory,nullptr);
    }
    bool create(const M2Mesh& mesh,const VkPhysicalDeviceMemoryProperties& properties,std::string& reason){
        auto good=[&](VkResult result,const char* text){if(result==VK_SUCCESS)return true;reason=text;return false;};
        uint64_t offsets[]={0,mesh.position.size(),mesh.position.size()+mesh.texcoord.size(),
                           mesh.position.size()+mesh.texcoord.size()+mesh.blend.size()};
        VkBufferCreateInfo ci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        ci.size=offsets[3]+mesh.index.size();ci.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if(!good(d.CreateBuffer(device,&ci,nullptr,&buffer),"create mesh buffer"))return false;
        VkMemoryRequirements req;d.GetBufferMemoryRequirements(device,buffer,&req);
        if(req.size>(32ull<<20)){reason="mesh allocation budget exceeded";return false;}
        uint32_t type=UINT32_MAX;
        for(uint32_t i=0;i<properties.memoryTypeCount;++i)
            if((req.memoryTypeBits&(1u<<i)) && (properties.memoryTypes[i].propertyFlags &
               (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
               (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}
        if(type==UINT32_MAX){reason="host-coherent mesh memory unavailable";return false;}
        VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;
        if(!good(d.AllocateMemory(device,&ai,nullptr,&memory),"allocate mesh memory") ||
           !good(d.BindBufferMemory(device,buffer,memory,0),"bind mesh memory") ||
           !good(d.MapMemory(device,memory,0,VK_WHOLE_SIZE,0,&mapped),"map mesh memory"))return false;
        const std::vector<uint8_t>* streams[]={&mesh.position,&mesh.texcoord,&mesh.blend,&mesh.index};
        for(uint32_t i=0;i<4;++i)std::memcpy(static_cast<uint8_t*>(mapped)+offsets[i],streams[i]->data(),streams[i]->size());
        geometry={0,{buffer,buffer,buffer,buffer},{offsets[0],offsets[1],offsets[2],offsets[0]},buffer,offsets[3],VK_INDEX_TYPE_UINT32};
        return true;
    }
};

class M2Backend {
public:
    Device* device;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory;
    VariantDispatch draw;
    SnapshotDispatch storage;
    M2Mesh profile;
    std::unique_ptr<VariantCache> cache;
    std::atomic<bool> enabled{false}, ready{false};
    std::atomic<uint64_t> matched{0},replaced{0},pending{0},unsupported{0},unreadable{0},exhausted{0},batches{0};
    std::mutex mutex;
    std::condition_variable wake;
    std::string last_reason;
    bool stopped=false;
    std::thread worker;
    std::shared_ptr<const M2GpuMesh> mesh;
    std::deque<std::shared_ptr<DrawSnapshots>> available;
    std::vector<std::weak_ptr<DrawSnapshots>> all;
    M2Backend(Device* dev,const VkPhysicalDeviceProperties& p,const VkPhysicalDeviceMemoryProperties& m)
        :device(dev),properties(p),memory(m){}
    ~M2Backend(){stop();}
    void note(const std::string& reason){std::lock_guard<std::mutex> lock(mutex);last_reason=reason;}
    bool init(){
        if(!profile.load(g_cfg.m2_package,last_reason))return false;
#define X(n) draw.n=reinterpret_cast<PFN_vk##n>(device->next_gdpa(device->handle,"vk" #n));
        EFVK_VARIANT_FUNCS(X)
#undef X
#define X(n) storage.n=reinterpret_cast<PFN_vk##n>(device->next_gdpa(device->handle,"vk" #n));if(!storage.n){last_reason="missing vk" #n;return false;}
        EFVK_SNAPSHOT_FUNCS(X)
#undef X
        // RenderPass2 is optional; the recipe builder rejects it if the actual function is absent.
        if(!draw.CreateGraphicsPipelines || !draw.CreateRenderPass || !draw.CmdDrawIndexed){last_reason="missing graphics entry";return false;}
        cache=std::make_unique<VariantCache>(device->handle,draw,properties.limits.maxBoundDescriptorSets,128,properties.limits);
        enabled=g_cfg.m2_enabled;
        worker=std::thread([this]{prepare();});return true;
    }
    void stop(){
        {std::lock_guard<std::mutex> lock(mutex);stopped=true;enabled=false;wake.notify_all();}
        if(worker.joinable())worker.join();
        if(cache)cache->stop();
        std::deque<std::shared_ptr<DrawSnapshots>> old;
        std::shared_ptr<const M2GpuMesh> old_mesh;
        {std::lock_guard<std::mutex> lock(mutex);old.swap(available);old_mesh.swap(mesh);ready=false;}
    }
    void prepare(){
        try{
            auto created=std::make_shared<M2GpuMesh>(device->handle,storage);std::string reason;
            if(!created->create(profile,memory,reason)){note(reason);return;}
            {std::lock_guard<std::mutex> lock(mutex);mesh=created;}
            while(true){
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    all.erase(std::remove_if(all.begin(),all.end(),[](const auto& p){return p.expired();}),all.end());
                    if(stopped)return;
                    if(available.size()>=2 || all.size()>=8){wake.wait_for(lock,std::chrono::milliseconds(100));continue;}
                }
                auto snapshots=std::make_shared<DrawSnapshots>(device->handle,storage);
                uint32_t bytes=(512+2*profile.bones)*48;
                if(!snapshots->create(memory,properties.limits,16,bytes)){note(snapshots->reason);return;}
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if(stopped)return;
                    all.push_back(snapshots);available.push_back(std::move(snapshots));ready=true;
                }
            }
        }catch(const std::exception& e){note(e.what());}
    }
    std::shared_ptr<M2Command> command(){
        std::lock_guard<std::mutex> lock(mutex);
        if(stopped || available.empty() || !mesh)return {};
        auto cmd=std::make_shared<M2Command>();cmd->snapshots=available.front();available.pop_front();cmd->mesh=mesh;
        wake.notify_one();return cmd;
    }
    bool record(CmdBuf*,const DrawRec&);
};

bool M2Backend::record(CmdBuf* cb,const DrawRec& r)
{
    if(!enabled.load(std::memory_order_relaxed) || !cache || r.count!=profile.match_count || r.first!=profile.match_first)
        return false;
    auto refuse=[&](std::atomic<uint64_t>& counter,const std::string& reason){++counter;note(reason);return false;};
    InstanceEntry entry;
    std::vector<uint8_t> current((profile.component_count+1)*48),previous(current.size());
    VariantResult variant;
    DrawGeometry original;
    {
        std::shared_lock<std::shared_mutex> lock(device->res_mx);
        auto ib=device->bufs.find(r.ib);
        if(ib==device->bufs.end() || (r.index_size!=2 && r.index_size!=4))return false;
        uint64_t start=uint64_t(r.first)*r.index_size,bytes=uint64_t(r.count)*r.index_size;
        if(r.ib_off>ib->second.size || start>ib->second.size-r.ib_off || bytes>ib->second.size-r.ib_off-start)return false;
        std::vector<uint8_t> indices(static_cast<size_t>(bytes));
        if(read_buffer_locked(device,ib->second,r.ib_off+start,bytes,indices.data())!=SRC_MAPPED ||
           crc32c(0,indices.data(),indices.size())!=profile.hash)return false;
        ++matched;
        if(cb->secondary || !cb->in_rp || cb->dyn_rp_count || cb->m2_extended_bind || r.instances!=1 ||
           r.first_instance!=0 || r.vertex_offset!=0 || r.sets[3].set || r.sets[3].pushed)
            return refuse(unsupported,"M2 requires primary classic direct single-instance draws and no live set 3 or extended binds");
        if(!ready.load())return refuse(pending,"M2 resources are preparing");
        auto pipe=device->pipes.find(r.pipeline);
        if(pipe==device->pipes.end() || !pipe->second.recipe || !pipe->second.layout)
            return refuse(unsupported,"M2 source pipeline metadata missing");
        VertexInputRecipe vertex;std::string reason;
        if(!profile.vertex_input(pipe->second.recipe->vertex,vertex,reason))return refuse(unsupported,reason);
        variant=cache->request(pipe->second.id,pipe->second.recipe,pipe->second.layout,vertex);
        if(!variant.variant){
            if(variant.state==VariantState::Queued || variant.state==VariantState::Building)
                return refuse(pending,"M2 variant is preparing");
            return refuse(unsupported,"M2 variant: "+variant.reason);
        }
        std::vector<BoundRange> ranges;
        for(uint32_t set: {2u,variant.variant->bone_set}){
            const auto& bound=r.sets[set];
            if(bound.dyn_count && uint64_t(bound.dyn_first)+bound.dyn_count>cb->dyn.size())
                return refuse(unreadable,"M2 dynamic offsets incomplete");
            collect_buffer_ranges_locked(device,bound,bound.dyn_count?cb->dyn.data()+bound.dyn_first:nullptr,bound.dyn_count,set,ranges);
        }
        const BoundRange *instance=nullptr,*bones=nullptr;
        for(const auto& range:ranges){
            if(range.element!=0)continue;
            if(range.set==2 && range.binding==0 && (range.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
               range.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC))instance=&range;
            if(range.set==variant.variant->bone_set && range.binding==variant.variant->bone_binding &&
               (range.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || range.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC))bones=&range;
        }
        auto read_range=[&](const BoundRange* range,uint64_t offset,size_t size,uint8_t* output){
            if(!range || offset>range->size || size>range->size-offset)return false;
            auto buffer=device->bufs.find(range->buffer);
            return buffer!=device->bufs.end() && read_buffer_locked(device,buffer->second,range->off+offset,size,output)==SRC_MAPPED;
        };
        if(!read_range(instance,0,entry.size(),entry.data()))return refuse(unreadable,"M2 instance entry is not mapped");
        uint32_t offsets[2];std::memcpy(offsets,entry.data()+80,8);
        if(offsets[0]<MergedSkeleton::kStaticRows || offsets[1]<MergedSkeleton::kStaticRows)
            return refuse(unsupported,"M2 initial component path requires two dynamic poses");
        if(!read_range(bones,uint64_t(offsets[0])*16,current.size(),current.data()) ||
           !read_range(bones,uint64_t(offsets[1])*16,previous.size(),previous.data()))
            return refuse(unreadable,"M2 component palette is not mapped");
        original.index=r.ib;original.index_offset=r.ib_off;
        original.index_type=r.index_size==2?VK_INDEX_TYPE_UINT16:VK_INDEX_TYPE_UINT32;
        for(uint32_t i=0;i<4;++i){
            if(!r.vb[i].buf)return refuse(unsupported,"M2 original vertex bindings incomplete");
            original.buffers.push_back(r.vb[i].buf);original.offsets.push_back(r.vb[i].off);
        }
    }
    // This first package is restricted to a component whose vertices use only its own palette.
    // Snapshot each draw independently, avoiding cross-submit master updates and instance mixing.
    MergedSkeleton skeleton;std::string reason;std::vector<BoneComponent> components;
    if(profile.component_first)components.push_back({0,profile.component_first});
    uint32_t selected=static_cast<uint32_t>(components.size());
    components.push_back({profile.component_first,profile.component_count});
    uint32_t end=profile.component_first+profile.component_count;
    if(end<profile.bones)components.push_back({end,profile.bones-end});
    InstanceEntry patched;
    if(!skeleton.configure(profile.bones,1,components,reason) || !skeleton.begin_update(0,reason) ||
       skeleton.import_component(0,selected,0,entry,{current.data(),current.size()},{previous.data(),previous.size()}, {},1,reason)==BoneImportResult::Rejected ||
       !skeleton.patch_entry(0,entry,patched,reason))return refuse(unreadable,"M2 bone import: "+reason);
    if(!cb->m2)cb->m2=command();
    if(!cb->m2)return refuse(exhausted,"M2 snapshot pool is at capacity or refilling");
    SnapshotSlot slot;
    if(!cb->m2->snapshots->append(skeleton.bytes(),patched,slot))return refuse(exhausted,cb->m2->snapshots->reason);
    // Establish ownership before recording any private handle. No allocations after the draw
    // transaction starts, so an exception cannot cause an original draw after a partial replacement.
    cb->m2->variants.push_back(variant.variant);
    IndexedDraw draw_indices;draw_indices.count=profile.indices;
    if(!emit_variant_draw(draw,cb->handle,*variant.variant,slot.set,slot.offset,cb->m2->mesh->geometry,
                          draw_indices,r.pipeline,original))return refuse(unsupported,"M2 draw transaction refused");
    ++replaced;return true;
}

bool m2_record(Device* device,CmdBuf* cb,const DrawRec& r){
    if(!device->m2)return false;
    try{return device->m2->record(cb,r);}
    catch(const std::exception& e){++device->m2->unsupported;device->m2->note(e.what());return false;}
}

void m2_create(Device* device,const VkPhysicalDeviceProperties& properties,const VkPhysicalDeviceMemoryProperties& memory)
{
    if(g_cfg.m2_package.empty() || g_cfg.mode!=Mode::Probe || !g_cfg.track_resources)return;
    try{
        auto backend=std::make_shared<M2Backend>(device,properties,memory);
        device->m2=backend;
        if(!backend->init()){logf("M2 initialization refused: %s",backend->last_reason.c_str());return;}
        logf("M2 package configured, match=%08x indices=%u enabled=%u; resources and variants prepare asynchronously",
             backend->profile.hash,backend->profile.match_count,backend->enabled.load()?1:0);
    }catch(const std::exception& e){
        if(device->m2){device->m2->enabled=false;device->m2->note(e.what());}
        logf("M2 initialization exception: %s",e.what());
    }
}
void m2_stop(Device* device){if(device->m2)device->m2->stop();}
void m2_retire(Device* device,VkPipeline handle){
    if(!device->m2 || !device->m2->cache)return;
    uint32_t id=0;
    {std::shared_lock<std::shared_mutex> lock(device->res_mx);auto p=device->pipes.find(handle);if(p!=device->pipes.end())id=p->second.id;}
    if(id)device->m2->cache->retire(id);
}
void m2_seal(Device* device,uint32_t count,const VkCommandBuffer* buffers){
    if(!device->m2)return;
    for(uint32_t i=0;i<count;++i)if(auto* cb=cb_of(device,buffers[i]))
        if(cb->m2 && !cb->m2->snapshots->sealed()){cb->m2->snapshots->seal();++device->m2->batches;}
}
void m2_status(Device* device,JsonWriter& w){
    w.key("replacement");w.begin_object();auto backend=device?device->m2:nullptr;
    w.kb("configured",bool(backend));w.kb("enabled",backend && backend->enabled.load());
    w.kb("ready",backend && backend->ready.load());
    if(backend){
        w.kn("matched",backend->matched.load());w.kn("replaced",backend->replaced.load());w.kn("pending",backend->pending.load());
        w.kn("unsupported",backend->unsupported.load());w.kn("unreadable",backend->unreadable.load());
        w.kn("exhausted",backend->exhausted.load());w.kn("sealed_batches",backend->batches.load());
        std::lock_guard<std::mutex> lock(backend->mutex);w.kv("last_reason",backend->last_reason);
        w.kn("available_batches",backend->available.size());
    }
    w.end_object();
}
std::string probe_m2_set(int enabled){
    DeviceRef ref;JsonWriter w;w.begin_object();
    if(!ref.dev || !ref.dev->m2 || !ref.dev->m2->cache){w.kb("ok",false);w.kv("error","M2 package is not configured");}
    else{
        if(enabled>=0)ref.dev->m2->enabled=enabled!=0;
        w.kb("ok",true);w.key("result");w.begin_object();m2_status(ref.dev,w);w.end_object();
    }
    w.end_object();return w.out;
}
} // namespace efvk
