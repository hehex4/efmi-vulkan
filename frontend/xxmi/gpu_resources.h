#pragma once
#include "runtime_host.h"
#include "image_rules.h"
#include "../gpu/resource_commands.h"
#include <map>
#include <tuple>

namespace efmi::frontend {
struct GpuResourceBinding {
    enum class View {None,ShaderResource,UnorderedAccess,RenderTarget,DepthStencil};
    gpu::Buffer buffer;
    gpu::Image image;
    gpu::ImageView image_view;
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
// D3D11 hazards operate on source subresources, not Vulkan byte intervals.
bool source_bindings_overlap(const GpuResourceBinding&,const GpuResourceBinding&) noexcept;
// Original INI adapter for owned mutable resources. Stage slots are logical
// source-API bindings; graphics/compute descriptor emission is a separate host
// responsibility. They must not be mistaken for changed application bindings.
// One instance per frontend generation, sharing budget ledgers across reloads.
class GpuResourceRuntime final : public RuntimeExtension {
public:
    enum class ExecutionSite { OutsideRenderPass, InsideRenderPass };
    GpuResourceRuntime(gpu::ResourceDevice,gpu::ResourceBudgets={});
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
    std::shared_ptr<gpu::ResourceSubmission> finish();
    // Discards unsubmitted work. Like the original interpreter, earlier CPU
    // assignments and already-submitted store feedback are not rolled back.
    void discard();
    GpuResourceBinding binding(wchar_t stage,ResourceCopyTargetType,unsigned slot) const;
    // Source-semantic target resolution for an adapted draw in an active scope.
    GpuResourceBinding resolve(ResourceCopyTarget&,CommandListState&);
    const D3D11_VIEWPORT& viewport() const {return viewport_;}
    const GpuResourceBinding& ini_parameters() const {return parameters_;}
    // Recorded uploads, not completed GPU execution or application binding.
    uint64_t parameter_uploads() const {return parameter_uploads_;}
    gpu::ResourceCommands& commands(){return commands_;}
    struct OutputState {
        D3D11_VIEWPORT viewport{};
        std::map<std::tuple<wchar_t,ResourceCopyTargetType,unsigned>,GpuResourceBinding> bindings;
    };
    OutputState save_outputs() const;
    void restore_outputs(OutputState&&) noexcept;
private:
    using Slot=std::tuple<wchar_t,ResourceCopyTargetType,unsigned>;
    bool run(CommandListCommand&,CommandListState&,std::wstring&);
    bool query(ResourceCopyTarget&,CommandListState&,float&,std::wstring&);
    ResourceCopyTarget& effective(ResourceCopyTarget&,CommandListState&);
    Slot slot(ResourceCopyTarget&,CommandListState&);
    CustomResource* declaration(ResourceCopyTarget&,CommandListState&,bool writing=false);
    GpuResourceBinding get(ResourceCopyTarget&,CommandListState&);
    GpuResourceBinding load(CustomResource&);
    GpuResourceBinding load_image(CustomResource&);
    gpu::Image allocate_image(ImageSourceDescription&);
    void select_view(GpuResourceBinding&,GpuResourceBinding::View,bool raw=false,bool complete_info=true);
    void fill_info(GpuResourceBinding&,ResourceCopyTargetType type=ResourceCopyTargetType::CUSTOM_RESOURCE);
    void image_view(GpuResourceBinding&,GpuResourceBinding::View,bool complete_info);
    void image_copy(ResourceCopyOperation&,ResourceCopyTarget&,CommandListState&,GpuResourceBinding,CustomResource*,GpuResourceBinding&);
    void image_clear(GpuResourceBinding,ClearViewCommand&);
    void set(ResourceCopyTarget&,CommandListState&,const GpuResourceBinding&);
    using Bindings=std::map<Slot,GpuResourceBinding>;
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
    std::map<CustomResource*,GpuResourceBinding> resources_;
    std::map<Slot,GpuResourceBinding> slots_;
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
    bool active_=false,installed_=false,failed_=false;
};
}
