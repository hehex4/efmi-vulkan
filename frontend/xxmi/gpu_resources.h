#pragma once
#include "runtime_host.h"
#include "image_rules.h"
#include "resource_match.h"
#include "../gpu/resource_commands.h"
#include <map>
#include <set>
#include <tuple>
#include <optional>
#include <cmath>

namespace efmi::frontend {
struct ApplicationResourceState;
struct GpuResourceBinding {
    enum class View {None,ShaderResource,UnorderedAccess,RenderTarget,DepthStencil};
    gpu::Buffer buffer;
    gpu::Image image;
    gpu::ImageView image_view;
    // Native view provenance survives named references across application calls.
    // It is reused only when allocation, format, type and complete range match.
    std::shared_ptr<const NativeResource> native_view;
    std::shared_ptr<gpu::ResourceInitialization> initialization;
    ImageSourceDescription image_description;
    D3D11_BUFFER_DESC description{};
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    uint32_t stride=0,source_stride=0,offset=0,visible_size=0;
    bool region=false;
    View view=View::None;
    DXGI_FORMAT view_format=DXGI_FORMAT_UNKNOWN;
    uint32_t view_offset=0,view_size=0;
    explicit operator bool() const {return bool(buffer) || bool(image);}
    UINT bind_flags() const {return image?image_description.texture.BindFlags:description.BindFlags;}
    void drop_view(){view=View::None;image_view.reset();}
};
struct PreparedDrawState {
    std::map<std::tuple<wchar_t,ResourceCopyTargetType,unsigned>,GpuResourceBinding> bindings;
    D3D11_VIEWPORT viewport{};
    std::map<gpu::Buffer,gpu::BufferResource::IndexBounds,std::owner_less<gpu::Buffer>> indices;
    std::set<const void*> initial_contents;
    bool admitted() const {return admission_ && *admission_;}
    const std::shared_ptr<const bool>& admission() const {return admission_;}
private:
    friend class GpuResourceRuntime;
    std::shared_ptr<const bool> admission_;
};
// D3D11 hazards operate on source subresources, not Vulkan byte intervals.
bool source_bindings_overlap(const GpuResourceBinding&,const GpuResourceBinding&) noexcept;
bool source_attachments_compatible(const GpuResourceBinding&,const GpuResourceBinding&);
// Original INI adapter for owned mutable resources. Stage slots are logical
// source-API bindings; graphics/compute descriptor emission is a separate host
// responsibility. They must not be mistaken for changed application bindings.
// One instance per frontend generation, sharing budget ledgers across reloads.
class GpuResourceRuntime final : public RuntimeExtension {
public:
    // Deferred: recording without a queue yet, on a thread that must not touch it (a generation
    // loading in the background); finish() names the queue. Store refuses there.
    enum class ExecutionSite { OutsideRenderPass, InsideRenderPass, Deferred, Prepared };
    GpuResourceRuntime(gpu::ResourceDevice,gpu::ResourceBudgets={});
    ~GpuResourceRuntime();
    std::vector<std::wstring> install(DrawPlanAdapter&) override;
    bool initialize_parameters(CommandListState&,std::wstring&) override;
    using DrawExecutor=std::function<bool(DrawCommand&,CommandListState&,std::wstring&)>;
    // A shader host may execute draws in this same explicit queue domain. The
    // original parameter flush runs before this callback, including count zero.
    void draw_commands(DrawExecutor);
    void execution_failed() noexcept override {if(active_)failed_=true;}
    // The host owns this queue's synchronization for the complete scope. Store
    // may submit and wait for GPU feedback inside it. A draw hook cannot opt in
    // by silently treating an active render pass as OutsideRenderPass.
    void begin(VkQueue,ExecutionSite);
    // Speculative buffer/owned-image/pool execution without pending batches or
    // queue access. Finish seals commands but keeps the registry undo
    // journal until host admission calls accept_prepared(), or discard rolls back.
    void begin_prepared();
    struct ApplicationBufferBinding {
        wchar_t stage=0;
        ResourceCopyTargetType type=ResourceCopyTargetType::INVALID;
        unsigned slot=0;
        NativeResource resource;
        // Proven source description and view format, never inferred from a
        // Vulkan storage binding. resource.offset/range describe the view or IA.
        bool source_known=false;
        D3D11_BUFFER_DESC description{};
        DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
        uint32_t stride=0;
        std::shared_ptr<const SourceResourceIdentity> identity;
        std::shared_ptr<const GpuResourceBinding> retained;
    };
    struct ApplicationImageSource {
        uint64_t resource_id=0;
        bool source_known=false;
        ImageSourceDescription description;
        gpu::ApplicationImage allocation;
        std::shared_ptr<const GpuResourceBinding> retained;
        std::shared_ptr<const SourceResourceIdentity> identity;
    };
    struct ApplicationImageBinding {
        wchar_t stage=0;
        ResourceCopyTargetType type=ResourceCopyTargetType::INVALID;
        unsigned slot=0;
        // Null means explicitly unbound; source facts are shared across views.
        std::shared_ptr<const ApplicationImageSource> source;
        std::shared_ptr<const NativeResource> native_view;
        bool view_known=false,volume_full_depth=false;
        DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
        VkImageViewType view_type=VK_IMAGE_VIEW_TYPE_2D;
        VkImageSubresourceRange range{};
        UINT flags=0; // Read-only depth views need their own original hazard rules.
    };
    using ApplicationResourceState=frontend::ApplicationResourceState;
    void begin_prepared(const ApplicationResourceState&);
    // Same transaction, but root source calls become ordered retained plan items.
    // CustomShader compute still records into the surrounding GPU segments.
    void begin_prepared_plan(const ApplicationResourceState&);
    // Cut without admitting any registry changes or releasing the undo journal.
    std::shared_ptr<const gpu::PreparedResourceCommands> cut_prepared();
    std::shared_ptr<const gpu::PreparedResourceCommands> finish_prepared();
    void accept_prepared();
    bool scope_active() const {return active_;}
    // Whether the active scope's recorded work is to be dropped: a command failed after it had recorded
    // part of its work, or the execution around the scope failed. finish() refuses such a scope;
    // discard() or suspend() drops its work and the next scope starts clean. The failure is the scope's:
    // it ended the whole generation's owned work before, which in a folder generation is every Mod's.
    bool failed() const {return failed_;}
    ExecutionSite site() const {return site_;}
    VkQueue queue() const {return queue_;}
    // Another queue of the family once the host waited for every earlier submission (D-090).
    void rebind(VkQueue queue){
        if(active_ || prepared_state_ || !queue)throw std::runtime_error("GPU resource scope rebinds only while closed and admitted");
        queue_=queue;commands_.rebind(queue);
    }
    bool pending() const {return commands_.pending();}
    // Closes the scope without submitting: recorded work stays pending for the next scope's
    // finish(). A generation loaded in the background is suspended until its first Present.
    void suspend();
    std::shared_ptr<gpu::ResourceSubmission> finish(const gpu::ResourceCommands::SubmitSync& sync={},VkQueue queue=VK_NULL_HANDLE);
    // Discards unsubmitted work. Like the original interpreter, earlier CPU
    // assignments and already-submitted store feedback are not rolled back.
    void discard();
    GpuResourceBinding binding(wchar_t stage,ResourceCopyTargetType,unsigned slot) const;
    // The host's current back buffer, which bb, r_bb and f_bb return: without upscaling the
    // original hands out the swap chain's current buffer for all three. The host sets it before
    // each Present it executes; without one they read as null, as with no swap chain.
    void backbuffer(gpu::Image,DXGI_FORMAT,UINT bind_flags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
    // Whether the scope since the last begin() bound the back buffer as a render target or an
    // unordered access view, or copied into it: what the host may have to draw again for a
    // presentation of the same frame (BUG-036).
    bool backbuffer_targeted() const {return backbuffer_targeted_;}
    // Source-semantic target resolution for an adapted draw in an active scope.
    GpuResourceBinding resolve(ResourceCopyTarget&,CommandListState&);
    // Attach proven native tracking facts to an owned allocation. Reference
    // aliases share them; newly allocated byte copies remain untracked. The
    // host updates the snapshot whenever its source hash policy changes it.
    // This neither imports application resources nor computes a DX11 hash.
    void identify_resource(const GpuResourceBinding&,SourceResourceIdentity);
    const D3D11_VIEWPORT& viewport() const {return viewport_;}
    const std::optional<VkViewport>& native_viewport() const {return native_viewport_;}
    bool viewport_known() const {return viewport_known_;}
    void application_viewport(const VkViewport* value){
        viewport_={};native_viewport_.reset();viewport_known_=value!=nullptr;
        if(value){native_viewport_=*value;viewport_={value->x,value->height<0?value->y+value->height:value->y,
            value->width,std::abs(value->height),value->minDepth,value->maxDepth};}
    }
    const GpuResourceBinding& ini_parameters() const {return parameters_;}
    // Recorded uploads, not completed GPU execution or application binding.
    uint64_t parameter_uploads() const {return parameter_uploads_;}
    gpu::ResourceCommands& commands(){return commands_;}
    struct OutputState {
        D3D11_VIEWPORT viewport{};
        std::optional<VkViewport> native_viewport;
        bool viewport_known=false;
        std::map<std::tuple<wchar_t,ResourceCopyTargetType,unsigned>,GpuResourceBinding> bindings;
    };
    OutputState save_outputs() const;
    void restore_outputs(OutputState&&) noexcept;
private:
    friend class GpuShaderRuntime;
    struct PreparedState;
    std::unique_ptr<PreparedState> prepared_state_;
    bool prepared_plan_=false;
    void remember(CustomResource&);
    void remember(CustomShader&);
    void remember(ResourceCopyOperation&);
    void remember(SlotRangeCopyOperation&);
    void remember(CustomResourcePool*);
    void remember(PoolCopyOperation&);
    void rollback_prepared() noexcept;
    struct ImportedBuffer {
        std::weak_ptr<gpu::BufferResource> buffer;
        D3D11_BUFFER_DESC description{};
    };
    std::map<uint64_t,ImportedBuffer> imported_buffers_;
    std::map<uint64_t,std::pair<uint64_t,std::weak_ptr<gpu::BufferResource>>> imported_handles_;
    struct ImportedImage {
        std::weak_ptr<gpu::ImageResource> image;
        ImageSourceDescription description;
    };
    std::map<uint64_t,ImportedImage> imported_images_;
    std::map<VkImage,std::pair<uint64_t,std::weak_ptr<gpu::ImageResource>>> imported_image_handles_;
    std::map<std::array<uint64_t,9>,std::weak_ptr<gpu::ImageViewResource>> imported_image_views_;
    GpuResourceBinding import_application_image(const ApplicationImageSource&);
    void import_application_view(GpuResourceBinding&,const ApplicationImageBinding&);
    using Slot=std::tuple<wchar_t,ResourceCopyTargetType,unsigned>;
    GpuResourceBinding backbuffer_;
    bool backbuffer_targeted_=false;
    bool run(CommandListCommand&,CommandListState&,std::wstring&);
    bool query(ResourceCopyTarget&,CommandListState&,float&,std::wstring&);
    bool render_target(CommandListState&,bool& bound,float& width,float& height,std::wstring&);
    bool match_source(ResourceCopyTarget&,CommandListState&,SourceResourceMatch&,std::wstring&);
    ResourceCopyTarget& effective(ResourceCopyTarget&,CommandListState&);
    Slot slot(ResourceCopyTarget&,CommandListState&);
    CustomResource* declaration(ResourceCopyTarget&,CommandListState&,bool writing=false);
    GpuResourceBinding get(ResourceCopyTarget&,CommandListState&,ResourceCopyTarget* destination=nullptr);
    GpuResourceBinding load(CustomResource&,ResourceCopyTarget* destination=nullptr,CommandListState* state=nullptr);
    GpuResourceBinding load_image(CustomResource&);
    // A file the original's loader makes no resource of: null from then on, with its warning (D-139).
    GpuResourceBinding unloaded(CustomResource&,const wchar_t* kind,const std::string& why);
    gpu::Image allocate_image(ImageSourceDescription&,gpu::ResourceCommands* = nullptr);
    void select_view(GpuResourceBinding&,GpuResourceBinding::View,bool raw=false,bool complete_info=true);
    void fill_info(GpuResourceBinding&,ResourceCopyTargetType type=ResourceCopyTargetType::CUSTOM_RESOURCE);
    void image_view(GpuResourceBinding&,GpuResourceBinding::View,bool complete_info);
    void image_copy(ResourceCopyOperation&,ResourceCopyTarget&,CommandListState&,GpuResourceBinding,CustomResource*,GpuResourceBinding&);
    void image_clear(GpuResourceBinding,ClearViewCommand&);
    void set(ResourceCopyTarget&,CommandListState&,const GpuResourceBinding&);
    using Bindings=std::map<Slot,GpuResourceBinding>;
    std::set<Slot> ini_slots_;
    void assign_binding(Slot,GpuResourceBinding);
    void publish_bindings(Bindings);
    void copy(ResourceCopyOperation&,CommandListState&);
    void copy_to(ResourceCopyOperation&,ResourceCopyTarget&,CommandListState&,GpuResourceBinding);
    void ranges(SlotRangeCopyOperation&,CommandListState&);
    void clear(ClearViewCommand&,CommandListState&);
    void store(StoreCommand&,CommandListState&);
    void active() const;
    bool flush_parameters(CommandListState&,bool,std::wstring&);
    void publish_parameters();
    gpu::ResourceCommands commands_;
    GpuResourceBinding parameters_;
    DrawExecutor draw_executor_;
    uint64_t parameter_uploads_=0;
    bool parameters_initialized_=false;
    // CPU IniParams assignments survive cancelled GPU work. Republish them at
    // the next original flush boundary even if that list changes no component.
    bool parameter_republish_=false;
    std::map<CustomResource*,GpuResourceBinding> resources_;
    struct CachedSource {SourceResourceIdentity::CachedData data;size_t offset=0,size=0;};
    std::map<CustomResource*,CachedSource> cached_sources_;
    SourceResourceIdentity::CachedData cached_source(const GpuResourceBinding&) const;
    void cache_source(CustomResource*,const GpuResourceBinding&,UINT offset,UINT size);
    float cached_query(ResourceCopyTarget&,CommandListState&,const GpuResourceBinding&,CustomResource*);
    // CustomResource::Substantiate runs once per resource: the declarations already substantiated (D-151).
    std::set<CustomResource*> substantiated_;
    std::map<std::weak_ptr<void>,SourceResourceIdentity,std::owner_less<std::weak_ptr<void>>> source_identities_;
    std::map<Slot,GpuResourceBinding> slots_;
    bool unknown_application_slots_=false;
    std::function<bool(uint64_t,const gpu::Image&,gpu::ApplicationImage&,std::string&)> application_image_observer_;
    // A source slot-range command materializes every view before one API bind.
    Bindings* deferred_bindings_=nullptr;
    // Copies to a stage slot cache per instruction, as in the original engine.
    std::map<ResourceCopyOperation*,GpuResourceBinding> copies_;
    // A current alias is not a writable copy-cache allocation. Original
    // ResourcePool retains owned allocations for every description it creates.
    using CopyPool=std::map<std::array<UINT,4>,gpu::Buffer>;
    std::map<CustomResource*,CopyPool> resource_copy_pools_;
    std::map<ResourceCopyOperation*,CopyPool> command_copy_pools_;
    using ImageKey=std::array<UINT,13>;
    using ImageCopyPool=std::map<ImageKey,gpu::Image>;
    std::map<CustomResource*,ImageCopyPool> resource_image_pools_;
    std::map<ResourceCopyOperation*,ImageCopyPool> command_image_pools_;
    std::map<std::array<UINT,10>,gpu::Image> clear_images_;
    std::map<SlotRangeCopyOperation*,std::vector<std::unique_ptr<ResourceCopyOperation>>> range_operations_;
    std::map<SlotRangeCopyOperation*,std::vector<GpuResourceBinding>> range_views_;
    VkQueue queue_=VK_NULL_HANDLE;
    D3D11_VIEWPORT viewport_{};
    std::optional<VkViewport> native_viewport_;
    bool viewport_known_=false;
    ExecutionSite site_=ExecutionSite::OutsideRenderPass;
    // A store's reads in flight in replaced draws' scopes, with the value's offset in each (D-147).
    // Commands that failed after recording part of their work: not run again in this generation.
    std::unordered_set<const CommandListCommand*> disabled_;
    bool active_=false,installed_=false,failed_=false;
};
// Defined here for source-aware providers; host orchestration only needs the
// forward declaration and never imports the original engine headers.
struct ApplicationResourceState {
    std::vector<GpuResourceRuntime::ApplicationBufferBinding> buffers;
    std::vector<GpuResourceRuntime::ApplicationImageBinding> images;
    // Fresh layout facts for saved named references that are no longer bound.
    std::vector<std::shared_ptr<const GpuResourceRuntime::ApplicationImageSource>> observations;
    // Every omitted source slot is proven unbound. Unknown context refuses.
    bool omitted_slots_unbound=false;
    // Partial source projections preserve unknowns. Any actual read of an
    // omitted slot refuses the transaction instead of manufacturing a null.
    bool allow_unknown_slots=false;
    bool independent_initialization=false;
    bool preserve_unmapped_ini_slots=false;
    std::set<std::tuple<wchar_t,ResourceCopyTargetType,unsigned>> mapped_slots;
    std::function<bool(uint64_t,const gpu::Image&,gpu::ApplicationImage&,std::string&)> observe_image;
};
}
