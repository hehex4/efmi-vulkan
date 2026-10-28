// The original as the judge (D-150): IniParams[0] and IniParams[1].x, and the first value of each of the
// buffers at t0 and t1 (zero for an unbound slot), the width of the image at t2 and the element count of
// the structured buffer at t3.
Texture1D<float4> IniParams : register(t120);
Buffer<float> t0 : register(t0);
Buffer<float> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
StructuredBuffer<float> t3 : register(t3);
RWBuffer<float> u0 : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    float4 p = IniParams.Load(int2(0, 0));
    u0[0] = p.x;
    u0[1] = p.y;
    u0[2] = p.z;
    u0[3] = p.w;
    u0[4] = IniParams.Load(int2(1, 0)).x;
    u0[5] = t0[0];
    u0[6] = t1[0];
    uint n;
    t1.GetDimensions(n);
    u0[7] = n;
    uint w, h, count, stride;
    t2.GetDimensions(w, h);
    u0[8] = w;
    t3.GetDimensions(count, stride);
    u0[9] = count;
    u0[10] = t3[0];
}
