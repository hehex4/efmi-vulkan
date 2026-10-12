#pragma once
#include "draw_plan.h"
#include "../assets/file_plan.h"

namespace efmi::frontend {
// Snapshot declarations once per retained frontend generation. Plans no longer
// borrow CustomResource pointers after compilation. Unsupported declarations
// remain dormant until a callback actually binds them; no files are opened.
class FilePlanCompiler {
public:
    explicit FilePlanCompiler(const CustomResources&);
    bool compile(const DrawPlan&,assets::FilePlan&,std::wstring& reason) const;
private:
    struct Declaration {
        std::array<std::shared_ptr<const assets::FileResource>,3> bindings;
        std::array<std::wstring,3> reasons;
    };
    std::unordered_map<const CustomResource*,Declaration> declarations_;
};
}
