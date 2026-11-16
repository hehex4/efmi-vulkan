# Architecture audit and remediation

Review input: 2026-11-15; updated 2026-11-16. The review input is `issues.md` against source 3.0.4. Changes below
are local commits on main and engine-work. Current source is
3.2.0, ABI `347c72b0cc7d0e40`. The latest separately checked no-probe candidate
is 3.1.8 with the exact semantic cache (D-296); earlier candidates below are historical. No real-game verification is included in this audit.

| Finding | Implemented or verified | Remaining boundary |
|---|---|---|
| I-01, multiple queues | D-286 proves pending overlap and a global-chain deadlock. D-288/D-289/D-291 correct semaphore object lifetime, failed-submission rollback and acquisition signal scope. | No production scheduler is installed; payload/value and resource access ordering remain open. Existing guards remain. See `queue-ordering-proof.md`. |
| I-02, priorities and evidence | Unobserved extension expansion is paused. The historical capture contains 30,812,846 descriptor-template updates and 409 template creations, contrary to the review's zero-use premise. | Enabled timeline/external extensions do not establish actual semaphore types or allow assuming those types cannot occur. |
| I-03, duplicated resource execution | D-280 gives application, merged, own-mesh and texture-only draws a shared native resource transaction. Explicit descriptor aliases, preflight, ordered emission and restoration share that path. | Geometry-specific capture and mesh preparation still have separate responsibilities. |
| I-04, pairwise refusal and stale state | D-283 replaces pair whitelists with storage-range checks and preserves split-pass state. D-290 admits the same image across disjoint sampled/output binding intervals, with readable-layout proof and existing split barriers. | Additional phase-local transitions and DX11 automatic conflicting-input nulling remain open; physical aliases, restoration accesses and queue guards retain their checks. |
| I-05, synchronous recording waits | D-278/D-280 make matched pipeline/file readiness nonblocking. D-285 extends this to graphics shader twins and unmatched graphics file publication. Pending graphics work keeps the application's draw until a later ready bind. | Compute first-use waits remain for one-shot producers. Other preparation costs are not declared eliminated. Reused command buffers retain what was recorded. |
| I-06, DX11 input compatibility | D-281 implements cross-slot textures. D-292..D-294 establish 5,265 unambiguous asset identities and 101 exact historical vertex matches. D-295 uses them for matched own/merged semantic layouts; D-298/D-299 validate independent masks against effective semantic or supported legacy declarations. The semantic GPU suite passes 143 checks and the full affected-area regression passes. | 68 historical vertex identities remain unmatched. Inherited application inputs, semantic relinking, numeric conversion and order-dependent edits remain limited; in-draw CustomShader/private-bone inputs still need ordered adapters. |
| I-07, private ABI coupling | D-279 puts native scope callbacks behind the module and retains private pipeline objects through opaque owners. A module with a different private object layout exchanges into the unchanged sandbox host. | Shared tracker/protocol layout changes correctly require a matching host. |
| I-08, old execution paths | D-282 retires the selectable single-component M2 executor. D-284 removes full-mesh binary re-encoding/decoding during runtime validation. File framing and content validation remain shared. | File size alone does not justify unrelated rewrites. |
| I-09, verification and artifacts | Four development artifacts are 3.2.0 and focused checks pass. D-296 packages the 3.1.8 no-probe candidate with semantic metadata; 22-package loading and both owned unpack layouts pass. | Root dist and game deployment remain older; sandbox and historical evidence do not close real-character verification. |

D-294 supplies the content-addressed semantic cache; D-295 loads it at generation creation and applies it to matched draws. Source declaration order, numeric conversion and other input limits remain explicit; see [the metadata evidence](shader-input-metadata.md).

## Input-layout evidence

`scripts/survey_vertex_semantics.py` reads the retained capture directory without
modifying shaders. SHA-256 deduplication of 4,724 SPIR-V files finds 637 unique
modules, including 169 with a vertex entry point. Their 1,044 input variables with
Location decorations have no OpName or HlslSemanticGOOGLE identity. None of those
vertex modules contains a variable/member HLSL semantic decoration. This is a
survey of historical data, not a new game capture or proof about every future shader.

