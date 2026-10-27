struct PSInput
{
    float4 Pos : SV_Position;
    float4 Color : COLOR;
};

float4 main(in PSInput PSIn) : SV_Target
{
    return PSIn.Color;
}
