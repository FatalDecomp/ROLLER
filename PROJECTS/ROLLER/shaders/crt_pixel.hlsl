// SPDX-FileCopyrightText: 2011 Hyllian <sergiogdb@gmail.com>
// SPDX-FileCopyrightText: 2011 Tyrells
// SPDX-FileCopyrightText: 2021-2023 The DOSBox Staging Team
// SPDX-License-Identifier: MIT
//
// CRT-Hyllian: scanlines + phosphor mask shader.
// Ported from GLSL (dosbox-staging) to HLSL for SDL3 GPU.
//
// Y pix_coord offset is +0.5 (dosbox convention) so that bright scanline
// centres fall at source-texel centres rather than boundaries — correct for
// upscaling a low-resolution source (e.g. 640x400) to a large viewport.

Texture2D    srcTex     : register(t0, space2);
SamplerState srcSampler : register(s0, space2);

cbuffer CRTUniforms : register(b0, space3)
{
    float2 srcSize;         // source texture pixel dimensions
    float2 dstSize;         // destination viewport pixel dimensions (unused; reserved)
    float  scanlines;       // SCANLINES_STRENGTH   0-1 (multiplied by 4 internally)
    float  beamMin;         // BEAM_MIN_WIDTH        0-1
    float  beamMax;         // BEAM_MAX_WIDTH        0-1
    float  colorBoost;      // COLOR_BOOST           1-3
    float  phosphorLayout;  // PHOSPHOR_LAYOUT       0=off 1=aperture 2=shadow
    float  maskIntensity;   // MASK_INTENSITY        0-1
    float  inputGamma;      // INPUT_GAMMA
    float  outputGamma;     // OUTPUT_GAMMA
    float  antiRinging;     // CRT_ANTI_RINGING      0-1
    float  _pad;
};

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

// Phosphor mask weights tiled in screen-space pixels (SV_Position.xy).
// Only layouts 0, 1, 2 — enough for 1080p.
float3 mask_weights(float2 coord, float intensity, int layout)
{
    float on  = 1.0;
    float off = 1.0 - intensity;

    float3 magenta = float3(on,  off, on );
    float3 green   = float3(off, on,  off);

    if (layout == 1)
    {
        // aperture grille — alternating magenta/green columns
        return lerp(magenta, green, floor(fmod(coord.x, 2.0)));
    }
    else if (layout == 2)
    {
        // 2x2 shadow mask — row-offset aperture grille
        float3 aperture     = lerp(magenta, green, floor(fmod(coord.x, 2.0)));
        float3 inv_aperture = lerp(green,   magenta, floor(fmod(coord.x, 2.0)));
        return lerp(aperture, inv_aperture, floor(fmod(coord.y, 2.0)));
    }

    return float3(on, on, on);
}

float4 main(PixelInput input) : SV_Target
{
    float dx = 1.0 / srcSize.x;
    float dy = 1.0 / srcSize.y;

    // pix_coord: +0.5 on Y so bright centres land on source-texel centres.
    float2 pixCoord = input.uv * srcSize + float2(-0.5, 0.5);
    float2 tc       = (floor(pixCoord) + float2(0.5, 0.5)) / srcSize;
    float2 fp       = frac(pixCoord);

    // --- 8 samples: 2 scanline rows x 4 horizontal taps ---
    float4 c00 = pow(abs(srcTex.Sample(srcSampler, tc + float2(-dx, -dy))), inputGamma);
    float4 c01 = pow(abs(srcTex.Sample(srcSampler, tc + float2(  0, -dy))), inputGamma);
    float4 c02 = pow(abs(srcTex.Sample(srcSampler, tc + float2( dx, -dy))), inputGamma);
    float4 c03 = pow(abs(srcTex.Sample(srcSampler, tc + float2(2*dx,-dy))), inputGamma);
    float4 c10 = pow(abs(srcTex.Sample(srcSampler, tc + float2(-dx,   0))), inputGamma);
    float4 c11 = pow(abs(srcTex.Sample(srcSampler, tc                   )), inputGamma);
    float4 c12 = pow(abs(srcTex.Sample(srcSampler, tc + float2( dx,   0))), inputGamma);
    float4 c13 = pow(abs(srcTex.Sample(srcSampler, tc + float2(2*dx,  0))), inputGamma);

    // --- Catmull-Rom cubic horizontal weights ---
    float  tx  = fp.x;
    float  tx2 = tx  * tx;
    float  tx3 = tx2 * tx;
    float4 w   = float4(
        -0.5*tx3 +      tx2 - 0.5*tx,   // w0  (c_0)
         1.5*tx3 - 2.5*tx2        + 1.0, // w1  (c_1, center-left)
        -1.5*tx3 + 2.0*tx2 + 0.5*tx,    // w2  (c_2, center-right)
         0.5*tx3 - 0.5*tx2               // w3  (c_3)
    );

    float4 color0 = c00*w.x + c01*w.y + c02*w.z + c03*w.w;
    float4 color1 = c10*w.x + c11*w.y + c12*w.z + c13*w.w;

    // --- Anti-ringing: clamp cubic result to [min, max] of the two centre taps ---
    float4 lo0 = min(c01, c02), hi0 = max(c01, c02);
    float4 lo1 = min(c11, c12), hi1 = max(c11, c12);

    float4 aux0 = color0;
    color0 = clamp(color0, lo0, hi0);
    color0 = lerp(aux0,   color0, antiRinging * step(0.0, (c00 - c01) * (c02 - c03)));

    float4 aux1 = color1;
    color1 = clamp(color1, lo1, hi1);
    color1 = lerp(aux1,   color1, antiRinging * step(0.0, (c10 - c11) * (c12 - c13)));

    // --- Vertical Gaussian scanline falloff ---
    float scanStr = 4.0 * scanlines;
    float pos0 = fp.y;
    float pos1 = 1.0 - fp.y;

    float4 lum0 = lerp(beamMin, beamMax, color0);
    float4 lum1 = lerp(beamMin, beamMax, color1);

    float4 d0 = scanStr * pos0 / (lum0 + 1e-7);
    float4 d1 = scanStr * pos1 / (lum1 + 1e-7);
    d0 = exp(-d0 * d0);
    d1 = exp(-d1 * d1);

    float4 color = colorBoost * (color0 * d0 + color1 * d1);

    // --- Phosphor mask ---
    // X: screen-space column (SV_Position.x) — physically correct; each column
    //    is a fixed vertical phosphor stripe.
    // Y: source texel row (floor(pixCoord.y)) — content-synchronized so the
    //    mask phase travels with the image.  Using screen-space Y here would
    //    cause the mask row parity to flip as uniform content (sky) scrolls
    //    vertically, producing a visible color flicker during camera movement.
    float2 maskCoord = float2(input.position.x, floor(pixCoord.y));
    color.rgb *= mask_weights(maskCoord, maskIntensity, (int)phosphorLayout);

    // --- Output gamma encode ---
    color = clamp(pow(abs(color), 1.0 / outputGamma), 0.0, 1.0);

    return float4(color.rgb, 1.0);
}
