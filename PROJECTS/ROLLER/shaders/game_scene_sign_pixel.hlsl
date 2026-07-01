// Sign depth-check pixel shader.
// Replicates SW painter's-algorithm behaviour (buildings always over track)
// without COMPARE_ALWAYS far-side bleed-through: the pass is split after
// BUILDING draws, the depth buffer is copied to depthCopy, then signs render
// with COMPARE_ALWAYS + no depth write while this shader discards pixels where
// the sign is more than SIGN_MAX_OCCLUDE_DIST world units behind the snapshot depth.
// Linearising to view-space avoids NDC compression falsely passing distant skyline
// buildings (vZ=185k+) that are only ~0.0016 NDC behind foreground geometry.
Texture2D        sceneTexture : register(t0, space2);
SamplerState     sceneSampler : register(s0, space2);
Texture2D<float> depthCopy    : register(t1, space2);
SamplerState     depthSampler : register(s1, space2); // required binding; Load() is used, not Sample()

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

// Confirmed engine projection constants (near=80, far=20000000).
// NDC formula: z_ndc = far*(z-near)/(z*(far-near))
// Inverse:     z_view = zNear*zFar / (zFar - z_ndc*(zFar-zNear))
static const float Z_NEAR = 80.0f;
static const float Z_FAR  = 20000000.0f;
// Signs at most this many world units behind the pre-sign depth snapshot are shown.
// Must exceed the canyon-sign gap (wall vZ≈19189, sign vZ≈27253 → gap≈8064 units)
// while hiding skyline buildings (gap≈145000+ units).
static const float SIGN_MAX_OCCLUDE_DIST = 15000.0f;

float linearDepth(float ndcZ)
{
    return Z_NEAR * Z_FAR / (Z_FAR - ndcZ * (Z_FAR - Z_NEAR));
}

float4 main(PixelInput input) : SV_Target
{
    float priorDepth  = depthCopy.Load(int3((int2)input.position.xy, 0));
    float signLinear  = linearDepth(input.position.z);
    float priorLinear = linearDepth(priorDepth);
    if (signLinear - priorLinear > SIGN_MAX_OCCLUDE_DIST)
        discard;

    float4 color = sceneTexture.Sample(sceneSampler, input.uv);
    if (color.a < 0.5) discard;

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
