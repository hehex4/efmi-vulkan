# M2 CPU rolling-buffer skeleton import

Layer source/candidate 0.8.2 adds `merged_skeleton.h/.cpp`. The probe does not call it.
It implements the installed Core's rolling-buffer importer and instance patch semantics,
with checked input windows and atomic failure. The constant-buffer source path, spatial
identity, INI execution, GPU upload and drawing are separate work.

## Addressing and update rules

The first 512 matrices are identity transforms. Each instance then owns `bones_count`
current matrices and the same number of previous matrices. A matrix is three float4 rows.
The instance entry is 256 bytes; constant 5, at byte 80, carries current/previous offsets
in float4 elements. Offsets below 1536 refer to implicit transforms and remain unchanged.

For a component bone `i`, the source is `remap[i]` (or `i` without a map). Copy the matrix
at source offset plus `3 + 3 * source_bone` rows into the merged component range; the
leading source matrix is skipped. The remap direction is destination bone to source bone,
and repeated source indices are legal. Mesh scale multiplies all twelve float terms,
including translation. Current/previous inputs and destination slices are independent.

Import once per logical update, component and instance. A lower LOD number may replace
an earlier import in that update. Increasing the serial keeps the CPU master but invalidates
current-update stamps. The caller supplies this serial; empty present intervals must not be
mistaken for complete animation updates. The helper does not decide submission boundaries.

Only the two dynamic bone offsets are patched in the copied instance entry. Flags, translation
and every other byte are preserved. The new offsets are one matrix (three rows) below the
instance's first merged bone: `1536 + 6 * bones * instance - 3` for the current pose and that plus
`3 * bones` for the previous one. The game's vertex shaders read bone `i` at
`offset + 3 + 3 * i`, the same lead matrix that the importer skips on the source side, and the
core passes `$merged_skeleton_offset - 3` to its instance config overrider (`MergedSkeleton.ini`;
the subtraction is in the INI, not in the HLSL). Layers 0.8.2 to 0.9.0 wrote the offsets without
the subtraction; see the correction below. Bounded component layouts must partition the merged
skeleton. Both source windows, remap length, scale and all consumed floats are checked before
any master byte or import stamp changes. An invalid import leaves the original draw available.
An independent copy of `bytes()` remains unchanged by later imports; the GPU integration must
retain its own immutable snapshot until the corresponding GPU work is complete.

## Verification

Native tests: 42 checks for exact row addressing, duplicate remaps, scale, independent poses,
nonzero instances, static/dynamic thresholds, finer-LOD priority, frame carry-over, bounds,
unaligned sources, nonfinite values, arithmetic budgets and atomic failures.

`scripts/verify_bone_import.py` reads literal skeleton declarations and R16_UINT remap files
from the installed sample without modifying them. It verifies each bundle fingerprint and
the matching INI fingerprint, selects actual VS bone snapshots, requires record/submit
equality, feeds the same inputs into production C++, and independently evaluates the Core
row-addressing rules in Python. It is a narrow offline verifier, not an INI interpreter.
Base sections with no LOD remap use their identity mapping. Replay uses scale 1; synthetic
native tests exercise other finite scales.

Five saved frames replay 283 matched draws: one 0.8.0 display frame, two 0.8.0 world LOD0
frames and both new 0.8.1 actual LOD1 frames. There are 74 imports and 209 repeats; the LOD0
world frames include upgrades after earlier LOD1 depth imports. After every record, the
entire 407,808-byte, eight-instance master and patched entry match the reference byte for
byte. Inputs include all eight installed LOD1 remap buffers; maximum source indices are
43, 85, 40, 14, 15, 77, 115 and 22. No source or asset data is committed. Replay report: `build/bone-replay-0sdxllk2/report.json`.

Full build/regression passes in `build/next`: unit 88, tracker 144, bones 42, recipes 120,
variants 37, 17 probe cases 517-532, modes 40, limits 174, timing 126, validation 76,
MCP 47 and evidence 113. Log: `build/bone-import-regression-20261012.txt`.

## Correction in 0.9.1: the patched offsets were one matrix too high

Found in review on 2026-10-12, before any game process had loaded a layer that replaces draws.
`patch_entry` wrote `(1536, 1536 + 3 * bones)` for the first instance. Three independent sources
say `(1533, 3030)` for the 499-bone sample:

- `MergedSkeleton.ini` sets `x0 = $merged_skeleton_offset - 3` for the instance config overrider.
- The DX11 baseline (`captures/dx11-baseline`, frames `frame-a` and `frame-on`): all 23 vertex
  instance entries that the core patched hold `(1533, 3030, 258)` in constant 5.
- The game's Vulkan vertex shader of the component 7 draw (shader 111231 of pipeline 402 in
  `captures/third-probe`, disassembled): `row = constant5.x + 3 + 3 * blend index`, entry chosen by
  `gl_InstanceIndex`.

With the old value every vertex would have taken the matrix of the next bone, and the last bone
of a component a matrix that the per-draw snapshot leaves zero. The tests did not show it because
they shared the assumption: the native test expected 1536, the Python reference computed the same
value, and the sandbox vertex shader read `offset + 3 * index` with the original mesh on bone 1.
The sandbox shader now addresses bones as the game's does and the original mesh uses bone 0; with
that shader the 0.9.0 layer fails all four GPU configurations, and 0.9.1 passes them. The native
test also checks the measured DX11 pair. The replay over the five saved frames passes again with
the corrected reference: all 283 patched entries hold `(1533, 3030)`, the master is unchanged
(`build/bone-replay-yuopgr1q/report.json`).

The Python reference is derived from the inspected Core HLSL and INI; no saved DX11 compute-output
buffer was available for a direct output comparison. This result is offline agreement on
saved game inputs, not original-HLSL GPU execution, live replacement, or animation proof.
The installed game continues to hold dist/0.8.1.
