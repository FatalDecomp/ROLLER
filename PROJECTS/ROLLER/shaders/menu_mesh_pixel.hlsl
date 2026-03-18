Texture2D meshTexture : register(t0, space2);
SamplerState meshSampler : register(s0, space2);

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 color    : TEXCOORD1;
};

float4 main(PixelInput input) : SV_Target
{
    float4 texColor = meshTexture.Sample(meshSampler, input.uv);
    float4 finalColor = texColor * input.color;

    if (finalColor.a < 0.01)
        discard;

    return finalColor;
}
