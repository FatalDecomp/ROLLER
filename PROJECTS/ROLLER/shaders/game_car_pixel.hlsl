Texture2D meshTexture : register(t0, space2);
SamplerState meshSampler : register(s0, space2);

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
    float4 color    : TEXCOORD1;
    float  fogDepth : TEXCOORD2;
};

float4 main(PixelInput input) : SV_Target
{
    float4 texColor   = meshTexture.Sample(meshSampler, input.uv);
    float4 finalColor = texColor * input.color;

    if (finalColor.a < 0.01)
        discard;

    finalColor.rgb = pow(max(finalColor.rgb, 0.00001), 1.0 / gamma);

    float fogDepthAdj = max(0.0, input.fogDepth - fogStart);
    float fogFactor   = saturate(1.0 - exp(-fogDensity * fogDensity * fogDepthAdj * fogDepthAdj));
    finalColor.rgb = lerp(finalColor.rgb, fogColor.rgb, fogFactor);

    float lum = dot(finalColor.rgb, float3(0.2126, 0.7152, 0.0722));
    finalColor.rgb = lerp(float3(lum, lum, lum), finalColor.rgb, saturation);

    finalColor.rgb = saturate((finalColor.rgb - 0.5) * contrast + 0.5);
    finalColor.rgb = saturate(finalColor.rgb + brightness);

    return finalColor;
}
