#pragma once
#include "draw_plan.h"
namespace efmi::frontend {
// Borrowed original buffer windows, valid only during the serial CPU transaction.
// The caller supplies bytes from its own tracked/mapped resource adapter.
class BufferQueries {
public:
    bool bind(CustomResource*,const uint8_t* data,size_t size,std::wstring& reason);
    void clear() {windows_.clear();}
    bool read(ResourceCopyTarget&,CommandListState&,float&,std::wstring& reason) const;
private:
    struct Window {const uint8_t* data;size_t size;};
    std::unordered_map<CustomResource*,Window> windows_;
};
}
