struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VertexOutput main(uint vertexID : SV_VertexID)
{
    float2 uv = float2((vertexID & 1) * 2.0, (vertexID >> 1) * 2.0);
    VertexOutput output;
    output.uv = uv;
    output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return output;
}
