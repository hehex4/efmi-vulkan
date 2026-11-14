#pragma once
#include "state.h"
#include "../../frontend/xxmi/native_bindings.h"

namespace efvk {
// Presence only: no layout proof or ownership is acquired from these immutable snapshots.
bool native_dynamic_outputs(const CmdBuf*, uint32_t context, uint32_t& mask);
bool native_resource(Device*, CmdBuf*, const DrawRec&, const efmi::frontend::NativeBinding&,
                     efmi::frontend::NativeResource&, std::string&);
}
