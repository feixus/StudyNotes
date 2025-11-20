#include "Common.hlsli"

#define RootSig "CBV(b0, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 3)), " \
                "DescriptorTable(UAV(u0, numDescriptors = 3))"

#define MAX_LIGHTS_PER_TILE 256
#define THREAD_COUNT 64

cbuffer ShaderParameters : register(b0)
{
    float4x4 cView;
    uint cLightCount;
}

StructuredBuffer<Light> tLights : register(t0);
StructuredBuffer<AABB> tClusterAABBs : register(t1);
StructuredBuffer<uint> tActiveClusterIndices : register(t2);

globallycoherent RWStructuredBuffer<uint> uLightIndexCounter : register(u0);
RWStructuredBuffer<uint> uLightIndexList : register(u1);
RWStructuredBuffer<uint2> uOutLightGrid : register(u2);

groupshared AABB gGroupAABB;
groupshared uint gClusterIndex;

groupshared uint gIndexStartOffset;
groupshared uint gLightCount;
groupshared uint gLightList[MAX_LIGHTS_PER_TILE];

void AddLight(uint lightIndex)
{
    uint index;
    InterlockedAdd(gLightCount, 1, index);
    if (index < MAX_LIGHTS_PER_TILE)
    {
        gLightList[index] = lightIndex;
    }
}

bool ConeInSphere(float3 conePos, float3 coneDir, float coneRange, float2 coneAngleSinCos, Sphere sphere)
{
    float3 v = sphere.Position - conePos;
    float lenSq = dot(v, v);
    float v1Len = dot(v, coneDir);
    float distanceClosestPoint = coneAngleSinCos.y * sqrt(lenSq - v1Len * v1Len) - v1Len * coneAngleSinCos.x;
    bool angleCull = distanceClosestPoint > sphere.Radius;
    bool frontCull = v1Len > sphere.Radius + coneRange;
    bool backCull = v1Len < -sphere.Radius;
    return !(angleCull || frontCull || backCull);
}

struct CS_Input
{
    uint3 GroupID : SV_GroupID;
    uint3 GroupThreadID : SV_GroupThreadID;
    uint3 DispatchThreadID : SV_DispatchThreadID;
    uint GroupIndex : SV_GroupIndex;
};

[RootSignature(RootSig)]
[numthreads(THREAD_COUNT, 1, 1)]
void LightCulling(CS_Input input)
{
    if (input.GroupIndex == 0)
    {
        gLightCount = 0;
        gClusterIndex = tActiveClusterIndices[input.GroupID.x];
        gGroupAABB = tClusterAABBs[gClusterIndex];
    }

    GroupMemoryBarrierWithGroupSync();

    [loop]
    for (uint i = input.GroupIndex; i < cLightCount; i += THREAD_COUNT)
    {
        Light light = tLights[i];
        if (light.IsPoint())
        {
            Sphere sphere;
            sphere.Radius = light.Range;
            sphere.Position = mul(float4(light.Position, 1.0f), cView).xyz;
            if (SphereInAABB(sphere, gGroupAABB))
            {
                AddLight(i);
            }
        }
        else if (light.IsSpot())
        {
            Sphere sphere;
            sphere.Radius = sqrt(dot(gGroupAABB.Extents.xyz, gGroupAABB.Extents.xyz));
            sphere.Position = gGroupAABB.Center.xyz;

            float3 conePosition = mul(float4(light.Position, 1.0f), cView).xyz;
            float3 coneDirection = mul(light.Direction, (float3x3)cView);
            float angle = acos(light.SpotlightAngles.y);
            if (ConeInSphere(conePosition, coneDirection, light.Range, float2(sin(angle), light.SpotlightAngles.y), sphere))
            {
                AddLight(i);
            }
        }
        else
        {
            AddLight(i);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (input.GroupIndex == 0)
    {
        InterlockedAdd(uLightIndexCounter[0], gLightCount, gIndexStartOffset);
        uOutLightGrid[gClusterIndex] = uint2(gIndexStartOffset, gLightCount);
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint j = input.GroupIndex; j < gLightCount; j += THREAD_COUNT)
    {
        uLightIndexList[gIndexStartOffset + j] = gLightList[j];
    }
}
