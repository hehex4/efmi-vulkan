#include "character_backend_internal.h"
namespace efvk {
namespace character_internal {
float ini_seconds() { static const uint64_t origin = host_time_us(); return float((host_time_us() - origin) * 1.0e-6); }
}
bool ini_present(ConfigurationProgram& program, OwnedPresenter* presenter, uint64_t frame, float frame_seconds, bool had_draws,
                 PresentWork* present, const efmi::frontend::RuntimeInput& base, uint64_t now, const std::function<void(const std::string&)>& note)
{
    // The keys, window and cursor are sampled once per presentation for every package.
    auto input = base;
    input.frame_seconds = frame_seconds;
    input.ticks_us = now;
#if defined(EFMI_COMMAND_TIMING) || defined(EFMI_PHASE_TIMING)
    auto present_began = std::chrono::steady_clock::now();
    auto present_phase = [&](const char* name) {
        const auto at = std::chrono::steady_clock::now();
        program.note_timing(name, uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(at - present_began).count())); present_began = at;
    };
#else
    auto present_phase = [](const char*) {};
#endif
    std::string reason, owned_reason;
    if (!had_draws) {
        // The first Present that draws into the back buffer kept nothing to replay: its frame's
        // presentations go without (one frame, a panel opening); every later one has it.
        if (!presenter || !presenter->last_drew_backbuffer() || !program.replayable()) return false;
        const bool owned = presenter->begin(present, owned_reason);
        if (!owned_reason.empty()) note(owned_reason);
        if (!owned) return false;
        const bool executed = program.replay(input, frame, ini_seconds(), reason);
        if (!executed) note("INI Present replay: " + reason);
        std::string submission;
        presenter->finish(present, executed, submission, true);
        if (!submission.empty()) note(submission);
        return false;
    }
    // With a presentation the generation's GPU commands run in a scope ordered before it.
    const bool owned = presenter && presenter->begin(present, owned_reason);
    present_phase("present owned begin");
    if (!owned_reason.empty()) note(owned_reason);
    // What a replay needs is kept only where the last Present drew into the back buffer (D-108).
    const bool keep_replay = presenter && presenter->last_drew_backbuffer();
    const bool executed = program.present(input, frame, ini_seconds(), reason, keep_replay);
    present_phase("present ini (whole)");
    if (!executed) note("INI Present: " + reason);
    // The first refusal of owned Present work ends the generation's GPU side; keep its reason.
    static std::atomic<bool> logged{false};
    if (owned && !executed && !logged.exchange(true)) logf("INI Present refused with owned GPU work: %s", reason.c_str());
    if (owned) {
        std::string submission;
        presenter->finish(present, executed, submission);
        if (!submission.empty()) note(submission);
    }
    present_phase("present owned finish");
    return true;
}
}
