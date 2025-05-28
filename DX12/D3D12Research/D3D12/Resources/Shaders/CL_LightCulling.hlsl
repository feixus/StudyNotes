#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

struct AABB
{
    float4 Center;
    float4 Extents;
};

struct Sphere
{
    float3 Position;
    float Radius;
};

struct Light
{
    float3 Position;
    int Enabled;
    float3 Direction;
    int Type;
    float4 Color;
    float Range;
    float SpotLightAngle;
    float Attenuation;
    int ShadowIndex;
};

#define MAX_LIGHTS_PER_TILE 256

cbuffer ShaderParameters : register(b0)
{
    float4x4 cView;
}

StructuredBuffer<Light> Lights : register(t0);
StructuredBuffer<AABB> tClusterAABBs : register(t1);
StructuredBuffer<uint> tActiveClusterIndices : register(t2);

globallycoherent RWStructuredBuffer<uint> uLightIndexCounter : register(u0);
RWStructuredBuffer<uint> uLightIndexList : register(u1);
RWStructuredBuffer<uint2> uOutLightGrid : register(u2);

groupshared AABB GroupAABB;
groupshared uint ClusterIndex;

groupshared uint IndexStartOffset;
groupshared uint LightCount;
groupshared uint LightList[MAX_LIGHTS_PER_TILE];

void AddLight(uint lightIndex)
{
    uint index;
    InterlockedAdd(LightCount, 1, index);
    if (index < MAX_LIGHTS_PER_TILE)
    {
        LightList[index] = lightIndex;
    }
}

bool SphereInAABB(Sphere sphere, AABB aabb)
{
    float3 dist = max(0, abs(sphere.Position - aabb.Center.xyz) - aabb.Extents.xyz);
    return dot(dist, dist) <= sphere.Radius * sphere.Radius;
}

struct CS_Input
{
    uint3 GroupID : SV_GroupID;
    uint3 GroupThreadID : SV_GroupThreadID;
    uint3 DispatchThreadID : SV_DispatchThreadID;
    uint GroupIndex : SV_GroupIndex;
};

#define THREAD_COUNT 512

[numthreads(THREAD_COUNT, 1, 1)]
void LightCulling(CS_Input input)
{
    if (input.GroupIndex == 0)
    {
        LightCount = 0;
        ClusterIndex = tActiveClusterIndices[input.GroupID.x];
        GroupAABB = tClusterAABBs[ClusterIndex];
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint i = input.GroupIndex; i < LIGHT_COUNT; i += THREAD_COUNT)
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
            Sphere sphere;
            sphere.Radius = light.Range * 0.5f / pow(cos(radians(light.SpotLightAngle * 0.5f)), 2);
            sphere.Position = mul(float4(light.Position, 1.0f), cView).xyz + mul(light.Direction, (float3x3)cView) * sphere.Radius;
            if (SphereInAABB(sphere, GroupAABB))
            {
                AddLight(i);
            }
            
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
        InterlockedAdd(uLightIndexCounter[0], LightCount, IndexStartOffset);
        uOutLightGrid[ClusterIndex] = uint2(IndexStartOffset, LightCount);
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint j = input.GroupIndex; j < LightCount; j += THREAD_COUNT)
    {
        uLightIndexList[IndexStartOffset + j] = LightList[j];
    }
}
