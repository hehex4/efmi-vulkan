// Compile the pinned original command engine without editing its source. This
// exposes its CPU input-layout evaluator to the backend declaration adapter.
#include "CommandList.cpp"

bool FrontendApplyInputLayoutOverrides(std::vector<D3D11_INPUT_ELEMENT_DESC>& elements,CommandListState& state)
{
    return ApplyInputLayoutOverrides(elements,&state);
}

void FrontendRunCommandList(CommandList& list,CommandListState& state)
{
    _RunCommandList(&list,&state);
}
