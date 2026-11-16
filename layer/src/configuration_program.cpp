#include "configuration_program.h"

namespace efvk {
bool ConfigurationProgram::load(efmi::frontend::RuntimeConfig options,
    std::shared_ptr<efmi::frontend::OwnedRuntime> gpu, std::string& why)
{
    gpu_ = std::move(gpu);
    options.texture_identity = true;
    auto extension = gpu_ ? gpu_->combine(nullptr) : nullptr;
    if (gpu_) gpu_->begin_deferred();
    runtime_ = efmi::frontend::IniRuntime::load(options, std::move(extension), why);
    if (gpu_ && gpu_->scope_active()) {
        if (runtime_) gpu_->suspend(); else gpu_->discard();
    }
    return bool(runtime_);
}
}
