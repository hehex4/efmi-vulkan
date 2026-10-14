// Test consumer for the original float4 Texture1D IniParams contract.
[[vk::binding(0, 0)]] Texture1D<float4> Parameters;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint4> Observed;

[numthreads(32, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint count;
    Parameters.GetDimensions(count);
    if (tid.x < count)
        Observed[tid.x] = asuint(Parameters.Load(int2(tid.x, 0)));
}
