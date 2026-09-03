struct PSInput
{
    float4 Pos : SV_Position;
    float3 Normal : NORMAL;
    float4 Color : COLOR;
    float3 WorldPos : WORLDPOS;
};

float4 main(in PSInput PSIn) : SV_Target
{
    return float4(0.95, 0.20, 0.15, 0.40);
}