The original `CommandList.cpp` 282 applies layout overrides to the actual DX11
element semantics, formats and offsets in order, including its offset-realignment
behavior. Vulkan Location numbers alone do not preserve arbitrary source semantic
names. The existing blend/offset contracts can remain; guessed per-character
tables cannot fill the general information gap.

D-292 finds additional identity in installed assets: paired API shader programs
and a shared source-channel table. The initial 39 objects produce 323 validated
vertex modules, 3 of which exactly match historical captures. In 211 binary pairs,
DXBC registers differ from Vulkan locations, so direct register copying is wrong.
This is an offline, asset-derived route; no runtime semantic index is installed.
See [input metadata evidence](shader-input-metadata.md) for the schema, inferred
compiler relation and remaining corpus/integration limits.

In-draw owned commands have a separate execution issue. `DrawPlanAdapter` currently
limits them to owned resources and compute slots; `OwnedRuntime::begin_draw`
records deferred work. Merely exposing an application descriptor would not make
that deferred work observe a producer earlier in the same application command
buffer. Retained resource identity, correct insertion order and access dependencies
must be solved together with I-01/I-04.

## Verification and candidate

- D-280: 72 shared native descriptor GPU checks, plus the touched-area regression.
- D-281: 98 cross-slot application image checks across merged, texture-only and own
  meshes, plus the touched-area regression.
- D-283: 29 application-image/output checks, 53 earlier output combinations and
  773 tracker checks, plus the touched-area regression.
- D-284: 129 package checks, 51 cold material checks, 98 application image checks
  and 44 matching before/after offline records for all 22 installed character Mods.
- D-285: 36 main shader checks, 91 shader boundary checks, 18 graphics file checks,
  64 output combinations, 12 compute checks, 20 matched readiness checks, 42 file
  checks, 773 tracker checks and 76 Vulkan validation checks. Early one-shot images
  captured warmup; ready-frame capture corrected the fixture without relaxing pixels.
- D-286: 16 GPU proof checks for each of no-layer/layer/inject, plus 18 wrapper
  checks. The binary submission canary is intentional and counted separately;
  there are no unexpected core/synchronization diagnostics.

The local no-probe archive is
`build/candidate-d287/EFMI-Vulkan-3.1.3.zip` in the engine-work worktree.
Its SHA-256 is
`420d4d446519174a3e7a958ef30b3688bf71142dbb2e3f02a213ef1ec260c2e8`.
It is an unpublished audit candidate, not a claim that the open queue and input
boundaries are solved. The release variant deliberately omits the probe/module
exchange control pipe as established by D-135; development host/module artifacts
retain the sandbox-verified exchange path.

The first candidate check falsely rejected all three successful folder loads
because it still parsed the pre-D-176 log spelling. `check_release.py` now accepts
the current package/bone-free-count line and the historical spelling, while keeping
the zero-left-out and exact one-character layout assertions. The final d287-release-after.out passes all archive, no-probe, 22-package load,
both unpack-layout and core/synchronization checks. The installed Mod and
launcher/game locations remain read-only; layout checks use project-owned copies.

Open implementation work remains I-01's full submission contract, I-04's sequential
access phases and I-06's owned input/semantic adapters. Real-game load and character
verification require the user's on-the-spot authorization under AGENTS.md.

## D-288 follow-up: source and candidate 3.1.4

The current source is 3.1.4 / ABI d551ddcbfa805dbb. Application semaphore lifetime
hooks now expire destroyed/recycled handle history. Failed creation, unrelated
objects, null handles, exact forwarding and driver-call lock scope are checked.
This changes the shared dispatch ABI and requires the matching host/module pair.
It does not close I-01's payload/value and cross-queue access requirements.

