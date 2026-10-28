// The original as the judge (D-150): the lower left half of the target, x + y <= 0 in clip space.
// IniParams[0].y above one half swaps two corners, turning the triangle's winding round.
Texture1D<float4> IniParams : register(t120);

void main(uint id : SV_VertexID, out float4 pos : SV_Position)
{
    float2 corners[3] = { float2(-1, -1), float2(-1, 1), float2(1, -1) };
    uint index = id;
    if (IniParams.Load(int2(0, 0)).y > 0.5 && id > 0)
        index = 3 - id;
    pos = float4(corners[index], 0, 1);
}
