// What a command buffer keeps of a replacement backend's work for one recording.
#pragma once
#include <memory>
#include <vector>

namespace efvk {

// The backend that made it knows what is inside. The tracker resets it when the recording is
// legally reset or begun again, and seals it at submission; nothing else.
struct CommandOwner {
    const void* backend = nullptr; // who made it: a backend leaves another's alone
    // Set by the backend when it records into it, cleared when the recording is reset. A command
    // buffer keeps its owner across recordings; an owner left unused by other code gives way.
    bool used = false;
    virtual ~CommandOwner() = default;
    virtual void reset_after_completion() = 0;
    virtual bool seal() = 0; // true when this call sealed it
};

// This backend's owner in a command buffer's owners, made with `make` when the recording has none
// (several character packages replace draws of one recording in the open world). An owner of
// another backend that this recording left unused gives way. Null when `make` has none to give.
template <class Make>
std::shared_ptr<CommandOwner> owner_for(std::vector<std::shared_ptr<CommandOwner>>& owners, const void* backend, Make make)
{
    std::shared_ptr<CommandOwner> mine;
    for (auto it = owners.begin(); it != owners.end();) {
        if (*it && (*it)->backend == backend) { mine = *it; ++it; }
        else if (!*it || !(*it)->used) it = owners.erase(it);
        else ++it;
    }
    if (!mine && (mine = make())) owners.push_back(mine);
    return mine;
}

} // namespace efvk
