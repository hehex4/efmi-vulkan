#pragma once
#include "runtime.h"
#include "../../layer/src/character_package.h"

namespace efmi::frontend {
// The live renderer supplies identity, LOD0/1 bone import and immutable palette
// ownership. This extension implements the same EFMIv1 interface for original
// mod commands and checks its evaluated metadata against that renderer contract.
std::shared_ptr<RuntimeExtension> character_core(const std::string& mod_namespace, const efvk::CharacterPackage&,
                                               bool rabbit_textures = false);
}
