// Offline survey bridge. Link with the unmodified aras-p/smol-v sources.
#include "smolv.h"
extern "C" __declspec(dllexport) size_t smolv_size(const void* data, size_t size) {
    const auto decoded = smolv::GetDecodedBufferSize(data, size);
    return decoded <= 64u*1024u*1024u ? decoded : 0;
}
extern "C" __declspec(dllexport) bool smolv_decode(const void* data, size_t size, void* output, size_t capacity) {
    return capacity <= 64u*1024u*1024u && smolv::Decode(data, size, output, capacity);
}
