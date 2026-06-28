Texture2D    g_tex     : register(t0, space2);
SamplerState g_sampler : register(s0, space2);

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 color    : TEXCOORD1;
};

float4 main(PixelInput input) : SV_Target
{
    float4 tex = g_tex.Sample(g_sampler, input.uv);
    /* palette index 0 → alpha 0 (transparent background)
     * non-zero index  → alpha 1 (sprite pixel, show raw palette colour)
     * input.color.a carries the per-particle overall opacity (0.5 if TRANSPARENT) */
    return float4(tex.rgb, tex.a * input.color.a);
}
