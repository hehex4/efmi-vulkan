// Override.cpp owns the original transition and duplicate-listener semantics.
// Its command-list entry must use the CPU adapter, not a reference D3D context.
#include "Override.h"
#include "runtime_host.h"
static void RuntimeOverrideRun(HackerDevice*, HackerContext*, CommandList* list, DrawCallInfo*, bool post)
{
    efmi::frontend::RuntimeRunInputList(list, post);
}
#define RunCommandList RuntimeOverrideRun
#include "Override.cpp"
#undef RunCommandList
