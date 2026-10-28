// The original as the judge (D-150): compute.ini's CustomShader.
RWBuffer<float> output : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    output[id.x] = id.x * 2.0 + 1.0;
}
