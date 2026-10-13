// Whole-character replacement from a character package: every matched section of one mod, with one
// merged skeleton per character instance and optional static materials.
// Inactive unless a character package is explicitly configured.
#pragma once
#include "m2_backend.h"

namespace efvk {
class CharacterBackend;
void character_device_setup(Device*, const VkDeviceCreateInfo*, VkInstance, PFN_vkGetInstanceProcAddr);
void character_submit(CharacterBackend&, VkQueue);
std::shared_ptr<CharacterBackend> character_create(Device*, const VkPhysicalDeviceProperties&,
                                                   const VkPhysicalDeviceMemoryProperties&);
void character_stop(CharacterBackend&);
// True when the original draw is to be dropped: its replacement was recorded, or the mod hides it.
bool character_record(CharacterBackend&, CmdBuf*, const DrawRec&);
void character_retire(CharacterBackend&, uint32_t pipeline_id);
// Once per present. An interval without draws does not count as a frame of the character.
void character_frame(CharacterBackend&, bool had_draws);
void character_sealed(CharacterBackend&);
void character_status(CharacterBackend&, JsonWriter&);
void character_enable(CharacterBackend&, bool);
} // namespace efvk
