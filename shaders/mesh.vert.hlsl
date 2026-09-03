cbuffer CameraConstants
{
    row_major float4x4 g_ViewProj;
    float4 g_CameraPos;
};

struct VSInput
{
    float3 Pos : ATTRIB0;
    float3 Normal : ATTRIB1;
    float2 UV : ATTRIB2;
    float4 Row0 : ATTRIB3;
    float4 Row1 : ATTRIB4;
    float4 Row2 : ATTRIB5;
    float4 Row3 : ATTRIB6;
    float4 Color : ATTRIB7;
};

struct PSInput
{
    float4 Pos : SV_Position;
    float3 Normal : NORMAL;
    float4 Color : COLOR;
    float3 WorldPos : WORLDPOS;
};

void main(in VSInput VSIn, out PSInput PSIn)
{
    float4x4 InstanceMat = MatrixFromRows(VSIn.Row0, VSIn.Row1, VSIn.Row2, VSIn.Row3);
    float4 WorldPos = mul(InstanceMat, float4(VSIn.Pos, 1.0));
    PSIn.WorldPos = WorldPos.xyz;
    PSIn.Pos = mul(g_ViewProj, WorldPos);
    PSIn.Normal = normalize(mul((float3x3)InstanceMat, VSIn.Normal));
    PSIn.Color = VSIn.Color;
}