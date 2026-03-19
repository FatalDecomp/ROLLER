cbuffer VertexUniforms : register(b0, space1)
{
    float4x4 projectionMatrix;
};

struct VertexInput
{
    float2 position : TEXCOORD0;
    float2 uv       : TEXCOORD1;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = mul(projectionMatrix, float4(input.position, 0.0, 1.0));
    output.uv = input.uv;
    return output;
}
