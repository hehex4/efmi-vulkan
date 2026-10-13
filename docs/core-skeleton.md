# Hosted Core skeleton integration

Status, 2026-10-13: original initialization and evaluated metadata pass offline.
No complete native Core runtime or new game replacement is implied.

The command mediator retains original XXMI scalar pool reads/writes, pool aliases
and callback references. Resource references snapshot declaration pointers in the
retained frontend generation without creating D3D objects. Direct pool indices are
guarded before the original cast; nested GPU queries, unsupported copies, cycles
and expiring resource aliases refuse. Mixed scalar writes conservatively invalidate
declaration references in that pool. CPU state changes are not rolled back when
later planning fails, matching the existing planner's variable-effect limitation.

frontend/xxmi/skeleton_plan.* copies already evaluated offset/count/remap pools
into owned metadata. It validates a complete, nonoverlapping bone partition,
preserves component ids including empty declared slots, and distinguishes explicit
null (identity remap) from an unassigned LOD. Remap files require plain R16_UINT
declarations; no files are opened or GPU operations issued by this adapter.

The standalone --skeleton-mod fixture reads the installed sample and the selected
original Core files without changes. It runs the actual initialization command
list through XXMI, retains the native GPU initialization as an unresolved event,
and exports its resulting metadata. The eight remap files, 499 bones, eight
instances and twelve active components match the earlier independent literal
reference. The thirteenth component slot is empty; 28 LOD slots are unassigned,
including that slot's four entries.

Validation: original CPU host 23, draw planner 28, resource planner 19, DDS 15,
Core pools 11 and metadata 10. All 384 prior callback scenarios still pass, with
404 bounded draws. Reports are in build/xxmi-host/; scripts are
probe_xxmi_host.py, check_core_skeleton.py and check_mod_draw_plans.py.
These do not render the character.

Next connect original spatial identification, native Core operations, shared
palette completeness and command-owned snapshots. Initialization metadata alone
does not prove that every source component has supplied a valid palette or that
two draws belong to the same character instance. Missing palettes must keep the
original draw available, never become zero-filled replacement bones.

## Original spatial identification (standalone)

The selected original Core command graph is compiled transitively, including
runtime callback sources and branches. Resource expressions use explicit query
mediation, with bounded calls and no fallback to reference D3D graphics stubs.
BufferQueries reads SpatialHash coordinates from a caller-owned, bounded original
instance window, validates indices/coordinates, and invokes pinned XXMI packing.
Original pool selection, proximity, expiration, component counting and caller
splitting remain original INI/C++ behavior. Invalid windows and reserved hash
sentinels refuse; a valid physical entity id is not fabricated.

The installed Core's twelve headless cases pass for two separate objects,
first-frame delay, later custom identification, movement, distant new objects,
insufficient evidence, repeated callers, expiration and rejected inputs. Synthetic
CPU windows are used; this is not game spatial tracking or a rendered replacement.
All components within each of five historical captures separately have matching
instance positions, but those isolated captures are not continuous movement data.

Only LOD0/LOD1 are in scope (user, 2026-10-13). Keep Core's configured pool stride
for correct indexing, while ignoring later-LOD asset references. No later-LOD
adaptation or rendering test is required. Shared palette epochs/completeness and
native component/material execution remain open. Evidence:
build/xxmi-host/core-spatial-report.json and core-spatial-native.txt; reproducible
runner scripts/check_core_spatial.py rejects stale host executables.
