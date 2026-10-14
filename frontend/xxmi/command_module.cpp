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

// Preserve the original buffer-view element calculation, including its use of
// buf_src_size as an end boundary rather than a length after offset.
void FrontendBufferViewElements(UINT stride,UINT offset,DXGI_FORMAT format,UINT end,UINT& first,UINT& count)
{
    D3D11_BUFFER_UAV view{};
    FillOutBufferDescCommon(&view,stride,offset,format,end);
    first=view.FirstElement;count=view.NumElements;
}
