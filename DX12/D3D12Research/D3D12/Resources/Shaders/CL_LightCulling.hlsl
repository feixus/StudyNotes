#include "Common.hlsl"

#define RootSig "CBV(b0, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 3)), " \
                "DescriptorTable(UAV(u0, numDescriptors = 3))"

#define MAX_LIGHTS_PER_TILE 256
#define THREAD_COUNT 512

cbuffer ShaderParameters : register(b0)
{
    float4x4 cView;
    uint cLightCount;
}

StructuredBuffer<Light> Lights : register(t0);
StructuredBuffer<AABB> tClusterAABBs : register(t1);
StructuredBuffer<uint> tActiveClusterIndices : register(t2);

globallycoherent RWStructuredBuffer<uint> uLightIndexCounter : register(u0);
RWStructuredBuffer<uint> uLightIndexList : register(u1);
RWStructuredBuffer<uint2> uOutLightGrid : register(u2);

groupshared AABB GroupAABB;
groupshared uint ClusterIndex;

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
        ClusterIndex = tActiveClusterIndices[input.GroupID.x];
        GroupAABB = tClusterAABBs[ClusterIndex];
    }

    GroupMemoryBarrierWithGroupSync();

    [loop]
    for (uint i = input.GroupIndex; i < cLightCount; i += THREAD_COUNT)
    {
        Light light = Lights[i];
        switch (light.Type)
        {
        case LIGHT_POINT:
        {
            Sphere sphere;
            sphere.Radius = light.Range;
            sphere.Position = mul(float4(light.Position, 1.0f), cView).xyz;
            if (SphereInAABB(sphere, GroupAABB))
            {
                AddLight(i);
            }
        }
        break;
        case LIGHT_SPOT:
        {
#ifdef OPTIMIZED_SPOT_LIGHT_CULLING
            Sphere sphere;
            sphere.Position = GroupAABB.Center;
            sphere.Radius = sqrt(dot(GroupAABB.Extents, GroupAABB.Extents));

            if (ConeInSphere(mul(float4(light.Position, 1.0f), cView).xyz, mul(light.Direction, (float3x3)cView), light.Range, float2(sin(radians(light.SpotLightAngle * 0.5f)), cos(radians(light.SpotLightAngle * 0.5f))), sphere))
            {
                AddLight(i);
            }
#else
            Sphere sphere;
            sphere.Radius = light.Range * 0.5f / pow(cos(radians(light.SpotLightAngle * 0.5f)), 2);
            sphere.Position = mul(float4(light.Position, 1.0f), cView).xyz + mul(light.Direction, (float3x3)cView) * sphere.Radius;
            if (SphereInAABB(sphere, GroupAABB))
            {
                AddLight(i);
            }
#endif
        }
        break;
        case LIGHT_DIRECTIONAL:
        {
            AddLight(i);
        }
        break;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (input.GroupIndex == 0)
    {
        InterlockedAdd(uLightIndexCounter[0], gLightCount, gIndexStartOffset);
        uOutLightGrid[ClusterIndex] = uint2(gIndexStartOffset, gLightCount);
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint j = input.GroupIndex; j < gLightCount; j += THREAD_COUNT)
    {
        uLightIndexList[gIndexStartOffset + j] = gLightList[j];
    }
}
