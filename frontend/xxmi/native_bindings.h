#pragma once
#include <cstdint>
#include <memory>

namespace efmi::frontend {
// Explicit aliases authored by a native Mod; never inferred from a DX11 register or a character.
struct NativeBinding {
    enum Kind : uint8_t { Resource, Constant, Access };
    uint32_t hash = 0;
    uint8_t stage = 0; // v, h, d, g, p, c
    Kind kind = Resource;
    uint32_t slot = 0, set = 0, binding = 0, element = 0;
};
// Metadata and optional owned same-handle reference to an application's tracked descriptor.
// The hash, when known, names tracked upload content, never an uncompleted GPU result.
struct NativeResource {
    bool bound = false, hash_known = false, format_known = false;
    uint32_t hash = 0, dimension = 0, format = 0, resource_format = 0, bind_flags = 0;
    uint32_t width = 0, height = 0, depth = 0, levels = 0, layers = 0, samples = 1;
    uint32_t stride = 0;
    uint64_t identity = 0, size = 0, offset = 0, range = 0;
    uint64_t buffer = 0, image_view = 0, buffer_view = 0;
    uint64_t image = 0;
    uint32_t image_view_id = 0, view_type = 0, view_format = 0;
    uint32_t aspect = 0, base_mip = 0, mip_count = 0, base_layer = 0, layer_count = 0;
    bool image_safe = false;
    uint32_t descriptor_type = 0, usage = 0, image_layout = 0;
    bool uploaded_readonly = false; // Published immutable file upload; every consumer must be read-only.
    bool uploaded_mutable = false; // Private pinned file buffer; compute and explicit hazards only.
    uint32_t buffer_stride = 0; // File-declared structure stride, independent of the bound view.
    uint64_t publication_semaphore = 0, publication_value = 0; // Completed upload, retained by keep.
    std::shared_ptr<const void> keep; // Complete view/resource/allocation chain, or absent when unretainable.
};
}
