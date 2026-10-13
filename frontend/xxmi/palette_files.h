#pragma once
#include "skeleton_plan.h"
#include "../core/palette_bank.h"
namespace efmi::frontend {
// Generation preparation only, outside draw execution. Reads stable R16 file
// contents with a read-only shared file handle. Never opens later-LOD resources.
bool load_palette_files(const SkeletonFilePlan&,core::PaletteDefinition&,std::string& reason);
}
