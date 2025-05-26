cbuffer Parameters : register(b0)
{
    float4x4 cWorldView;
    float4x4 cProjection;
    float2 cScreenDimensions;
    float cNearZ;
    float cFarZ;
    uint4 cClusterDimensions;
    float2 cClusterSize;
}

// UAV(register u#) and render target outputs(SV_Target#) share the same register namespace.
// SV_Target0 implicitly uses u0 internally.
RWStructuredBuffer<uint> uUniqueCluster : register(u1);

uint GetSliceFromDepth(float depth)
{
    float aConstant = cClusterDimensions.z / log(cFarZ / cNearZ);
    float bConstant = (cClusterDimensions.z * log(cNearZ)) / log(cFarZ / cNearZ);
    return floor(log(depth) * aConstant - bConstant);
}

struct VS_Input
{
    float3 position : POSITION;
};

struct PS_Input
{
    float4 position : SV_Position;
    float4 positionVS : TEXCOORD0;
};

PS_Input MarkClusters_VS(VS_Input input)
{
    PS_Input output = (PS_Input)0;
    output.positionVS = mul(float4(input.position, 1.0f), cWorldView);
    output.position = mul(output.positionVS, cProjection);
    return output;
}

void MarkClusters_PS(PS_Input input)
{
    uint zSlice = GetSliceFromDepth(input.positionVS.z);
    uint2 clusterIndexXY = floor(input.position.xy / cClusterSize);
    uint clusterIndex1D = clusterIndexXY.x + clusterIndexXY.y * cClusterDimensions.x + zSlice * cClusterDimensions.x * cClusterDimensions.y;

    uUniqueCluster[clusterIndex1D] = 1;
}


