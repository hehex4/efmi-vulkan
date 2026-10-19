#pragma once
#include "runtime.h"
#include "../../layer/src/character_package.h"

namespace efmi::frontend {
// The live renderer supplies identity, LOD0/1 bone import and immutable palette
// ownership. This extension implements the same EFMIv1 interface for original
// mod commands and checks its evaluated metadata against that renderer contract.
std::shared_ptr<RuntimeExtension> character_core(const std::string& mod_namespace, const efvk::CharacterPackage&,
                                               bool rabbit_textures = false);

// Directory generation (D-078): no offline package. The same extension takes its contract from the
// generation it is loaded into: the mod's mesh sections from the original override registry, with
// the component and LOD each assigns, and bones, components and LOD remaps from the original Core
// initializer. Filled while IniRuntime::load runs; complete once it succeeded.
struct CharacterDiscovery {
    efvk::CharacterPackage contract;                // bones, components (LOD1 remaps read), sections; no streams
    std::vector<std::filesystem::path> remap_files; // per component of the contract: its LOD1 remap file, empty for identity
    size_t left_to_game = 0;                        // mesh sections of the mod the contract has no part for
};
std::shared_ptr<RuntimeExtension> character_discovery(const std::string& mod_namespace, std::shared_ptr<CharacterDiscovery> output);
}
