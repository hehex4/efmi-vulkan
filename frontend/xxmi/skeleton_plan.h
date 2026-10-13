#pragma once
#include "resource_plan.h"
#include <array>

namespace efmi::frontend {
enum class RemapSource { Unassigned, Identity, File };
struct SkeletonRemapFile {
    RemapSource source=RemapSource::Unassigned;
    std::filesystem::path path;
};
struct SkeletonFileComponent {
    uint32_t first=0,count=0; // Component id is the vector index; zero-count slots are retained.
    std::array<SkeletonRemapFile,4> lods;
};
struct SkeletonFilePlan {
    static constexpr uint32_t supported_lods=2;
    uint32_t bones=0,instances=0,lod_count=0;
    std::vector<SkeletonFileComponent> components;
};
// Read evaluated original pools, not INI text. No file or GPU work. Output owns
// its paths/metadata and changes only after the complete partition is valid.
bool read_skeleton_plan(uint32_t bones,uint32_t instances,uint32_t lods,
    CustomResourcePool& offsets,CustomResourcePool& counts,CustomResourcePool& remaps,
    const DrawPlanAdapter&,const FilePlanCompiler&,SkeletonFilePlan&,std::wstring& reason);
}
