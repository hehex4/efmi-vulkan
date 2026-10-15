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

An expression querying this delegates GetResource to the current checked target,
but FindTextureOverrides still sees target type THIS. It consequently uses the
whole-resource path even when that checked target is IB/VB. Preserve this source
detail rather than normalizing the query type along with its bound resource.
Explicit pre/post prefixes select a combined target pre/post invocation; they do
not mean that only the selected target phase runs. An unprefixed check participates
in the caller's ordinary pre/post scheduling.

Each match executes the current phase, or all pre lists followed by all post lists
for the combined command form. Existing transaction, recursion, command and query
budgets must cover nested checks too. Non-finite dynamic indices must be guarded
before native integer conversions, without replacing finite source evaluation.

## Owned implementation, candidate1.3.1

resource_hash_module.cpp compiles the unchanged source matcher and exposes its
private descriptor matcher to proven CPU snapshots. No fake COM objects or new
predicate parser is needed. Original draw maps and Rendering.track_region_hashes
are parsed with the existing source getters. Unknown facts refuse when required;
unused region data is not requested. Source bytes are never read back implicitly.

General CheckTextureOverride supports resolved resources, stage/IA/output slots,
slot ranges and wrapped pool element ranges. Owned allocations receive explicit
source facts through weak allocation identities; references share them while new
allocations do not acquire their source's hash by copying bytes. The importing
host is responsible for updating facts under its source hash policy. Standalone
custom allocations without native tracking records correctly match nothing.

CPU311 checks include region prefilter/fallback, unknown identities, dynamic this
and non-finite guards. GPU9242 checks include80 exact native cached-matcher
comparisons over real WARP buffers/1D/2D/3D resources, identity0/no-record cases,
source draw predicates/order, all shader SRVs, IA/UAV slots, wrapped pools,
reference/copy identity and live this rebinding. Core/sync validation is clean.

Full rebuilt regression passes:build/next/run_all-1.3.1.out exit0/all steps;
source hashes99/9/12 match, compiler172/58 and corpus37/37 pass, text345 clean.
Installed inputs and dist1.0.2 unchanged; no game or system action. These are
owned execution results, not application resource/hash provenance. Stream output,
swapchain/indirect resources and imported draw placement need separate adapters.
Next is original ShaderRegex linkage; no game input is required at this boundary.
