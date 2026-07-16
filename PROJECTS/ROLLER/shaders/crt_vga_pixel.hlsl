// SPDX-FileCopyrightText: 2020-2026 The DOSBox Staging Team
// SPDX-License-Identifier: MIT
//
// VGA 1080p fake double-scan: sharp-bilinear texel scaling (the "prescale"
// technique) plus per-row scanline dimming, per-row color boost/saturation,
// and a phosphor mask. Ported from GLSL (dosbox-staging:
// resources/shaders/crt/vga-1080p-fake-double-scan.glsl) to HLSL for SDL3
// GPU. Reuses crt_vertex.hlsl (fullscreen triangle, UV already in 0..1), so
// prescale/texel math is computed here from srcSize/dstSize instead of in a
// dedicated vertex shader.

Texture2D    srcTex     : register(t0, space2);
SamplerState srcSampler : register(s0, space2);

cbuffer CRTVgaUniforms : register(b0, space3)
{
    float2 srcSize;         // source texture pixel dimensions
    float2 dstSize;         // destination viewport pixel dimensions
    float  phosphorLayout;  // 0=off 1=aperture 2=shadow-mask
    float  scanlineMin;     // SCANLINE_STRENGTH_MIN
    float  scanlineMax;     // SCANLINE_STRENGTH_MAX
    float  colorBoostEven;  // COLOR_BOOST_EVEN
    float  colorBoostOdd;   // COLOR_BOOST_ODD
    float  maskStrength;    // MASK_STRENGTH
    float  gammaInput;      // GAMMA_INPUT
    float  gammaOutput;     // GAMMA_OUTPUT
};

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

// Phosphor mask weights tiled in screen-space pixels (SV_Position.xy),
// matching the original GLSL's use of gl_FragCoord for both the mask and
// the scanline parity below.
float3 mask_weights(float2 coord, float intensity, int layout)
{
    float on  = 1.0;
    float off = 1.0 - intensity;

    float3 green   = float3(off, on,  off);
    float3 magenta = float3(on,  off, on );

    float3 aperture = lerp(magenta, green, floor(fmod(coord.x, 2.0)));

    if (layout == 1)
        return aperture;

    if (layout == 2)
    {
        float3 inv_aperture = lerp(green, magenta, floor(fmod(coord.x, 2.0)));
        return lerp(aperture, inv_aperture, floor(fmod(coord.y, 2.0)));
    }

    return float3(1.0, 1.0, 1.0);
}

float4 gamma_in(float4 c)
{
    return pow(abs(c), gammaInput);
}

// Manual bilinear sampling with gamma-correct blending (matches tex2D_linear
// in the source GLSL) -- expects a NEAREST sampler bound.
float4 tex2D_linear(float2 uv)
{
    // subtract 0.5 to centre the texel, add back after floor
    float2 texCoord = uv * srcSize - float2(0.5, 0.5);

    float2 s0t0 = floor(texCoord) + float2(0.5, 0.5);
    float2 s0t1 = s0t0 + float2(0.0, 1.0);
    float2 s1t0 = s0t0 + float2(1.0, 0.0);
    float2 s1t1 = s0t0 + float2(1.0, 1.0);

    float2 invSrcSize = 1.0 / srcSize;

    float4 c_s0t0 = gamma_in(srcTex.Sample(srcSampler, s0t0 * invSrcSize));
    float4 c_s0t1 = gamma_in(srcTex.Sample(srcSampler, s0t1 * invSrcSize));
    float4 c_s1t0 = gamma_in(srcTex.Sample(srcSampler, s1t0 * invSrcSize));
    float4 c_s1t1 = gamma_in(srcTex.Sample(srcSampler, s1t1 * invSrcSize));

    float2 w = frac(texCoord);

    float4 c0 = c_s0t0 + (c_s1t0 - c_s0t0) * w.x;
    float4 c1 = c_s0t1 + (c_s1t1 - c_s0t1) * w.x;
    return c0 + (c1 - c0) * w.y;
}

float4 add_vga_overlay(float4 color, float2 screenCoord)
{
    float3 lumFactors = float3(0.2126, 0.7152, 0.0722);
    float  luminance  = dot(lumFactors, color.rgb);

    float evenOdd = floor(fmod(screenCoord.y, 2.0));

    float dimFactor    = lerp(1.0 - scanlineMax, 1.0 - scanlineMin, luminance);
    float scanlineDim  = saturate(evenOdd + dimFactor);
    color.rgb *= scanlineDim;

    color.rgb *= lerp(colorBoostEven, colorBoostOdd, evenOdd);

    float saturation = lerp(1.2, 1.03, evenOdd);
    float l = length(color.rgb);
    color.r = pow(abs(color.r) + 1e-7, saturation);
    color.g = pow(abs(color.g) + 1e-7, saturation);
    color.b = pow(abs(color.b) + 1e-7, saturation);
    color.rgb = normalize(color.rgb) * l;

    color.rgb *= mask_weights(screenCoord, maskStrength, (int)phosphorLayout);
    return color;
}

float4 main(PixelInput input) : SV_Target
{
    float2 prescale = ceil(dstSize / srcSize);

    float2 texCoordPx = input.uv * srcSize;
    float2 texelFloor = floor(texCoordPx);
    float2 s          = frac(texCoordPx);

    float2 regionRange = float2(0.5, 0.5) - float2(0.5, 0.5) / prescale;
    float2 centerDist  = s - float2(0.5, 0.5);
    float2 f = (centerDist - clamp(centerDist, -regionRange, regionRange)) * prescale + float2(0.5, 0.5);

    float2 modTexel = min(texelFloor + f, srcSize - float2(0.5, 0.5));
    float4 color = tex2D_linear(modTexel / srcSize);

    color = add_vga_overlay(color, input.position.xy);

    color = pow(abs(color), 1.0 / gammaOutput);
    return float4(saturate(color.rgb), 1.0);
}
