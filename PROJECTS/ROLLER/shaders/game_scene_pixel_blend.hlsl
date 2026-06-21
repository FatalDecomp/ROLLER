Texture2D sceneTexture : register(t0, space2);
SamplerState sceneSampler : register(s0, space2);

cbuffer ScenePixelUniforms : register(b0, space3)
{
    float  fogDensity;
    float  gamma;
    float  fogStart;
    float  saturation;
    float4 fogColor;
    float  contrast;
    float  brightness;
    float2 _pad;
};

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float  fogDepth : TEXCOORD1;
};

float4 main(PixelInput input) : SV_Target
{
    float4 color = sceneTexture.Sample(sceneSampler, input.uv);

    color.rgb = pow(max(color.rgb, 0.00001), 1.0 / gamma);

    float fogDepthAdj = max(0.0, input.fogDepth - fogStart);
    float fogFactor   = saturate(1.0 - exp(-fogDensity * fogDensity * fogDepthAdj * fogDepthAdj));
    color.rgb = lerp(color.rgb, fogColor.rgb, fogFactor);

    float lum = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    color.rgb = lerp(float3(lum, lum, lum), color.rgb, saturation);

    color.rgb = saturate((color.rgb - 0.5) * contrast + 0.5);
    color.rgb = saturate(color.rgb + brightness);

    return color;
}
