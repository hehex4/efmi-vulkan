#pragma once
#include "hlsl_compiler.h"
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace efmi::gpu {
// Translated shader stages kept on disk (D-126), as 3DMigoto keeps a compiled CustomShader beside
// its HLSL: what DXC compiled from a source and what vkd3d-shader translated from a shipped cache,
// under a key of everything that decides the result, so a later load of the same inputs reads the
// program instead of compiling it again. The key covers the compiler libraries and this build (any
// other build or tool version finds nothing), the stage, flags and Vulkan version, and the bytes of
// the source or bytecode; an HLSL entry also lists its includes with their bytes, which must still be
// on disk unchanged. A missing, stale or damaged entry is a miss; writing is best effort and never
// fails a load. The folder is the project's own (beside the settings), never a Mod's. The first
// cache a process opens on a folder removes what no build of these tools can read any more: entries
// of another build or tool version, and temporary files a writer left.
class ShaderCache {
public:
    ShaderCache(std::filesystem::path folder, const std::vector<std::filesystem::path>& tools);
    // Empty when the source cannot be read (the compiler reports that itself).
    std::string hlsl_key(const HlslRequest&) const;
    std::string bytecode_key(const std::vector<uint8_t>& bytecode, char stage, uint32_t mode) const;
    bool load(const std::string& key, HlslProgram&);
    void store(const std::string& key, const HlslProgram&);
    struct Stats { std::atomic<uint64_t> hits{0}, misses{0}, stale{0}, stored{0}, removed{0}; };
    const Stats& stats() const { return stats_; }
private:
    void prune();
    std::filesystem::path folder_;
    std::string identity_;
    Stats stats_;
};
// Every field of a program as bytes and back; false for anything malformed or truncated.
std::vector<uint8_t> serialize_program(const HlslProgram&);
bool deserialize_program(const uint8_t*, size_t, HlslProgram&);
}
