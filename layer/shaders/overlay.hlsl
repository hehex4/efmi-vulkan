// The overlay's notices (D-137, layer/src/notices.cpp): the text image the host drew with GDI, one
// texel per pixel at the top left of the presented image, premultiplied by its alpha, as 3DMigoto's
// notices are drawn over the frame (Overlay.cpp DrawNotices). Compiled by scripts/build.py with DXC
// into layer/build/overlay_spirv.cpp.
struct Push {
    uint srgb; // the target encodes sRGB: the text's colours are given encoded, the target expects them linear
};
[[vk::push_constant]] Push pc;
[[vk::binding(0, 0)]] Texture2D<float4> Text;

// A triangle that covers the viewport, which the host sets to the text image's rectangle.
float4 vs_main(uint id : SV_VertexID) : SV_Position
{
    const float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * 2.0 - 1.0, 0.0, 1.0);
}

float3 linear_from_srgb(float3 c)
{
    return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4));
}

float4 ps_main(float4 position : SV_Position) : SV_Target
{
    float4 c = Text.Load(int3(int2(position.xy), 0));
    if (pc.srgb != 0 && c.a > 0.0)
        c.rgb = linear_from_srgb(c.rgb / c.a) * c.a;
    return c;
}
