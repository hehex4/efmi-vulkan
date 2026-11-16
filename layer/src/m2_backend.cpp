#include "m2_backend.h"
#include "command_owner.h"
#include "character_backend.h"
#include "character_module.h"
#include "character_package.h"
#include "state.h"
#include "json.h"
#include <algorithm>
#include <filesystem>
#include <shared_mutex>

namespace efvk {
// ---------------------------------------------------------------------------
// The character backend and its exchange (docs/hot-swap.md).
//
// Every call into the backend in use holds Device::character_mx shared, so taking the lock
// exclusively is the moment in which no thread is inside it: the staged backend takes its place
// there, between two draws. The backend that was in use is stopped and destroyed on a thread of
// its own, because that joins threads; what command buffers still hold of it stays alive with them.

namespace {
constexpr uint64_t kStagingMicroseconds = 30ull * 1000 * 1000; // a staged backend that is not ready by then is dropped

struct ModuleState {
    std::mutex mx;
    uint32_t generation = 0;      // exchanges completed in this process
    std::string path, version;    // of the code in use; no path: the copy built into the layer
    std::string staged_path, staged_version, error;
    uint64_t staged_us = 0;
    int enabled = -1;             // what the probe last asked for; -1: the configured value
    std::vector<std::thread> janitors;
} g_module;

void host_log(const char* text) { logf("%s", text); }
uint8_t host_read_buffer(Device* dev, const BufInfo& b, uint64_t off, uint64_t size, uint8_t* out)
{
    return static_cast<uint8_t>(read_buffer_locked(dev, b, off, size, out));
}

void module_status(JsonWriter& w)
{
    std::lock_guard<std::mutex> lock(g_module.mx);
    char stamp[24];
    snprintf(stamp, sizeof(stamp), "%016llx", static_cast<unsigned long long>(EFVK_ABI));
    w.key("module"); w.begin_object();
    w.kn("generation", g_module.generation);
    w.kb("builtin", g_module.path.empty());
    w.kv("version", g_module.version);
    w.kv("path", g_module.path);
    w.kv("staged", g_module.staged_path);
    w.kv("last_error", g_module.error);
    w.kv("abi", stamp);
    w.end_object();
}

// The staged backend takes over (`promote`) or is given up.
void exchange(Device* device, bool promote)
{
    std::shared_ptr<CharacterBackend> old;
    const CharacterModule* old_module = nullptr;
    std::string version;
    {
        std::unique_lock<std::shared_mutex> lock(device->character_mx);
        if (!device->character_staged) return;
        if (promote) {
            old = std::move(device->character); old_module = device->character_module;
            device->character = std::move(device->character_staged); device->character_module = device->staged_module;
            device->character_module->activate(*device->character);
            version = device->character_module->version;
        } else {
            old = std::move(device->character_staged); old_module = device->staged_module;
        }
        device->character_staged.reset(); device->staged_module = nullptr;
    }
    std::lock_guard<std::mutex> lock(g_module.mx);
    if (promote) {
        ++g_module.generation; g_module.path = g_module.staged_path; g_module.version = version; g_module.error.clear();
        logf("replacement module %u in use: %s (%s)", g_module.generation, g_module.path.c_str(), version.c_str());
    } else {
        g_module.error = "staged backend was not ready in time: " + g_module.staged_path;
        logf("replacement module given up, not ready in time: %s", g_module.staged_path.c_str());
    }
    g_module.staged_path.clear(); g_module.staged_version.clear();
    if (old) g_module.janitors.emplace_back([old, old_module]() mutable { old_module->stop(*old); old.reset(); });
}
} // namespace

bool m2_record(Device* device,CmdBuf* cb,const DrawRec& r){
    if(device->character_mode){
        std::shared_lock<std::shared_mutex> lock(device->character_mx);
        return device->character && device->character_module->record(*device->character,cb,r);
    }
    return false;
}

// A non-indexed or indirect draw while the module wants every draw (docs/vulkan-native-mods.md): true drops it.
bool m2_shader_draw(Device* device,CmdBuf* cb,const DrawRec& r){
    if(!device->character_mode || !device->shader_draws.load(std::memory_order_relaxed))return false;
    std::shared_lock<std::shared_mutex> lock(device->character_mx);
    return device->character && device->character_module->shader_draw && device->character_module->shader_draw(*device->character,cb,r);
}

VkPipeline m2_bind(Device* device,CmdBuf* cb,VkPipeline pipeline){
    std::shared_lock<std::shared_mutex> lock(device->character_mx);
    const auto* module=device->character_module?device->character_module:builtin_character_module();
    VkPipeline selected=pipeline;
    if(device->character_mode && device->character && module->bind)
        selected=module->bind(*device->character,cb,pipeline);
    return cb && cb->gfx_pipeline==pipeline?module->native_bind(device,cb,selected):selected;
}
namespace {
template<class Function,class... Args>
void native_callback(Device* device,Function CharacterModule::* callback,Args... args){
    std::shared_lock<std::shared_mutex> lock(device->character_mx);
    const auto* module=device->character_module?device->character_module:builtin_character_module();
    (module->*callback)(device,args...);
}
}
void m2_native_call(Device* d,CmdBuf* cb,bool compute){native_callback(d,&CharacterModule::native_call,cb,compute);}
void m2_native_begin(Device* d,CmdBuf* cb,const VkRenderPassBeginInfo* info){native_callback(d,&CharacterModule::native_begin_classic,cb,info);}
void m2_native_begin(Device* d,CmdBuf* cb,const VkRenderingInfo* info){native_callback(d,&CharacterModule::native_begin_rendering,cb,info);}
void m2_native_end_images(Device* d,CmdBuf* cb){native_callback(d,&CharacterModule::native_end_images,cb);}
void m2_native_end_targets(Device* d,CmdBuf* cb){native_callback(d,&CharacterModule::native_end_targets,cb);}

void m2_created(Device* device,VkPipeline pipeline){
    if(!device->character_mode)return;
    std::shared_lock<std::shared_mutex> lock(device->character_mx);
    if(device->character && device->character_module->created)device->character_module->created(*device->character,pipeline);
}

void m2_pass_end(Device* device,CmdBuf* cb){
    if(!device->character_mode)return;
    std::shared_lock<std::shared_mutex> lock(device->character_mx);
    if(device->character && device->character_module->pass_end)device->character_module->pass_end(*device->character,cb);
}

void m2_create(Device* device,const VkPhysicalDeviceProperties& properties,const VkPhysicalDeviceMemoryProperties& memory)
{
    if(!character_configured() || g_cfg.mode!=Mode::Probe || !g_cfg.track_resources)return;
    device->physical_properties=properties;device->memory_properties=memory;
    device->character_module=builtin_character_module();
    device->character=device->character_module->create(device,properties,memory);
    if(device->character)device->character_module->activate(*device->character);
    device->character_mode=true;
    std::lock_guard<std::mutex> lock(g_module.mx);
    g_module.generation=0;g_module.path.clear();g_module.version=device->character_module->version;
    g_module.staged_path.clear();g_module.error.clear();g_module.enabled=-1;
}

void m2_stop(Device* device){
    if(device->character_mode){
        std::shared_ptr<CharacterBackend> active,staged;
        const CharacterModule* active_module=nullptr;const CharacterModule* staged_module=nullptr;
        {
            std::unique_lock<std::shared_mutex> lock(device->character_mx);
            device->character_stopped=true;
            device->shader_draws.store(false,std::memory_order_relaxed);
            device->native_binding_order.store(false,std::memory_order_relaxed);
            active.swap(device->character);staged.swap(device->character_staged);
            active_module=device->character_module;staged_module=device->staged_module;device->staged_module=nullptr;
        }
        if(active)active_module->stop(*active);
        if(staged)staged_module->stop(*staged);
        // Backends that were exchanged earlier finish their own stop before the device goes.
        std::vector<std::thread> janitors;
        {std::lock_guard<std::mutex> lock(g_module.mx);janitors.swap(g_module.janitors);}
        for(auto& thread:janitors)if(thread.joinable())thread.join();
        active.reset();staged.reset();
    }
}
void m2_retire(Device* device,VkPipeline handle){
    if(!device->character_mode)return;
    uint32_t id=0;
    {std::shared_lock<std::shared_mutex> lock(device->res_mx);auto p=device->pipes.find(handle);if(p!=device->pipes.end())id=p->second.id;}
    if(!id)return;
    if(device->character_mode){
        std::shared_lock<std::shared_mutex> lock(device->character_mx);
        if(device->character)device->character_module->retire(*device->character,id);
        if(device->character_staged)device->staged_module->retire(*device->character_staged,id);
    }
}
void m2_seal(Device* device,uint32_t count,const VkCommandBuffer* buffers){
    for(uint32_t i=0;i<count;++i)if(auto* cb=cb_of(device,buffers[i]))
        for(auto& owner:cb->owners)if(owner)owner->seal();
}
VkResult m2_submit(Device* device,VkQueue queue,SubmitWork& work){
    for(const auto& batch:work.batches)for(auto* cb:batch.cbs)if(cb)
        for(const auto* owners:{&cb->owners,&cb->executed_owners})for(const auto& owner:*owners)if(owner && owner->used){
            const auto result=owner->before_submit(queue);if(result!=VK_SUCCESS)return result;
        }
    if(!device->character_mode)return VK_SUCCESS;
    std::shared_lock<std::shared_mutex> lock(device->character_mx);
    auto put=[&](const std::shared_ptr<CharacterBackend>& backend,const CharacterModule* module){
        const size_t signals=work.signals.size();size_t prefixes=0;
        for(const auto& b:work.batches)prefixes+=b.prefix.size();
        const VkResult result=module->submit(*backend,queue,work);
        size_t after=0;for(const auto& b:work.batches)after+=b.prefix.size();
        if(after!=prefixes || work.signals.size()!=signals)work.owners.emplace_back(backend,module);
        return result;
    };
    if(device->character){
        const VkResult result=put(device->character,device->character_module);
        if(result!=VK_SUCCESS)return result;
    }
    // A staged backend brings its images up through the same queues before it takes over. If that
    // fails it never becomes ready and is given up; the application's submission is not its concern.
    if(device->character_staged)put(device->character_staged,device->staged_module);
    return VK_SUCCESS;
}
void m2_submitted(Device*,VkQueue queue,SubmitWork& work,VkResult result){
    for(auto& owner:work.owners)if(owner.second && owner.second->submitted)owner.second->submitted(*owner.first,queue,work,result);
    work.owners.clear();
}
void m2_frame(Device* device,bool had_draws,PresentWork* present){
    if(!device->character_mode)return;
    bool promote=false,late=false;
    {
        std::shared_lock<std::shared_mutex> lock(device->character_mx);
        if(device->character)device->character_module->frame(*device->character,had_draws,present);
        if(device->character_staged){
            promote=device->staged_module->ready(*device->character_staged);
            if(!promote){
                std::lock_guard<std::mutex> state(g_module.mx);
                late=host_time_us()-g_module.staged_us>kStagingMicroseconds;
            }
        }
    }
    if(promote || late)exchange(device,promote);
}
void m2_status(Device* device,JsonWriter& w){
    if(device && device->character_mode){
        std::shared_lock<std::shared_mutex> lock(device->character_mx);
        w.key("replacement");w.begin_object();
        if(device->character)device->character_module->status(*device->character,w);
        else{w.kv("kind","character");w.kb("configured",false);w.kb("enabled",false);w.kb("ready",false);}
        module_status(w);
        // The layer's work inside the application's batches (D-127).
        w.key("submission_work");w.begin_object();
        w.kn("prefixed_batches",device->prefixed_batches.load());w.kn("widened_waits",device->widened_waits.load());
        w.kn("prefix_alone",device->prefix_alone.load());w.end_object();
        w.kn("present_gpu_waits",device->present_waits.load()); // BUG-062: stays 0
        w.end_object();return;
    }
    w.key("replacement");w.begin_object();
    w.kb("configured",false);w.kb("enabled",false);w.kb("ready",false);
    w.end_object();
}
std::string probe_m2_set(int enabled,bool advance){
    DeviceRef ref;JsonWriter w;w.begin_object();
    if(ref.dev && ref.dev->character_mode){
        if(enabled>=0){
            {std::lock_guard<std::mutex> lock(g_module.mx);g_module.enabled=enabled;}
            std::shared_lock<std::shared_mutex> lock(ref.dev->character_mx);
            if(ref.dev->character)ref.dev->character_module->enable(*ref.dev->character,enabled!=0);
        }
        if(advance)m2_frame(ref.dev,true);
        w.kb("ok",true);w.key("result");w.begin_object();m2_status(ref.dev,w);w.end_object();
    }
    else {w.kb("ok",false);w.kv("error","Replacement backend is not configured");}
    w.end_object();return w.out;
}
std::string probe_m2_module(const std::string& path,const std::string& package){
    DeviceRef ref;JsonWriter w;w.begin_object();
    Device* device=ref.dev;
    auto fail=[&](const std::string& why){
        {std::lock_guard<std::mutex> lock(g_module.mx);g_module.error=why+": "+path;}
        logf("replacement module refused: %s: %s",why.c_str(),path.c_str());
        w.kb("ok",false);w.kv("error",why);w.end_object();return w.out;
    };
    if(!device || !device->character_mode)return fail("this process has no character backend");
    {
        std::shared_lock<std::shared_mutex> lock(device->character_mx);
        if(device->character_staged)return fail("another module is being staged");
    }
    std::wstring wide(path.size()+1,L'\0');
    const int chars=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,&wide[0],static_cast<int>(wide.size()));
    if(path.empty() || chars<=0)return fail("module path is not valid UTF-8");
    if(!package.empty())return fail("Per-package module loading has been removed; configure the root INI installation");
    const HMODULE library=LoadLibraryW(wide.c_str());
    if(!library)return fail("module cannot be loaded, error "+std::to_string(GetLastError()));
    const auto entry=reinterpret_cast<CharacterModuleEntry>(GetProcAddress(library,EFVK_MODULE_ENTRY));
    // One staging at a time; the module copies the settings while its entry runs and keeps the
    // table of functions, which therefore lives as long as the process.
    static HostApi api;
    Config config=g_cfg;
    api.size=sizeof(HostApi);api.abi=EFVK_ABI;api.config=&config;api.log=host_log;api.crc32c=crc32c;
    api.read_buffer_locked=host_read_buffer;api.collect_buffer_ranges_locked=collect_buffer_ranges_locked;
    api.gpu_bones=[]{return g_cfg.m2_gpu_bones;};
    api.notice=[](int level,const char* key,const char* text){notice(static_cast<NoticeLevel>(level),key,text);};
    const CharacterModule* module=entry?entry(&api):nullptr;
    api.config=nullptr;
    if(!module || module->size!=sizeof(CharacterModule) || module->abi!=EFVK_ABI || !module->activate ||
       !module->native_bind || !module->native_call || !module->native_begin_classic ||
       !module->native_begin_rendering || !module->native_end_images || !module->native_end_targets){
        // Nothing of it ran beyond the entry, so it may go again.
        FreeLibrary(library);
        return fail("module was built against other structures of the layer; this layer cannot use it");
    }
    std::shared_ptr<CharacterBackend> staged=module->create(device,device->physical_properties,device->memory_properties);
    std::string reason;
    if(!staged || !module->usable(*staged,reason)){
        if(staged){module->stop(*staged);staged.reset();}
        return fail("module's backend refused: "+reason);
    }
    int enabled=-1;
    {std::lock_guard<std::mutex> lock(g_module.mx);enabled=g_module.enabled;}
    if(enabled>=0)module->enable(*staged,enabled!=0);
    {
        std::unique_lock<std::shared_mutex> lock(device->character_mx);
        if(device->character_staged || device->character_stopped){
            const bool stopped=device->character_stopped;
            lock.unlock();module->stop(*staged);staged.reset();
            return fail(stopped?"the device is being destroyed":"another module is being staged");
        }
        device->character_staged=staged;device->staged_module=module;
    }
    {
        std::lock_guard<std::mutex> lock(g_module.mx);
        g_module.staged_path=path;g_module.staged_version=module->version;g_module.staged_us=host_time_us();g_module.error.clear();
    }
    logf("replacement module staged: %s (%s)",path.c_str(),module->version);
    w.kb("ok",true);w.key("result");w.begin_object();w.kb("staged",true);w.kv("version",module->version);module_status(w);w.end_object();
    w.end_object();return w.out;
}
} // namespace efvk
