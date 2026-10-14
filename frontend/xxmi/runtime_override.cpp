// Override.cpp owns the original transition and duplicate-listener semantics.
// Its command-list entry must use the CPU adapter, not a reference D3D context.
#include "Override.h"
#include "runtime_host.h"
#include <stdexcept>
static std::vector<ExplicitCommandListSection*> input_dependencies;
static bool transition_upload_pending=false;
static void RuntimeFinishParameterTransition()
{
    if(!transition_upload_pending)return;
    transition_upload_pending=false;
    efmi::frontend::RuntimePublishTransitionParameters();
}
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
    // Within UpdateTransitions the only command calls are deferred post lists,
    // after the original parameter and variable loops. Publish before them.
    RuntimeFinishParameterTransition();
    efmi::frontend::RuntimeRunInputList(list, post);
}
#define RunCommandList RuntimeOverrideRun
#define ParseRunExplicitCommandList RuntimeParseInputRun
#include "Override.cpp"
#undef ParseRunExplicitCommandList
#undef RunCommandList

void RuntimeUpdateTransitions(HackerDevice* device)
{
    if(transition_upload_pending)throw std::runtime_error("Nested original parameter transition");
    // Original UpdateIniParams runs whenever the parameter map was nonempty,
    // even if float comparison would say unchanged. Keep the unchanged source
    // transition loops; no command can observe their intermediate CPU writes.
    transition_upload_pending=!CurrentTransition.params.empty();
    try{CurrentTransition.UpdateTransitions(device);RuntimeFinishParameterTransition();}
    catch(...){transition_upload_pending=false;throw;}
}
