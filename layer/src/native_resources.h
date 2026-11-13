#pragma once
#include "state.h"
#include "../../frontend/xxmi/native_bindings.h"

namespace efvk {
bool native_resource(Device*, CmdBuf*, const DrawRec&, const efmi::frontend::NativeBinding&,
                     efmi::frontend::NativeResource&, std::string&);
}
