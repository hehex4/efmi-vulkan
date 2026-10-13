// What a command buffer keeps of a replacement backend's work for one recording.
#pragma once

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

} // namespace efvk
