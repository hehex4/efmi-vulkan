#pragma once
#include "../../frontend/xxmi/runtime.h"
#include "../../frontend/xxmi/owned_runtime.h"

namespace efvk {
// One original configuration registry. No file, namespace or skeleton owns a
// loading unit; original command execution instantiates resources on first use.
class ConfigurationProgram {
public:
    bool load(efmi::frontend::RuntimeConfig, std::shared_ptr<efmi::frontend::OwnedRuntime>, std::string&);
    const auto& runtime() const { return runtime_; }
    const auto& gpu() const { return gpu_; }
    bool present(const efmi::frontend::RuntimeInput& input, uint64_t frame, float seconds, std::string& why, bool keep = true) {
        return runtime_->present(input, frame, seconds, why, keep);
    }
    bool replay(const efmi::frontend::RuntimeInput& input, uint64_t frame, float seconds, std::string& why) {
        return runtime_->replay(input, frame, seconds, why);
    }
    bool replayable() const { return runtime_->replayable(); }
    bool save(std::string& why) { return runtime_->save(why); }
    bool settings_dirty() const { return runtime_->settings_dirty(); }
    bool take_reload_request() { return runtime_->take_reload_request(); }
    void suspend_persistence() { runtime_->suspend_persistence(); }
    void inherit_persistent(const ConfigurationProgram& previous) { runtime_->inherit_persistent(*previous.runtime_); }
    auto status() const { return runtime_->status(); }
    auto discard_gates() const { return runtime_->discard_gates(); }
    auto runtime_gap_count() const { return runtime_->gap_count(); }
    auto runtime_gaps_since(uint32_t seen) const { return runtime_->gaps_since(seen); }
    void note_timing(const char* name, uint64_t us) { runtime_->note_timing(name, us); }
private:
    std::shared_ptr<efmi::frontend::OwnedRuntime> gpu_;
    std::shared_ptr<efmi::frontend::IniRuntime> runtime_;
};
}
