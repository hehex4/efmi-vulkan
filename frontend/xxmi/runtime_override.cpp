// Override.cpp owns the original transition and duplicate-listener semantics.
// Its command-list entry must use the CPU adapter, not a reference D3D context.
#include "Override.h"
#include "runtime_host.h"
static std::vector<ExplicitCommandListSection*> input_dependencies;
void RuntimeResetInputDependencies() { input_dependencies.clear(); }
const std::vector<ExplicitCommandListSection*>& RuntimeInputDependencies() { return input_dependencies; }
static bool RuntimeParseInputRun(const wchar_t* section, const wchar_t* key, std::wstring* value,
    CommandList* explicit_list, CommandList* pre, CommandList* post, const std::wstring* ns)
{
    const bool ok = ParseRunExplicitCommandList(section, key, value, explicit_list, pre, post, ns);
    if (ok) {
        // Key cycles copy temporary activation lists into their private state.
        // Retain the stable named callee, never a pointer into those temporaries.
        for (auto* list : {explicit_list, pre, post}) if (list && !list->commands.empty()) {
            const auto* call = dynamic_cast<RunExplicitCommandList*>(list->commands.back().get());
            auto* target = call ? call->command_list_section : nullptr;
            if (target && std::find(input_dependencies.begin(), input_dependencies.end(), target) == input_dependencies.end())
                input_dependencies.push_back(target);
        }
    }
    return ok;
}
static void RuntimeOverrideRun(HackerDevice*, HackerContext*, CommandList* list, DrawCallInfo*, bool post)
{
    efmi::frontend::RuntimeRunInputList(list, post);
}
#define RunCommandList RuntimeOverrideRun
#define ParseRunExplicitCommandList RuntimeParseInputRun
#include "Override.cpp"
#undef ParseRunExplicitCommandList
#undef RunCommandList
