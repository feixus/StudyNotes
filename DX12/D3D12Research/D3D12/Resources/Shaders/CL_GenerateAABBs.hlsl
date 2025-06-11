#include "Common.hlsl"

#define RootSig "CBV(b0, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL)"

cbuffer Parameters : register(b0)
{
    float4x4 cProjectionInverse;
    float2 cScreenDimensions;
    float2 cClusterSize;
    uint3 cClusterDimensions;
    float cNearZ;
    float cFarZ;
}

// log-distribution of depth slices
float GetDepthFromSlice(uint slice)
{
    return cNearZ * pow(cFarZ / cNearZ, (float)slice / cClusterDimensions.z);
}

float4 ScreenToView(float2 viewSpace)
{
    viewSpace = viewSpace / cScreenDimensions;
    float4 clipSpace = float4(viewSpace.x * 2.0f - 1.0f, (1.0f - viewSpace.y) * 2.0f - 1.0f, 0.0f, 1.0f);
    clipSpace = mul(clipSpace, cProjectionInverse);
    return clipSpace / clipSpace.w;
}

float3 LineFromOriginZIntersection(float3 lineFromOrigin, float depth)
{
    float3 normal = float3(0.0f, 0.0f, 1.0f);
    float t = depth / dot(normal, lineFromOrigin);
    return t * lineFromOrigin;
}

RWStructuredBuffer<AABB> uOutAABBs : register(u0);

struct CS_Input
{
    uint3 GroupID : SV_GroupID; // 3D index of the group in the dispatch
};

[RootSignature(RootSig)]
[numthreads(1, 1, 1)]
void GenerateAABBs(CS_Input input)
{
    uint3 clusterIndex3D = input.GroupID;
    uint clusterIndex1D = input.GroupID.x + input.GroupID.y * cClusterDimensions.x + input.GroupID.z * cClusterDimensions.x * cClusterDimensions.y;

    float2 minPoint_SS = float2(cClusterSize.x * clusterIndex3D.x, cClusterSize.y * clusterIndex3D.y);
    float2 maxPoint_SS = float2(cClusterSize.x * (clusterIndex3D.x + 1), cClusterSize.y * (clusterIndex3D.y + 1));

    float3 minPoint_VS = ScreenToView(minPoint_SS).xyz;
    float3 maxPoint_VS = ScreenToView(maxPoint_SS).xyz;

    float farZ = GetDepthFromSlice(clusterIndex3D.z);
    float nearZ = GetDepthFromSlice(clusterIndex3D.z + 1);

    float3 minPointNear = LineFromOriginZIntersection(minPoint_VS, nearZ);
    float3 maxPointNear = LineFromOriginZIntersection(maxPoint_VS, nearZ);
    float3 minPointFar = LineFromOriginZIntersection(minPoint_VS, farZ);
    float3 maxPointFar = LineFromOriginZIntersection(maxPoint_VS, farZ);

    float3 bbMin = min(min(minPointNear, minPointFar), min(maxPointNear, maxPointFar));
    float3 bbMax = max(max(minPointNear, minPointFar), max(maxPointNear, maxPointFar));

    uOutAABBs[clusterIndex1D].Center = float4((bbMin + bbMax) * 0.5f, 1.0f);
    uOutAABBs[clusterIndex1D].Extents = float4(bbMax - uOutAABBs[clusterIndex1D].Center.xyz, 1.0f);
}

