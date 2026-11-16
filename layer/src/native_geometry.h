#pragma once
#include "state.h"
#include "../../frontend/assets/file_plan.h"

namespace efvk {
struct NativeGeometryPlan {
    std::vector<std::shared_ptr<const NativeGeometryState>> bindings;
};
bool prepare_native_geometry(Device*, CmdBuf*, const DrawRec&, const efmi::assets::FilePlan&,
                             NativeGeometryPlan&, std::string&);
void record_native_geometry(Device*, CmdBuf*, const efmi::assets::FilePlan&, const NativeGeometryPlan&, size_t);
}
