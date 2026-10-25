#pragma once
#include "draw_plan.h"
#include "runtime.h"
#include <memory>
#include <string>
#include <vector>

namespace efmi::frontend {
// EFMITTL on Vulkan (D-113, docs/efmittl-transparency.md). The library's INI keeps its policy; this
// gives it what its shader hooks get from the game under DX11, from the host's facts, and makes its
// capture a native boundary whose translucent draws the renderer records into its layers. Installed
// only into a generation that loaded the library with the interface named here.
class EfmittlBridge {
public:
    // Null when the generation has no EFMITTL library or not the lists, variables and resource this
    // bridge knows; the library then keeps its own fallback (opaque parts).
    static std::unique_ptr<EfmittlBridge> install(DrawPlanAdapter&, std::vector<std::wstring>& boundaries);
    // The lit vertex shader hook: `$vs_kind = 1` before a draw whose vertex shader is the game's lit
    // character one, 0 after (the shadow hooks are never seen, D-113).
    void before_draw(const RuntimeDraw&) noexcept;
    void after_draw() noexcept;
    // The copy shader hook's pre list: `if $lit_seen` then `$copy_after_lit = 1`.
    void library_hook(LibraryHook) noexcept;
private:
    EfmittlBridge() = default;
    DrawPlanAdapter* adapter_ = nullptr;
    CustomResource* mask_ = nullptr;
    CommandListVariable *vs_kind_ = nullptr, *lit_seen_ = nullptr, *copy_after_lit_ = nullptr, *alpha_ = nullptr,
                        *count_ = nullptr, *first_ = nullptr, *channel_ = nullptr, *invert_ = nullptr;
};
}
