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
    virtual bool prepare(DrawPlanAdapter&, CommandListState&, std::wstring&) { return true; }
    virtual bool validate_overrides(std::wstring&) { return true; }
    virtual bool before_draw(const RuntimeDraw&, CommandListState&, std::wstring&) { return true; }
};
void RuntimeRunInputList(CommandList*, bool post);
}
