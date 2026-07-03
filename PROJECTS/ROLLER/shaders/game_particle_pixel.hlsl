struct PixelInput
{
    float4 position : SV_Position;
    float4 color    : TEXCOORD0;
};

float4 main(PixelInput input) : SV_Target
{
    return input.color;
}
