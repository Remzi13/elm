cbuffer CameraConstants
{
    row_major float4x4 g_ViewProj;
};

struct VSInput
{
    float3 Pos : ATTRIB0;
    float4 Color : ATTRIB1;
};

struct PSInput
{
    float4 Pos : SV_Position;
    float4 Color : COLOR;
};

// Pulls debug primitives slightly towards the camera, so a line lying on a surface
// (a collider edge on its model) is not hidden by that surface
static const float DepthBias = 2e-6;

void main(in VSInput VSIn, out PSInput PSIn)
{
    PSIn.Pos = mul(g_ViewProj, float4(VSIn.Pos, 1.0));
    PSIn.Pos.z -= DepthBias * PSIn.Pos.w;
    PSIn.Color = VSIn.Color;
}
