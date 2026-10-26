# Generations with their own engine state (D-129, BUG-063's open part)

## The problem

The original engine (the pinned XXMI sources the frontend links) keeps one configuration in process
globals: `G`, `customResources`, `explicitCommandListSections`, `command_list_globals`, the key
actions and about twenty more. The layer runs several configurations at once: one generation per
character package, the live one and its F10 or Mods-folder successor. Until 1.9.7 a generation's
objects lived in its `EngineState` and a `Scope` swapped them into the globals under one
process-wide engine lock. Every entry into the original engine took that lock: a matched draw on the
game's recording threads, a Present on the presenting thread, a settings save, and a whole INI load
on a loader thread.

So a load in the background stopped the game for the whole load: every matched draw and every
presentation of every generation waited for it. Anything slow inside a load stopped the game that
long: BUG-063's exponential walk stopped it forever; a driver call that waits for the GPU inside a
load, while the game's thread needs the lock to submit what the GPU waits for, is a deadlock of
BUG-058's class. Generations also waited for each other while executing (about 20 us per contended
matched draw, 1.8.8's `engine_lock` counters).

## What the original does

3DMigoto has one configuration. Its reload (`ReloadConfig`, IniHandler.cpp) runs from Present on
the presenting thread under `G->mCriticalSection`, which other threads touching the configuration
also take; the game stops for the reload. The layer moved loads off the presenting thread so that
they would not stop the game, but kept the original's single lock, and with it the stop.

The Vulkan form keeps what the original guarantees (a configuration is never seen half built, and
one configuration's state is used by one thread at a time) and drops what only followed from having
one configuration: each generation is a configuration of its own, as if it were its own 3DMigoto,
and the exchange at a Present is the only moment two of them meet.

## The design

1. **The original's configuration globals belong to the generation the thread entered.** A forced
   include for the runtime's translation units (`frontend/xxmi/engine_globals.h`) redirects each name
   to an accessor: `#define customResources (*efvk_engine_customResources())`. The original's
   declaration `extern CustomResources customResources;` and its definition
   `CustomResources customResources;` both become declarations of the accessor function, so the
   pinned sources compile unchanged and define no global; every use goes through the accessor, which
   returns the member of the `EngineState` that the calling thread entered. The state owns these
   objects for its whole life: nothing is swapped.
2. **`G` is a thread-local pointer** behind an inline accessor (`#define G (efvk_engine_globals())`),
   set by the scope; so are the runtime's own scope-local pointers (the input adapter and context, the
   window and key samples, the transition flag). Only trivially destructible values are thread-local:
   the module is exchanged while the game runs, and a thread-local object with a destructor would be
   left behind in the game's threads.
3. **Locks.** A scope takes its generation's mutex only. A load also holds the process-wide load
   mutex, taken before its generation's, which serializes what only the parser uses (the INI section
   tables, the include walk's state, `deferred_bind_flags`, the key parser's dependencies); no
   drawing, presenting or saving thread ever takes it. The original assembler's opcode tables are
   filled through `operator[]` from any assembly, so assembly takes an assembler mutex of its own;
   the diagnostics list takes its own.
4. **Every writable global of the runtime is classified.** `scripts/check_engine_globals.py` lists
   the writable data symbols of the runtime's objects (dumpbin) and fails on one its table does not
   name: per generation (redirected), thread-local, parse-only (load mutex), own lock, set once
   before use, or the original's statistics counters (never read; the original increments them from
   any thread as well). A global added later cannot become shared unnoticed.

## Tests (failing on 1.9.7)

`sandbox/runtime_concurrency_cases.h`, in the original INI runtime step:

- a load held inside its generation's scope: another generation presents and draws meanwhile;
- one generation's draw held: another generation presents and draws meanwhile, and the held
  generation's own Present waits (one thread at a time per generation stays);
- two generations driven from two threads while a third thread loads and retires generations: each
  generation's counters and plans stay its own.

## Costs and risks

- Each access to a redirected container is a call returning a pointer; `G` is a thread-local load.
  Matched draws touch the containers rarely (variables and resources are resolved to pointers at
  parse time); `G` is read often. Measured on the replay's per-draw interpreter time before and
  after.
- Loads still run one after another (the load mutex), as they did under the engine lock.
- The engine lock's status counters (`engine_lock`) now count waits for a generation's own lock.
