#include "Common.hlsli"

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
                "CBV(b1, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u1, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL)"

cbuffer PerObjectParameters : register(b0)
{
    float4x4 cWorld;
}

cbuffer PerViewParameters : register(b1)
{
    int4 cClusterDimensions;
    int2 cClusterSize;
    float2 cLightGridParams;
    float4x4 cView;
    float4x4 cViewProjection;
}

// UAV(register u#) and render target outputs(SV_Target#) share the same register namespace.
// SV_Target0 implicitly uses u0 internally.
RWStructuredBuffer<uint> uActiveCluster : register(u1);

// clusterCountZ * (log(depth) - log(n)) / log(f / n)
uint GetSliceFromDepth(float depth)
{
    return floor(log(depth) * cLightGridParams.x - cLightGridParams.y);
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

[RootSignature(RootSig)]
PS_Input MarkClusters_VS(VS_Input input)
{
    PS_Input output = (PS_Input)0;
    float4 posWS = mul(float4(input.position, 1.0f), cWorld);
    output.positionVS = mul(posWS, cView);
    output.position = mul(posWS, cViewProjection);
    output.texcoord = input.texcoord;
    return output;
}

[earlydepthstencil]
void MarkClusters_PS(PS_Input input)
{
    uint zSlice = GetSliceFromDepth(input.positionVS.z);
    uint2 clusterIndexXY = floor(input.position.xy / cClusterSize);
    uint clusterIndex1D = clusterIndexXY.x + cClusterDimensions.x * (clusterIndexXY.y  + zSlice * cClusterDimensions.y);
    uActiveCluster[clusterIndex1D] = 1;
}


