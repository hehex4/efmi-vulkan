# First real probe observation

Offline preparation is complete for the current M1 gate. The next observation requires an actual
game frame; no sandbox run can identify the game's current descriptors, shaders or index regions.
Historical run 3 remains accepted as initial G1 loading evidence. No repeated 30-minute run is needed.

## Preconditions

The user chooses when to start the game and which account to use. Do not launch or inject it
automatically. Use the recorded run-3 launcher configuration: Vulkan (no force-d3d11 argument),
XXMI injection Skip, only this project's `dist/efmi_vk_inject.dll`. Launcher settings are read-only
for the agent. The prepared layer is 0.5.0 with `mode = probe`; a running process keeps its loaded DLL.

Show the character corresponding to `arcane fes 1.2`. Vulkan does not yet render the mod; the
purpose is to observe the original character's data. First capture a stable view, then optionally
a second distinct scene/view once the first result is saved. Duration alone cannot replace a frame.

## Collect and preserve

Use the PID reported by `efvk_status` for the already running Endfield probe. The collector requires
both a PID and a target label, checks the executable name, and refuses to overwrite an output directory:

```powershell
& 'D:/Python312/python.exe' -B scripts/collect_probe.py --pid <PID> --target game --out captures/first-probe --mod 'D:/XXMI-Launcher-Portable-v2.1.6/EFMI/Mods/arcane fes 1.2/arcane.ini'
```

Replace `<PID>` with the actual number. Using the main INI makes the 21 character hash rules
unambiguous; choosing the whole folder also includes the panel's additional rule for a repeated hash.
The script only connects to the existing probe. It does not launch, inject, alter graphics calls,
edit configurations or request a GPU read-back.

The bundle contains raw frame descriptors/draws, status and API counters before/after capture,
INI fingerprints and complete match results, a G2 summary, pipeline metadata and retained SPIR-V.
It prioritizes pipelines belonging to matching draws, then other frame pipelines, with a default
limit of 16 (`--pipeline-limit`, 0..128). Omitted pipelines and objects destroyed before queries
are explicit. The byte hashes in `manifest.json` identify each saved file. That manifest is written
last; a directory without it is an interrupted collection, not complete evidence. Exports stay
inside this project and are ignored by Git.

`--target sandbox` requires efmi_vk_sandbox.exe; `--target game` requires Endfield.exe. The labels
cannot be interchanged to pass a gate. `complete` in the manifest describes the collection only,
not G1/G2/G3, stability, visible-character verification or rendering compatibility.

## Questions this observation can answer

| Gate/question | Read |
| --- | --- |
| Actual API paths | api-before.json / api-after.json: called and resolved functions, enabled extensions |
| G2 recording/submission structure | summary.json and frame.json: recording threads, primary/secondary buffers, submits, render passes, draw kinds |
| G3 a matching index regions | matches.json and frame.json: exact hash/first/count matches and mapped/shadow/unknown sources |
| Candidate resource/shader mapping | frame.json, pipelines.json, shaders.json and SPIR-V; compare with a later modded DX11 baseline |

## What still needs separate evidence

Ordinary descriptors and all resource visibility are submit-time observations. Push descriptor
values are retained at record time, but that does not establish that their buffer bytes were
readable then. This bundle deliberately avoids treating a later live buffer read as earlier draw
data. G3 b/c require targeted record-time availability measurements after identifying the relevant
actual shader/binding paths. G3 d and full DX11/Vulkan mapping still need the modded DX11 baseline.
Requests are sequential, not an atomic snapshot across several Vulkan devices.

Choose any further descriptor hooks, CPU bone import or GPU read-back only from that evidence.
The frontend host and image-panel work follow the G2/G3-backed minimal replacement backend.

## Sandbox verification

`sandbox/evidence_test.py` passes 52 checks through both entries with push templates and secondary
recording. It verifies raw draw/descriptor timing retention, hashes of every exported file, two
retained shaders, INI matching, wrong target labels, overwrite refusal and preservation of the raw
frame when a pipeline query fails. This is exporter verification, not a game capture.
