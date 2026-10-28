// The original as the judge (D-150): one texel and the dimensions of the image at t0.
Texture2D<float4> t0 : register(t0);
RWBuffer<float> u0 : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    float4 a = t0.Load(int3(1, 2, 0));
    uint w, h;
    t0.GetDimensions(w, h);
    u0[0] = a.x;
    u0[1] = a.y;
    u0[2] = a.z;
    u0[3] = a.w;
    u0[4] = w;
    u0[5] = h;
}
