struct PSInput
{
    float4 Pos : SV_Position;
    float3 Normal : NORMAL;
    float4 Color : COLOR;
    float3 WorldPos : WORLDPOS;
};

float4 main(in PSInput PSIn) : SV_Target
{
    float3 lightDir = normalize(float3(0.4, 0.9, -0.5));
    float diff = max(dot(PSIn.Normal, lightDir), 0.0);
    float3 lighting = float3(0.24, 0.26, 0.30) + float3(0.85, 0.85, 0.82) * diff;
    return float4(PSIn.Color.rgb * lighting, PSIn.Color.a);
}