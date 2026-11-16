#pragma once
#include "../../frontend/xxmi/native_bindings.h"
#include <map>

namespace efvk {
// Immutable recording state, independent of the module that produced it.
// Application IA binds replace only the named slots. Recording owners retain
// every prior resource until the command buffer can legally be reset.
struct NativeGeometryState {
    // NativeBinding now includes retained Vertex/Index source addresses.
    // This shared stamp changes when their payload interpretation changes.
    static constexpr uint32_t protocol = 2;
    std::map<uint32_t, std::shared_ptr<const efmi::frontend::NativeResource>> vertices;
    bool index_overridden = false;
    std::shared_ptr<const efmi::frontend::NativeResource> index;
    bool empty() const { return vertices.empty() && !index_overridden; }
};
}
