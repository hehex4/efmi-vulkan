#pragma once
#include "runtime.h"
#include "draw_plan.h"

namespace efmi::frontend {
// Extensions mediate named native Core boundaries, never reinterpret INI text.
// All methods run with the owning generation's original globals installed.
class RuntimeExtension {
public:
    virtual ~RuntimeExtension() = default;
    virtual std::wstring declarations() { return {}; }
    virtual std::vector<std::wstring> declaration_units() { return {}; }
    virtual std::vector<std::wstring> native_shaders() { return {}; }
    virtual std::vector<std::wstring> install(DrawPlanAdapter&) = 0;
    // Called with zeroed original parameter storage, before either Constants
    // phase. GPU extensions require their host's explicit queue scope here.
    virtual bool initialize_parameters(CommandListState&,std::wstring&) { return true; }
    virtual bool prepare(DrawPlanAdapter&, CommandListState&, std::wstring&) { return true; }
    virtual bool validate_overrides(std::wstring&) { return true; }
    virtual bool before_draw(const RuntimeDraw&, CommandListState&, std::wstring&) { return true; }
    // A CPU command or expression can fail after an extension recorded GPU
    // work. Notify it even when the failure did not occur inside its callback.
    virtual void execution_failed() noexcept {}
};
void RuntimeRunInputList(CommandList*, bool post);
void RuntimePublishTransitionParameters();
}
