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

## Standalone GPU and historical bytecode evidence

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
