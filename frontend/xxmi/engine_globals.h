#pragma once
// D-129 (docs/engine-generations.md): the original engine's configuration globals belong to the
// generation the calling thread entered. Forced into every translation unit of the runtime after the
// port's own header. Each name below becomes an accessor call: the pinned sources' declaration
// `extern CustomResources customResources;` and their definition `CustomResources customResources;`
// both turn into declarations of the accessor (which runtime.cpp defines), so the original compiles
// unchanged and defines no global; every use reaches the entered generation's object.
//
// The headers that the original and the runtime include are taken in first, so that no name below
// is replaced inside them.
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <d3d11_1.h>
#include <DirectXMath.h>

// G: the entered generation's Globals, a thread-local pointer the scope sets (only trivially
// destructible values are thread-local: the module is exchanged while the game's threads live on).
struct Globals;
extern __declspec(thread) Globals* efvk_engine_g;
inline Globals* efvk_engine_globals() { return efvk_engine_g; }
#define G (efvk_engine_globals())

#define customResources (*efvk_engine_customResources())
#define customResourcePools (*efvk_engine_customResourcePools())
#define customShaders (*efvk_engine_customShaders())
#define shader_regex_groups (*efvk_engine_shader_regex_groups())
#define shader_regex_group_index (*efvk_engine_shader_regex_group_index())
#define explicitCommandListSections (*efvk_engine_explicitCommandListSections())
#define command_list_globals (*efvk_engine_command_list_globals())
#define persistent_variables (*efvk_engine_persistent_variables())
#define unknown_variables (*efvk_engine_unknown_variables())
#define registered_command_lists (*efvk_engine_registered_command_lists())
#define command_lists_profiling (*efvk_engine_command_lists_profiling())
#define command_lists_cmd_profiling (*efvk_engine_command_lists_cmd_profiling())
#define dynamically_allocated_command_lists (*efvk_engine_dynamically_allocated_command_lists())
#define pending_post_command_lists (*efvk_engine_pending_post_command_lists())
#define presetOverrides (*efvk_engine_presetOverrides())
#define CurrentTransition (*efvk_engine_CurrentTransition())
#define OverrideSave (*efvk_engine_OverrideSave())
