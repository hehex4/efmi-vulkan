# Private fragment image bindings (offline checkpoint)

frontend/gpu/fragment_images.cpp relocates explicitly requested sampled-image
bindings in a fragment-only SPIR-V module. It changes just the Binding and
DescriptorSet literals. Vertex code, samplers, fragment buffers, numeric shader
instructions and all unrelated decorations remain unchanged. The caller must
create compatible private layouts/descriptors, retain images and establish actual
slot correspondence; this helper does none of those tasks implicitly.

The accepted input is one fragment entry, bounded to 4 MiB/32 requested images,
with direct, nonarray, nonmultisampled float32 sampled 2D images. Duplicate sources
or targets, occupied destinations, descriptor aliases, missing declarations and
unsupported types/decorations/instruction extensions refuse the whole rewrite.
Refusal preserves the prior output. This is a narrow relocation check, not full
SPIR-V validation or a general reflection library.

The game's declarations use Depth=2, meaning unknown depth classification in the
[SPIR-V specification](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#OpTypeImage).
The adapter follows opaque values through loads, sampled-image construction,
image extraction, copies, pointer chains, selects and phis. A requested image
reaching depth-comparison instructions or escaping through calls, stores or
composites refuses. Depth comparisons using other images do not prevent a color
texture move. Sparse/new instruction extensions are not implemented by this path.

## Standalone GPU and historical bytecode evidence (initial relocation checkpoint)

scripts/check_image_upload.py now passes 79 headless checks under core/sync
validation, including the earlier uploader/cache coverage. A material fixture
uses one original blue image in both stages. The vertex shader reads red as an
X displacement; the fragment shader outputs RGBA. With the fragment binding
relocated to a red private image, the triangle becomes red at its original
position. The next original draw is blue without rebinding its original image.
A third draw binds an explicit zero 2D image and returns RGBA zero without
null-descriptor features. The remaining quadrant stays clear green. Cached
image owners survive cache retirement before these draws are recorded/executed.

All four pixel samples pass, with zero validation errors/warnings. The private
set in this fixture uses set 3/binding 2, leaving 0/1 available for the existing
VS skeleton/entry contract. This does not combine with the production variant
cache or DrawSnapshots yet. Evidence: build/assets-gpu/report.json and
material-pixels.rgba. No layer is loaded by this headless test.

scripts/verify_fragment_images.py checks the existing display-scene slot table
and its evidence fingerprints, requires the current tested executable/source
fingerprints, and validates historical source/rewritten modules with spirv-val.
Sixteen compatible image moves across seven fragment shaders match the existing
independent Python literal-only rewrite byte for byte. All nine source shaders
are valid. Three requests correctly refuse the 2D path, as explained below.
Report: build/fragment-images/report.json. These are bytecode checks, not driver
creation of the actual game pipelines or rendered character materials.

## Original-resource correspondence is not replacement compatibility

The 19 entries of docs/arcane-texture-slots.json remain original-resource
correspondences. Three entries refer to 3D shader images, so assigning a sample
2D DDS there is incompatible with the current Vulkan adapter:

| Fragment | DX11 slot | Vulkan binding | Original DX11 draw | Mod-on DX11 draw |
| --- | --- | --- | --- | --- |
| 427e2365 | ps-t15 | set 0, binding 35 | 93 | 162 |
| 4e9748d0 | ps-t15 | set 0, binding 36 | 82 | 145 |
| 50c1e4bb | ps-t14 | set 0, binding 36 | 74 | 126 |

The original pair on both APIs is a 1x1x1 3D sRGB RGBA8 placeholder. This confirms
that the original-resource pairing was not simply a 2D/3D mismatch. In both saved
DX11 mod-on frames (frame-a/frame-on), the same slots of these custom draws hold
mod-owned 2D views: 2048-square BC7 sRGB diffuse on the first two and 8192-square
BC7 linear on the last. Capture metadata proves those bindings, not what their
DX11 shaders sampled or the resulting pixels. The offline audit is reproduced
by verify_fragment_images.py using the read-only sibling capture decoder; it
fingerprints the input headers/payloads and records every affected draw.

The table now annotates these three requests as refused_dimension_mismatch
without deleting or changing their original binding correspondence. A callback
requiring one cannot silently drop only that material operation and be accepted
as a complete replacement. Do not reinterpret a volume as 2D, infer zero output,
or change unrelated shader images without establishing the source semantics.
The initial chat description called this a mapping issue; the subsequent DX11
inspection refined it to replacement-dimension compatibility. LIMIT-021 records
the unresolved behavior. Remaining work includes native descriptor ownership,
pipeline integration, source/API dimension semantics and shared character/Core.

## Follow-up: saved DX11 bytecode and differing runtime behavior

Saved DX11 bytecode was found in the installed EFMI output archives and read
without modifying them. The zero-seed FNV-1 hash of each binary exactly equals
the PS hash in the captures. Local D3DDisassemble confirms Texture3D declarations
and one sample_l_indexable(texture3d) at LOD 0 for each affected slot. The verifier
now reproduces this check and records bytecode paths, SHA-256, declarations and
sample instructions. These are the matching historical shaders, not guesses
based solely on filenames or a synthetic shader's declarations.

[Microsoft's D3D11 functional specification, section 22.3.12](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#22.3.12%20Input%20Resource%20Declaration%20Statement)
requires a Texture3D declaration to receive a Texture3D resource. Thus the
sample's later Texture2D binding falls outside that API contract. An observation
of one implementation cannot establish a portable outcome for it.

scripts/probe_dx11_dimensions.py builds a separate headless DX11 pixel test.
Matched 2D/red and 3D/blue draws and explicit null bindings pass all four controls
on both the NVIDIA hardware adapter (vendor 0x10de, device 0x2d19) and WARP. Four
intentionally mismatched cases are observations, not positive API tests:

| Operation | NVIDIA hardware | WARP software |
| --- | --- | --- |
| 3D shader, 2D red view, z=0/0.5/2 | RGBA zero in all three | Red in all three |
| 2D shader, 3D blue view | RGBA zero | Blue |

PSGetShaderResources retains the requested view in every case on both devices;
the zero results are not explained by the API getter reporting an unbound slot.
The render target was cleared green, so zero is distinct from a skipped draw.
The debug runtime is unavailable on this installation for both devices; those
two optional runs are reported unavailable, never as validation passes. No SDK
or system feature was installed to change this. Evidence and exact source/exe
fingerprints: build/dx11-dimensions/report.json.

This establishes nonportable dimension-mismatch behavior and supports keeping
the default refusal. A compatibility policy for the observed NVIDIA behavior
must be explicit and remain separate from valid explicit-null INI semantics.
Do not coerce all volumes to 2D, assume all drivers return zero, or claim actual
game pixels were replayed. The next general operation is shader-level null
resource handling; a zero 1x1 texture alone cannot reproduce every query or
sampler-border case of an absent resource.

## Explicit null resources

`null_fragment_images` now implements explicit absent-image requests for direct,
nonarray, nonmultisampled float32 sampled 2D and 3D resources. It follows opaque
loads/copies/image construction, replaces supported sampling/fetch/size/mip
results with typed zero constants, and removes the selected opaque definitions.
It keeps the original numeric result ids and all other resource reads. Unused
global declarations remain valid; the selected image is no longer accessed.
No Vulkan null-descriptor feature or dummy image is needed. The caller must
validate source bytecode before this bounded adapter, as for relocation.

This matches the absent-resource sampling/load/resinfo behavior described by the
[D3D11 functional specification](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).
Image selection/phi, function/store/composite/extension escapes, QueryLod, storage,
arrays, multisampling and sparse instructions refuse. Refusal leaves output
unchanged, including a valid first request followed by an invalid one. It is not
a policy for the mismatched views in LIMIT-021. In the sample, component 1 requests
ps-t18=null; component 4 assigns a real normal texture to the same DX11 slot.
The backend operation has no component identifier.

The headless asset suite now passes 92 checks with clean core/sync validation.
In a 96x64 offscreen image, the original resource gives a white border sample,
width 1 and mip count 1. Null rewriting gives zero for all three while an unrelated
blue image still supplies alpha 1. Replacing the resource with a zero 1x1 texture
instead retains white/nonzero results, proving the distinction. Existing private
FS/unchanged VS and next-original-draw pixel controls still pass; a clear green
pixel proves the draws do not cover the whole target. The fixture's source and
null output also pass spirv-val. Evidence: build/assets-gpu/report.json,
build/fragment-null-check.txt and material-pixels.rgba (96x64 RGBA8).

Historical adapter coverage nulls all 19 mapped bindings across nine saved
fragment shaders, including the three 3D declarations; every result passes
spirv-val. This deliberately broad coverage is not the mod's requested null mask
or a material rendering result. The 16 relocation/reference checks and three
expected dimension refusals still pass. Report: build/fragment-images/report.json.
The helper remains standalone; production descriptor/pipeline ownership and
shared character/Core integration are the next work. No DLL/deployment/game change.
