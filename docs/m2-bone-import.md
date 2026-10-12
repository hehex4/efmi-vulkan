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
and every other byte are preserved. Bounded component layouts must partition the merged
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

The Python reference is derived from the inspected Core HLSL; no saved DX11 compute-output
buffer was available for a direct output comparison. This result is offline agreement on
saved game inputs, not original-HLSL GPU execution, live replacement, or animation proof.
The installed game continues to hold dist/0.8.1.
