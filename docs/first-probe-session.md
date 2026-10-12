# First real probe observation

Offline preparation is complete for the current M1 gate. The next observation requires an actual
game frame; no sandbox run can identify the game's current descriptors, shaders or index regions.
Historical run 3 remains accepted as initial G1 loading evidence. No repeated 30-minute run is needed.

## Preconditions

The user chooses when to start the game and which account to use. Do not launch or inject it
automatically. Use the recorded run-3 launcher configuration: Vulkan (no force-d3d11 argument),
XXMI injection Skip, only this project's `dist/efmi_vk_inject.dll`. Launcher settings are read-only
for the agent. The prepared layer is 0.5.1 with `mode = probe`; a running process keeps its loaded DLL.

Show the character corresponding to `arcane fes 1.2`. Vulkan does not yet render the mod; the
purpose is to observe the original character's data. First capture a stable view, then optionally
a second distinct scene/view once the first result is saved. Duration alone cannot replace a frame.

## Look at the status first

Probe mode has never been in the game. Before collecting, read `efvk_status` and
`efvk_api_usage` once and check:

- `counts.shaders_not_retained`: above 0 means the 512 MiB SPIR-V budget was reached; the
  shaders created after that have hash and size but no code.
- `counts.descriptor_sets`, `counts.retained_shader_bytes`, `counts.shadow_bytes`: what the
  probe holds in the game's memory.
- `last_frame.unknown_writes`: index data the probe could not see on the CPU.
- In `efvk_api_usage`, which descriptor update path is used and how often per frame.

If the game is noticeably slower or uses much more memory than without the probe, quit the
game normally and change `dist/efmi_vk.cfg` for the next launch, in this order:

1. `resources = 0`: draws, buffers, hashes and call counters only. Gate items G2 and G3 a can
   still be answered; descriptor contents and shaders cannot.
2. `mode = passthrough`: no tracking and no pipe.

A captured frame holds at most 64 MiB of descriptor contents; `descriptors_omitted` in
`frame.json` and `summary.json` says how many sets were left out.

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

## Second session: contents at submit (layer 0.6.0)

The first session took place on 2026-10-12 with 0.5.1 (`captures/first-probe`). What it left open for
G3 b and c is the content of the instance entry and the bone rows as the draws used them, and whether
those bytes are in place when a draw is recorded. With 0.6.0 the collector can keep them
(`docs/probe-snapshots.md`); same preconditions, same scene, a new output directory:

```powershell
& 'D:/Python312/python.exe' -B scripts/collect_probe.py --pid <PID> --target game --out captures/second-probe --mod 'D:/XXMI-Launcher-Portable-v2.1.6/EFMI/Mods/arcane fes 1.2/arcane.ini' --pipeline-limit 64 --snapshot mod --follow instance
```

`--snapshot mod` selects the draws whose region hash is in the mod; `--follow instance` reads the two
bone offsets in constant 5 of the entry at set 2 binding 0 and keeps 12,288 bytes of the bone buffer at
each, plus the 256 bytes of the entry on their own. Expect about 45 selected draws and a few megabytes
kept (the layer's budget is 32 MiB). Read in `frame.json`: `snapshot.record` (how many ranges were
`equal`, `different`, `not_recorded`), and per draw the `entry` item and the `follow` items.

## Third session: texture contents and pipeline creation (layer 0.7.0)

The second session took place on 2026-10-12 with 0.6.0 (`captures/second-probe`,
`captures/second-probe-all`; results in `docs/probe-snapshots.md`). What is still open for the slot
table are four entries where two textures look alike. 0.7.0 hashes mip 0 of every texture when it is
uploaded, which happens while the scene loads, so the layer has to be in the process from the start
as usual. Same preconditions and scene. Before collecting, read the status once:

- `counts.image_content`: uploads per outcome and `microseconds`. If loading was noticeably slower
  than before, that number says how much of it was the hashing; the fallback is `image_hash = 0` in
  `dist/efmi_vk.cfg`.

Then one bundle with every pipeline:

```powershell
& 'D:/Python312/python.exe' -B scripts/collect_probe.py --pid <PID> --target game --out captures/third-probe --mod 'D:/XXMI-Launcher-Portable-v2.1.6/EFMI/Mods/arcane fes 1.2/arcane.ini' --pipeline-limit 128
```

and the pairing with the DX11 frame and its texture hashes:

```powershell
& 'D:/Python312/python.exe' -B scripts/dx11_baseline.py summarize --label frame-off --mod 'D:/XXMI-Launcher-Portable-v2.1.6/EFMI/Mods/arcane fes 1.2/arcane.ini' --vulkan captures/third-probe --textures captures/dx11-baseline/frame-off-tex.textures.json
```

Read in its output: how many texture pairs were found by content, and whether the four open slots
are placed. `api-after.json` of the bundle has `pipeline_creation` for the whole session.

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
