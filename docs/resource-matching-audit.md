# Original resource matching and checked-target audit

Date:2026-10-15. Read-only XXMI revision:
a7847eae82aaac32175c9a7d199673d5c9323bcd. This is source evidence, not imported
application resource identity or game execution.

## The command path differs from the creation helper

ResourceCopyTarget::FindTextureOverrides resolves the actual current target and
calls find_texture_overrides_for_resource. The cached command path appends hash
matches, then descriptor matches, applying matches_draw_info to each. It does
not stop after an exact hash match. The separate templated find_texture_overrides
creation helper does stop, so substituting that helper would change commands.
Keep the original list order, including priority/name ordering within each family;
do not invent an additional global sort. Resource filter queries walk the result
backward for the last explicit filter_index, then default to1 for any match.
Unbound is negative zero; bound with no match is positive zero.

The original per-resource candidate cache requires ResourceHandleInfo. Custom
allocations, shared resources and the backbuffer without that tracking record
match nothing, including descriptor predicates. A reference to a tracked source
resource retains its identity; a byte copy is a distinct resource. A Vulkan object
address or arbitrary content digest does not prove the configured source hash.

## Buffer regions and predicates

With track_region_hashes, VB/IB checks use original draw-derived region helpers.
VB uses stride and FirstVertex; its size uses VertexCount or the pinned index-count
estimate. IB uses the source index format and FirstIndex/IndexCount. No region
size follows the ordinary path. Otherwise the original draw-info prefilter runs
before the region hash is requested. A proven region hash of0 falls back to the
whole-resource hash. Unknown region data is different from a proven zero result.
Descriptor candidates remain based on the whole resource.

## Checked targets and ownership

CheckTextureOverride accepts individual resource targets, slot ranges and pool
ranges. Each range element supplies its own this target. Commands within the
match can rebind the slot; a subsequent this query sees that current binding.
The original range loop snapshots its bounds and restores the caller's this/post
state. A single dynamic target retains its expression, which the original resolver
evaluates when needed. Pool elements are read, not updated, by a check.

Each match executes the current phase, or all pre lists followed by all post lists
for the combined command form. Existing transaction, recursion, command and query
budgets must cover nested checks too. Non-finite dynamic indices must be guarded
before native integer conversions, without replacing finite source evaluation.

Next: supply proven resource snapshots to the original descriptor and draw-info
matching code, generalize checked targets and preserve owned resource identity
through references. Verify actual native candidate order, regions, pools and
rebinding before enabling directory/application placement. No game input needed.
