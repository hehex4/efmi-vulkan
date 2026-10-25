// The merged skeleton's import on the GPU (D-117): one component's bone matrices of one or two
// poses, from the game's bone buffer into a slot of the identity's ring, as the Core's own
// MergedSkeleton_BoneDataImporter_RollingBuffer.hlsl does. One thread per bone of the component.
// Compiled by scripts/build.py with DXC into layer/build/bone_merge_spirv.cpp.
struct Push {
    uint source_rows0;  // the entry's offsets: rows of 16 bytes into the source range, pose 0
    uint source_rows1;  // pose 1 (the previous pose)
    uint destination;   // the slot's first row of pose 0, bone 0 (after the implicit rows)
    uint pose_rows;     // rows of one pose: the skeleton's bones times 3
    uint first;         // the component's first merged bone
    uint count;         // its bones
    uint flags;         // 1 pose 0 imported, 2 pose 1 imported, 4 source bones through the remap
    uint remap_offset;  // first uint of the component's remap table
    float scale;        // the Core's custom mesh scale (1)
};
[[vk::push_constant]] Push pc;
[[vk::binding(0, 0)]] StructuredBuffer<float4> Source;        // the game's bone rows
[[vk::binding(1, 0)]] StructuredBuffer<uint> Remap;           // the package's LOD remap tables
[[vk::binding(2, 0)]] RWStructuredBuffer<float4> Destination; // the ring slot

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint bone = id.x;
    if (bone >= pc.count) return;
    uint source = (pc.flags & 4) ? Remap[pc.remap_offset + bone] : bone;
    // The game's shaders read bone i at offset + 3 + 3 * i: the matrix before bone zero is skipped.
    uint src = 3 + source * 3;
    uint dst = pc.destination + (pc.first + bone) * 3;
    if (pc.flags & 1) {
        uint s = pc.source_rows0 + src;
        Destination[dst + 0] = Source[s + 0] * pc.scale;
        Destination[dst + 1] = Source[s + 1] * pc.scale;
        Destination[dst + 2] = Source[s + 2] * pc.scale;
    }
    if (pc.flags & 2) {
        uint s = pc.source_rows1 + src;
        uint d = dst + pc.pose_rows;
        Destination[d + 0] = Source[s + 0] * pc.scale;
        Destination[d + 1] = Source[s + 1] * pc.scale;
        Destination[d + 2] = Source[s + 2] * pc.scale;
    }
}
