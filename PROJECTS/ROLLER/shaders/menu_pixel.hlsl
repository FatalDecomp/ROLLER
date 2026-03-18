cbuffer PixelUniforms : register(b0, space3)
{
    float alphaMul;
    float transparentColorR;
    float transparentColorG;
    float transparentColorB;
    float replaceFromR;  // Color to replace (e.g., 0x8F palette index). Set < 0 to disable.
    float replaceFromG;
    float replaceFromB;
    float replaceToR;    // Replacement color
    float replaceToG;
    float replaceToB;
    float _pad0;         // Padding to align to 16 bytes
    float _pad1;
};

Texture2D menuTexture : register(t0, space2);
SamplerState menuSampler : register(s0, space2);

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

float4 main(PixelInput input) : SV_Target
{
    float4 color = menuTexture.Sample(menuSampler, input.uv);

    if (transparentColorR >= 0.0)
    {
        float3 diff = abs(color.rgb - float3(transparentColorR, transparentColorG, transparentColorB));
        if (diff.r < 0.004 && diff.g < 0.004 && diff.b < 0.004)
            discard;
    }

    // Color replacement (e.g., 0x8F glyph recoloring for text)
    if (replaceFromR >= 0.0)
    {
        float3 rdiff = abs(color.rgb - float3(replaceFromR, replaceFromG, replaceFromB));
        if (rdiff.r < 0.004 && rdiff.g < 0.004 && rdiff.b < 0.004)
            color.rgb = float3(replaceToR, replaceToG, replaceToB);
    }

    color.a *= alphaMul;
    return color;
}
