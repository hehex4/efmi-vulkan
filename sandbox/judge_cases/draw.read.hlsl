// The original as the judge (D-150): the texels at (1, 2), inside the drawn half, and (3, 0), outside it.
Texture2D<float4> t0 : register(t0);
RWBuffer<float> u0 : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    float4 a = t0.Load(int3(1, 2, 0));
    float4 b = t0.Load(int3(3, 0, 0));
    u0[0] = a.x;
    u0[1] = a.y;
    u0[2] = a.z;
    u0[3] = a.w;
    u0[4] = b.x;
    u0[5] = b.y;
    u0[6] = b.z;
    u0[7] = b.w;
}
