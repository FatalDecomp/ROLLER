/* Screen-space textured particle quad: NDC position (xyz) + UV + per-vertex flat color. */
struct VertexInput
{
    float3 position : TEXCOORD0;
    float2 uv       : TEXCOORD1;
    float4 color    : TEXCOORD2;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 color    : TEXCOORD1;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 1.0);
    output.uv       = input.uv;
    output.color    = input.color;
    return output;
}
