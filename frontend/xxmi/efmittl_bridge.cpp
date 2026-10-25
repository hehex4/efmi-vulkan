#include "efmittl_bridge.h"
#include "runtime_host.h"
#include "globals.h"
#include <cmath>

namespace efmi::frontend {
namespace {
const std::wstring library = L"\\efmittl\\";
CommandListVariable* variable(const wchar_t* name)
{
    const auto found = command_list_globals.find(L"$" + library + name);
    return found == command_list_globals.end() ? nullptr : &found->second;
}
// A count or first index the library's caller set: a whole number a draw can take.
bool index_value(float value, uint32_t& out)
{
    if (!std::isfinite(value) || value < 0 || value >= 4294967296.0f || std::floor(value) != value) return false;
    out = uint32_t(value); return true;
}
}
std::unique_ptr<EfmittlBridge> EfmittlBridge::install(DrawPlanAdapter& a, std::vector<std::wstring>& boundaries)
{
    const std::wstring capture = L"commandlist" + library + L"capture";
    const auto list = explicitCommandListSections.find(capture);
    const auto mask = customResources.find(L"resource" + library + L"mask");
    if (list == explicitCommandListSections.end() || !explicitCommandListSections.count(L"commandlist" + library + L"draw") ||
        !explicitCommandListSections.count(L"commandlist" + library + L"composite") || mask == customResources.end())
        return nullptr;
    std::unique_ptr<EfmittlBridge> bridge(new EfmittlBridge);
    auto& b = *bridge;
    const std::pair<CommandListVariable**, const wchar_t*> names[] = {
        {&b.vs_kind_, L"vs_kind"}, {&b.lit_seen_, L"lit_seen"}, {&b.copy_after_lit_, L"copy_after_lit"}, {&b.alpha_, L"alpha"},
        {&b.count_, L"count"}, {&b.first_, L"first"}, {&b.channel_, L"mask_channel"}, {&b.invert_, L"mask_invert"}};
    for (const auto& name : names)
        if (!(*name.first = variable(name.second))) return nullptr;
    b.adapter_ = &a; b.mask_ = &mask->second;
    boundaries.push_back(capture);
    // The library's CommandListCapture, reached only in the forward lit pass outside its fallback,
    // after CommandListDraw resolved the opacity: the call's draw goes to the renderer's layers.
    a.native(list->second, capture, [&b](CommandListState& state, unsigned phases, std::wstring& why) {
        if (!(phases & 1)) return true;
        uint32_t count = 0, first = 0;
        if (!index_value(b.count_->fval, count) || !index_value(b.first_->fval, first)) { why = L"EFMITTL capture: invalid count or first index"; return false; }
        const float channel = std::isfinite(b.channel_->fval) ? std::fmin(std::fmax(b.channel_->fval, 0.0f), 3.0f) : 0.0f;
        return b.adapter_->translucent(state, count, first, b.alpha_->fval, b.mask_, uint32_t(channel), b.invert_->fval > 0.5f, why);
    });
    return bridge;
}
void EfmittlBridge::before_draw(const RuntimeDraw& draw) noexcept { vs_kind_->fval = draw.lit_vertex ? 1.0f : 0.0f; }
void EfmittlBridge::after_draw() noexcept { vs_kind_->fval = 0.0f; }
void EfmittlBridge::library_hook(LibraryHook hook) noexcept
{
    if (hook == LibraryHook::CopyShader && lit_seen_->fval != 0.0f) copy_after_lit_->fval = 1.0f;
}
}
