// Expose the original parser's CPU-only entry points for the standalone prototype.
#include "IniHandler.cpp"

void HostParseExcerpt(const wchar_t* text,const wchar_t* ns)
{
    std::wistringstream stream(text);
    std::wstring name=ns?ns:L"";
    ParseIniStream(&stream,ns?&name:nullptr);
}
void HostCompileList(const wchar_t* name,CommandList* pre,CommandList* post)
{
    ParseCommandList(name,pre,post,nullptr,true);
}
void HostParseConstants() { ParseConstantsSection(); }
