#pragma once
// What the runtime's host shims keep per generation (D-129, docs/engine-generations.md): the
// resource root its files resolve against and the original warnings its load and execution reported.
#include <cstddef>
#include <deque>
#include <string>
#include <utility>
struct EngineHostState {
    std::wstring resource_root;
    size_t diagnostic_count = 0;
    std::deque<std::pair<size_t, std::string>> diagnostics;
};
// The generation the calling thread entered, or null outside every generation.
EngineHostState* EngineHost();
