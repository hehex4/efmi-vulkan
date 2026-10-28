// The original as the judge (D-150): a typed buffer at t0, a structured buffer at t1, a raw buffer at t2,
// a typed buffer read with another format at t3; first values, element counts.
Buffer<float> t0 : register(t0);
StructuredBuffer<float> t1 : register(t1);
ByteAddressBuffer t2 : register(t2);
Buffer<float4> t3 : register(t3);
RWBuffer<float> u0 : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    uint n, count, stride, bytes;
    t0.GetDimensions(n);
    u0[0] = t0[1];
    u0[1] = n;
    t1.GetDimensions(count, stride);
    u0[2] = t1[2];
    u0[3] = count;
    t2.GetDimensions(bytes);
    u0[4] = asfloat(t2.Load(4));
    u0[5] = bytes;
    float4 v = t3[0];
    u0[6] = v.x;
    u0[7] = v.y;
    t3.GetDimensions(n);
    u0[8] = n;
}