Tracker checks pass 783, the overlap/bone replay and 17 probe configurations pass,
Vulkan validation passes 76 and the new host/module exchange passes 18 checks.
The current unpublished candidate is
`build/candidate-d288/EFMI-Vulkan-3.1.4.zip`, SHA-256
`341e7ae42754786dc5ba5215dbd16e24e6f7617c31607f29d5318a40f0705a93`. Its archive, no-probe binary, 22-package load and both sandbox unpack
layout checks pass. It supersedes the 3.1.3 candidate described above. No actual
game deployment or real-character verification occurred.

## D-289 follow-up: source 3.1.5

Failed queue calls now remove only their own provisional signal contributions.
Successful/pending peers, object reuse and late returns are isolated, and lifetime
cleanup replaces the wholesale capacity purge. The concrete bookkeeping is opaque
to replacement modules; Device's changed storage requires the matching host.
The corrected tracker passes 801 checks and all signal-submission regression
steps pass, including actual overlap, both bone replays and module exchange.
The first build hit the previously recorded LNK1163 error; the reviewed retry
succeeded. No production cross-queue scheduler is claimed. The next implementation
work is ordered image phases; no new release archive or real-game load occurred.

## D-290 follow-up: ordered image phases in 3.1.6

The former whole-plan union now admits the exact same image when its sampled
binding intervals and output intervals are disjoint. Existing layout/ownership
preflight and pass-split barriers remain mandatory; different-image memory aliases
and retained fallback/restoration inputs keep their guards. The shared ABI is
unchanged. Twelve new CPU cases pass (tracker 813). Actual GPU scenes distinguish
new green writes from the old white source, test both orders/repeated/post phases,
reject overlaps, and complete an injected-entry run and module exchange.

The initial phase check is 51/53 because two fixture expectations were wrong:
successive draws legitimately see old then new data, and post conditions must use
the intended per-draw branch. Both corrected cases pass 12/12 on unchanged runtime
binaries. The older application-image check is initially 74/76 because two
one-shot captures selected the original warmup frame; ready-frame triggering
passes the three ordinary graphics variants, 15/15, without changing pixels.
The other touched-area steps pass; no successful step is repeated merely to hide
an initial failure. Additional phase-local layout transitions and DX11
automatic conflicting-input nulling remain separate work, as does I-01 scheduling.
The last separately checked no-probe candidate remains 3.1.4; no game was loaded.

## D-291 follow-up: acquisition scope and candidate 3.1.7

Both existing image-acquisition entries now remove stale queue-producer history
only after success/suboptimal. Sixteen deterministic mocked-driver checks bring
the tracker to 829; the extracted prior behavior fails four accepted-acquisition
cases. The affected-area regression passes, including actual pending overlap,
bone reuse, module exchange and 76 Vulkan validation checks. Shared ABI is unchanged,
but replacing only a backend cannot install these host hooks.

The current unpublished archive is `build/candidate-d291/EFMI-Vulkan-3.1.7.zip`,
SHA-256 `20ed0a51ff1bfe7aa8659aa713f859f710441289af9a83afb43366d7ef854401`. Its digest/no-probe checks, all 22 installed character packages,
both owned unpack layouts and core/synchronization validation pass. It supersedes
the 3.1.4 candidate with D-289/D-290/D-291 included. No game was loaded or deployed.

## D-293 follow-up: VFS shader identity evidence

All 17 version-5 indices pass CRC checks; all 1,075 initial bundle ranges match
their recorded MD5s. The unpacked 232 Shader objects include embedded schemas
that independently validate the earlier 39 stripped-object reads. The expanded
survey independently validates 5,266 unique vertex modules and exactly matches
101 of the 169 historical vertex modules. One empty-input sky module has
conflicting DXBC signatures and is excluded globally; no historical match is
affected. Unpaired variants, the remaining 68 historical modules and runtime
integration remain open. See shader-input-metadata.md for reproducible evidence.
This changes offline tools only; runtime and candidate remain 3.1.7.
