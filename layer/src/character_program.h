#pragma once
#include "character_package.h"
#include "../../frontend/xxmi/runtime.h"
#include <map>

namespace efvk {
// A parsed original INI generation and its file-to-renderer resource catalogue.
// A missing sidecar keeps the existing static/sandbox package behavior.
class CharacterProgram {
public:
    static bool configured(const std::string& package_path);
    bool init(const std::string& package_path, CharacterPackage&, std::string& reason);
    bool plan(const efmi::frontend::RuntimeDraw&, CharacterPackage&, CharacterList&, bool& skip, bool& added, std::string& reason);
    bool present(const efmi::frontend::RuntimeInput&, uint64_t frame, float seconds, std::string& reason);
    bool save(std::string& reason) { return !runtime_ || runtime_->save(reason); }
    bool take_reload_request() { return runtime_ && runtime_->take_reload_request(); }
    void suspend_persistence() { if (runtime_) runtime_->suspend_persistence(); }
    void inherit_persistent(const CharacterProgram& previous) { runtime_->inherit_persistent(*previous.runtime_); }
    efmi::frontend::RuntimeStatus status() const { return runtime_ ? runtime_->status() : efmi::frontend::RuntimeStatus{}; }
private:
    std::shared_ptr<efmi::frontend::IniRuntime> runtime_;
    std::map<std::wstring, uint32_t> streams_, images_;
    std::map<std::string, CharacterList> lists_;
    std::vector<std::pair<uint32_t, uint32_t>> index_bounds_;
};
}
