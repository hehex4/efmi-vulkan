# General replacement operations

Layer 0.9.2 introduces two backend operations for frontend-resolved data. Neither contains a
component number, sample hash, character name, INI conditional or asset filename. The existing
single-component package retains its eligibility restrictions and uses these operations.

## Vertex stream contracts

`patch_vertex_streams` takes the application's owned vertex declaration and a list of stream
contracts. Each contract names an original binding and stride, a replacement stride, and
original offset/format to replacement offset/format mappings. Shader locations remain intact.
Every attribute in a replaced stream must be covered; unrelated streams, including the game's
stride-zero default tangent stream, remain unchanged.

The editor checks original-layout agreement, duplicate bindings/locations/contracts, known
vertex format sizes, element bounds and budgets before publishing a result. Failure leaves
the previous output untouched. This is CPU declaration editing, not a guarantee that the
driver accepts every output or that a mod's bytes are correct. The existing variant builder
still creates pipelines asynchronously and preserves the original draw when not ready.

Native fixtures exercise UV strides 8/12/20 and bone-index source strides 4/12 with arbitrary
binding numbers, plus widening, relocation, missing/ambiguous elements and atomic failures.
The legacy component-7 adapter retains its original stricter profile; this change does not
activate other meshes in the game.

## Ordered draw transactions

`emit_variant_draws` receives up to 256 resolved items. Each item names a retained ready
pipeline, private VS descriptor/instance offset, geometry and indexed-draw arguments. It
validates every item before emitting any Vulkan command, then executes them in source order
and restores the application pipeline/VB/IB once at the end. It allocates no resources and
does not evaluate expressions. The old single-draw function delegates to this same path.

An explicitly empty resolved sequence is a successful skip with no state changes, for a
frontend's intentional hidden geometry. An item with zero indices, missing resources or an
unrestorable binding span rejects the whole sequence. The caller remains responsible for
resource ownership until GPU completion, index/vertex bounds, material state, supported
application draw state and compatibility with the original pipeline layout.

The headless GPU fixture now renders two replacement ranges with distinct private entries,
then an original draw without application rebinding. A deliberately invalid second item has
a first item that would draw in a separate pixel region; readback verifies that rejected
transactions draw nothing. Normal ranges, original FS data, restoration and resubmission are
checked under core/sync validation through no layer, layer and inject, with RenderPass 1/2.
Native call tracing separately checks nonzero first indices, signed base vertices, instance
arguments, geometry changes, ordering and one final restoration.

## Verification and remaining work

Full build and suite in `build/next` pass; log `build/general-draw-0.9.2-regression.txt`.
Recipe checks increase from 120 to 134, mesh/declaration checks from 30 to 49. Existing hook
rendering remains 45 and all other suites pass. No deployment or game observation of 0.9.2.

These are common backend mechanisms, not a complete mod loader. D-016/D-033 still require the
hosted XXMI frontend, resource loading, shared skeleton/instance identity, material descriptor
overrides, conditionals, keys/persist, reload and the sample's image panel. Frontend output
must supply the contracts and resolved draw list; adding components must not add backend
component branches. Source/build/next are 0.9.2; dist and its active settings remain 0.9.1.
