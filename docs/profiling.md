# Profiling the layer in a running process (1.8.6)

The tracker's own timings (`tracker_work`, `record`, `stage_us` in the probe status) say how long a
category took, not where inside it the time went. In the 1.8.4 session the layer's work around each
of the game's draws came to about 7 us on average, matched or not, and nothing in the counters named
the cause. The probe's sampler answers that in the game itself.

scripts/sample_profile.py profiles the project's own processes (the replay, the sandbox) from outside.
The game runs elevated, so an outside profiler without administrator rights cannot open its threads;
the sampler below runs inside the layer, which the game has loaded already.

## The sampler (layer/src/sampler.cpp)

- Probe command `profile` with `milliseconds` (at most 30000) and `hz` (at most 1000). The pipe thread
  samples, for that long, every thread that has entered the layer (the stall watchdog's slots: the
  game's recording, submitting and presenting threads), not the game's other threads.
- A sample suspends one thread, reads its registers, copies the top 128 KiB of its stack and resumes
  it. Nothing is allocated and no lock is taken while the thread is held, so a thread suspended inside
  the heap or the loader cannot stop the sampler; the unwinding (RtlLookupFunctionEntry,
  RtlVirtualUnwind) runs afterwards on the copy, with the stack registers moved into it.
- The reply holds the loaded modules (path, base, size) and the distinct stacks with their counts and
  thread, each frame as `module index + offset`, the most frequent 20000 stacks.
- Cost: a few microseconds per sample per thread; at 250 Hz and six threads well under one percent of
  a core, and nothing outside the sampled window.

## The report (scripts/profile_game.py)

    python scripts/profile_game.py PID --seconds 10 --hz 250

saves the raw reply under build/ and names the frames through dbghelp: the layer and the module from
the build's PDBs (layer/build, layer/build/module, dist), other modules by their exports (a name more
than 4 KiB from its export is marked `+0x...?`). A stack whose top frame is a kernel wait counts as
waiting. It prints per thread and overall the functions most often on top (exclusive) and the layer's
functions anywhere in the stacks (inclusive), and how much of the busy time had a layer frame.
`--input FILE` reads a saved reply again.

## Checked

- The probe end-to-end test asks for a 300 ms profile in every configuration and checks samples,
  frames and the layer among the modules (sandbox/probe_test.py).
- A 3 s profile of the multi-threaded sandbox: 1197 samples, kernel waits recognized, the layer's own
  functions named from its PDB.
