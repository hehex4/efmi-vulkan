// Preserve original key parsing, modifiers, cycle/hold/toggle and edge dispatch.
// Replace only the OS polling source with the host's one coherent input sample.
#include "Input.h"
#include "runtime.h"
// The dispatching thread's sample; each thread dispatches the generation it entered (D-129).
static __declspec(thread) const efmi::frontend::RuntimeInput* runtime_input = nullptr;
static SHORT WINAPI RuntimeGetAsyncKeyState(int key)
{
    return runtime_input && key >= 0 && key < 256 && runtime_input->keys[key] ? SHORT(0x8000) : 0;
}
// The original's key actions are the entered generation's (engine_globals.h for the shared names;
// this one is local to input.cpp, whose `static` declaration becomes the accessor's).
std::vector<InputAction*>* RuntimeInputActions();
#define GetAsyncKeyState RuntimeGetAsyncKeyState
#define actions (*efvk_engine_actions())
#include "input.cpp"
#undef GetAsyncKeyState
static std::vector<InputAction*>* efvk_engine_actions() { return RuntimeInputActions(); }

size_t RuntimeInputCount() { return actions.size(); }
bool RuntimeDispatchInput(HackerDevice* device, const efmi::frontend::RuntimeInput& input)
{
    efmi::frontend::RuntimeInput released;
    runtime_input = input.foreground ? &input : &released;
    bool changed = false;
    try { for (auto* action : actions) changed |= action->Dispatch(device); }
    catch (...) { runtime_input = nullptr; throw; }
    runtime_input = nullptr;
    return changed;
}
