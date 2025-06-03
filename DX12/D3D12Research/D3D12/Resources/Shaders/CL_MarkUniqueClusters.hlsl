cbuffer Parameters : register(b0)
{
    float4x4 cWorldView;
    float4x4 cProjection;
    uint4 cClusterDimensions;
    float2 cClusterSize;
    float cSliceMagicA;
    float cSliceMagicB;
}

// UAV(register u#) and render target outputs(SV_Target#) share the same register namespace.
// SV_Target0 implicitly uses u0 internally.
RWStructuredBuffer<uint> uActiveCluster : register(u1);

Texture2D tDiffuseTexture : register(t0);
SamplerState sDiffuseSampler : register(s0);

uint GetSliceFromDepth(float depth)
{
    return floor(log(depth) * cSliceMagicA - cSliceMagicB);
}

struct VS_Input
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD;
};

struct PS_Input
{
    float4 position : SV_Position;
    float4 positionVS : TEXCOORD0;
    float2 texcoord : TEXCOORD1;
};

PS_Input MarkClusters_VS(VS_Input input)
{
    PS_Input output = (PS_Input)0;
    output.positionVS = mul(float4(input.position, 1.0f), cWorldView);
    output.position = mul(output.positionVS, cProjection);
    output.texcoord = input.texcoord;
    return output;
}

void MarkClusters_PS(PS_Input input)
{
    uint zSlice = GetSliceFromDepth(input.positionVS.z);
    uint2 clusterIndexXY = floor(input.position.xy / cClusterSize);
    uint clusterIndex1D = clusterIndexXY.x + clusterIndexXY.y * cClusterDimensions.x + zSlice * cClusterDimensions.x * cClusterDimensions.y;

#ifdef ALPHA_BLEND
    float s = tDiffuseTexture.Sample(sDiffuseSampler, input.texcoord).a;
    if (s < 0.01f)
    {
        discard;
    }
    uActiveCluster[clusterIndex1D] = ceil(s);
#else
    uActiveCluster[clusterIndex1D] = 1;
#endif
}


