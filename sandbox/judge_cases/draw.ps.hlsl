// The original as the judge (D-150): IniParams[0].x as red, full green, no blue, alpha 0.6.
Texture1D<float4> IniParams : register(t120);

float4 main(float4 pos : SV_Position) : SV_Target0
{
    return float4(IniParams.Load(int2(0, 0)).x, 1, 0, 0.6);
}
