#pragma once
#include "character_module.h"
#include "state.h"
#include "../../frontend/xxmi/owned_runtime.h"
#include <deque>
#include <filesystem>
#include <map>

namespace efvk {
class JsonWriter;

// Where the owned GPU tools (dxcompiler.dll and the others) are: beside the module, else the layer.
std::filesystem::path owned_tools_directory();

// Owned GPU execution of a character generation's original Present work on the game's device
// (D-072): the tools beside the layer, the presented swap chain image as the back buffer, and
// submissions ordered between the application's semaphores and its presentation.
class OwnedPresenter {
public:
    // A GPU runtime for one generation on this device, or null with the reason when owned
    // execution is unavailable (tools not deployed beside the layer, device below Vulkan 1.1).
    static std::shared_ptr<efmi::frontend::OwnedRuntime> runtime(Device*, const VkPhysicalDeviceMemoryProperties&,
                                                                      const std::filesystem::path& working, std::string& reason);
    // The window, cursor and swap chain size of the presented swap chain, for the INI.
    static void sample(Device*, const PresentWork*, efmi::frontend::RuntimeInput&);

    OwnedPresenter(Device*, std::shared_ptr<efmi::frontend::OwnedRuntime>);
    ~OwnedPresenter();
    OwnedPresenter(const OwnedPresenter&) = delete;
    OwnedPresenter& operator=(const OwnedPresenter&) = delete;
    // Opens (or resumes) the scope for this presentation and sets bb. False without one.
    bool begin(PresentWork*, std::string& reason);
    // After the Present lists: submit and hand the presentation this image's semaphore, or
    // discard the scope when they refused.
    void finish(PresentWork*, bool executed, std::string& reason, bool replay = false);
    // Whether the last presentation of a rendered frame drew into or copied into the back buffer:
    // a presentation of the same frame again (frame generation) then replays the Present (BUG-036).
    bool last_drew_backbuffer() const { return drew_backbuffer_; }
    // Owned work recorded since the last submission (replaced draws' work, D-087), on `queue`
    // before an application submission there. False with the reason when it cannot be submitted.
    bool submit_pending(VkQueue queue, std::string& reason);
    // Waits for every submission; the device's images are released after it, the semaphores a few
    // presentations later (a presentation may still wait on one, D-098).
    void stop();
    void status(JsonWriter&) const;

private:
    struct Presented {
        efmi::gpu::Image image;
        VkSemaphore done = VK_NULL_HANDLE;
        uint32_t generation = 0;
    };
    Device* device_;
    std::shared_ptr<efmi::frontend::OwnedRuntime> gpu_;
    PFN_vkCreateSemaphore CreateSemaphore_ = nullptr;
    PFN_vkDestroySemaphore DestroySemaphore_ = nullptr;
    std::map<VkImage, Presented> images_;
    Presented* current_ = nullptr;
    std::deque<std::shared_ptr<efmi::gpu::ResourceSubmission>> tickets_;
    uint64_t presents_ = 0, submitted_ = 0, failed_ = 0, unsupported_ = 0, draw_submitted_ = 0, queue_changes_ = 0, queue_waits_ = 0;
    uint64_t replays_ = 0;
    bool drew_backbuffer_ = false;
    // The presentation moved to another queue: the owned work follows it once every earlier
    // submission completed. False while one is still running; nothing waits for the GPU here.
    bool follow(VkQueue queue);
    std::string last_error_;
    std::filesystem::path readback_;
    bool stopped_ = false;
    void readback(PresentWork*, const Presented&);
};
} // namespace efvk
