struct PSInput
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

Texture2D<float4> tCurrentTexture : register(t0);
SamplerState sPointSampler : register(s0);

Texture2D tHistoryTexture : register(t1);
SamplerState sLinearSampler : register(s1);

// vertexID from 0 to 3 in full screen quad
// | vertexID | position (x, y) | texCoord (x, y) |
// |----------|-----------------|-----------------|
// | 0 | (-1, -1) | (0, 1) |
// | 1 | (-1, 3) | (0, -1) |
// | 2 | (3, -1) | (2, 1) |
// | 3 | (3, 3) | (2, -1) |
PSInput VSMain(uint vertexID : SV_VertexID)
{
    PSInput output;
    output.position.x = (float)(vertexID / 2) * 4.0f - 1.0f;
    output.position.y = (float)(vertexID % 2) * 4.0f - 1.0f;
    output.position.z = 0.0f;
    output.position.w = 1.0f;

    output.texCoord.x = (float)(vertexID / 2) * 2.0f;
    output.texCoord.y = 1.0f - (float)(vertexID % 2) * 2.0f;

    return output;
}

float4 PSMain(PSInput input) : SV_Target
{
    float4 currentColor = tCurrentTexture.Sample(sPointSampler, input.texCoord);
    float4 historyColor = tHistoryTexture.Sample(sLinearSampler, input.texCoord);

    return lerp(historyColor, currentColor, 0.5f);
}






