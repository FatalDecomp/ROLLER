Texture2D overlayTexture : register(t0, space2);
SamplerState overlaySampler : register(s0, space2);

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

float4 main(PixelInput input) : SV_Target
{
    return overlayTexture.Sample(overlaySampler, input.uv);
}
