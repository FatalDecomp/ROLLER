Texture2D hudTexture : register(t0, space2);
SamplerState hudSampler : register(s0, space2);

cbuffer HUDPixelUniforms : register(b0, space3)
{
    float vigStrength;
    float3 _pad;
};

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

float4 main(PixelInput input) : SV_Target
{
    float2 centered  = input.uv * 2.0 - 1.0;
    float  distSq    = dot(centered, centered);
    float  vignette  = 1.0 - saturate(vigStrength * distSq);

    float4 color = hudTexture.Sample(hudSampler, input.uv);
    if (color.a < 0.5)
    {
        if (vigStrength <= 0.0) discard;
        return float4(0.0, 0.0, 0.0, 1.0 - vignette);
    }
    color.rgb *= vignette;
    return color;
}
