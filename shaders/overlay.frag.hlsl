Texture2D g_Texture;
SamplerState g_Texture_sampler;

struct PSInput
{
    float4 Pos : SV_Position;
    float2 UV : TEX_COORD;
    float4 Color : COLOR;
};

float4 main(in PSInput PSIn) : SV_Target
{
    float4 color = g_Texture.Sample(g_Texture_sampler, PSIn.UV) * PSIn.Color;
    color.rgb *= color.a;
#if OVERLAY_MANUAL_SRGB
    color.rgb = float3(
        color.r < 0.04045 ? color.r / 12.92 : pow(max(color.r + 0.055, 0.0) / 1.055, 2.4),
        color.g < 0.04045 ? color.g / 12.92 : pow(max(color.g + 0.055, 0.0) / 1.055, 2.4),
        color.b < 0.04045 ? color.b / 12.92 : pow(max(color.b + 0.055, 0.0) / 1.055, 2.4));
    float inverseAlpha = 1.0 - color.a;
    color.a = 1.0 - (inverseAlpha < 0.04045
        ? inverseAlpha / 12.92
        : pow(max(inverseAlpha + 0.055, 0.0) / 1.055, 2.4));
#endif
    return color;
}
