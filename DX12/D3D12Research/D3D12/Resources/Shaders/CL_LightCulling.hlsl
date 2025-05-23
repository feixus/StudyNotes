#define BLOCK_SIZE 16
#define LIGHT_COUNT 512

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

struct AABB
{
    float4 Min;
    float4 MAX;
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
    float4x4 cViews;
    uint4 cNumThreadGroups;
}

StructuredBuffer<Light> Lights : register(t1);
StructuredBuffer<AABB> tClusterAABBs : register(t2);
StructuredBuffer<uint> tActiveClusterIndices : register(t3);

globallycoherent RWStructuredBuffer<uint> uLightIndexCounter : register(u0);
RWStructuredBuffer<uint> uLightIndexList : register(u1);
RWTexture1D<uint2> uOutLightGrid : register(u2);

groupshared AABB GroupAABB;
groupshared uint LightCount;
groupshared uint LightIndexStartOffset;
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

float SquaredDistPointAABB(float3 p, AABB aabb)
{
    float sqDist = 0.0f;
    for (int i = 0; i < 3; i++)
    {
        float v = p[i];
        if (v < aabb.Min[i])
        {
            sqDist += (aabb.Min[i] - v) * (aabb.Min[i] - v);
        }
        if (v > aabb.Max[i])
        {
            sqDist += (v - aabb.Max[i]) * (v - aabb.Max[i]);
        }
    }
    return sqDist;
}

bool SphereInAABB(Sphere sphere, AABB aabb)
{
    float sqDist = SquaredDistPointAABB(sphere.Position, aabb);
    return sqDist <= sphere.Radius * sphere.Radius;
}

struct CS_Input
{
    uint3 GroupID : SV_GroupID;
    uint3 GroupThreadID : SV_GroupThreadID;
    uint3 DispatchThreadID : SV_DispatchThreadID;
    uint GroupIndex : SV_GroupIndex;
};

[numthreads(BLOCK_SIZE, 1, 1)]
void CS(CS_Input input)
{
    uint clusterIndex = tActiveClusterIndices[input.DispatchThreadID.x];
    
    if (input.GroupIndex == 0)
    {
        LightCount = 0;
        GroupAABB = tClusterAABBs[clusterIndex];
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint i = input.GroupIndex; i < LIGHT_COUNT; i += BLOCK_SIZE * BLOCK_SIZE)
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
        InterlockedAdd(uLightIndexCounter[0], LightCount, LightIndexStartOffset);
        uOutLightGrid[input.GroupID.x] = uint2(LightIndexStartOffset, LightCount);
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint j = input.GroupIndex; j < LightCount; j += BLOCK_SIZE * BLOCK_SIZE)
    {
        uLightIndexList[LightIndexStartOffset + j] = LightList[j];
    }
}
