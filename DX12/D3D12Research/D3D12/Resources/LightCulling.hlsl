#include "Common.hlsl"
#include "Constants.hlsl"

cbuffer ShaderParameters : register(b0)
{
	float4x4 cView;
	uint4 cNumThreadGroups;
}

cbuffer LightData : register(b1)
{
    Light cLights[LIGHT_COUNT];
}

StructuredBuffer<Frustum> tInFrustums : register(t0);
RWStructuredBuffer<uint> uLightIndexCounter : register(u0);
RWStructuredBuffer<uint> uLightIndexList : register(u1);
RWTexture2D<uint2> uOutLightGrid : register(u2);

groupshared Frustum GroupFrustum;
groupshared uint LightCount;
groupshared uint LightIndexStartOffset;
groupshared uint LightList[1024];

struct CS_INPUT
{
	uint3 GroupId : SV_GroupID;						// 3D index of the group in the dispatch
	uint3 GroupThreadId : SV_GroupThreadID;			// local 3D index of a thread
	uint3 DispatchThreadId : SV_DispatchThreadID;	// global 3D index of a thread
	uint GroupIndex : SV_GroupIndex;				// index of the thread in the group
};

void AddLight(uint lightIndex)
{
    uint index;
    InterlockedAdd(LightCount, 1, index);
    LightList[index] = lightIndex;
}

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(CS_INPUT input)
{
    int2 texCoord = input.DispatchThreadId.xy;
    if (input.GroupIndex == 0)
    {
        LightCount = 0;
        GroupFrustum = tInFrustums[input.GroupId.x + input.GroupId.y * cNumThreadGroups.x];
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint i = input.GroupIndex; i < LIGHT_COUNT; i += BLOCK_SIZE * BLOCK_SIZE)
    {
        if (cLights[i].Enabled)
        {
            switch (cLights[i].Type)
            {
                case 1:
                    Sphere sphere;
                    sphere.Radius = cLights[i].Range;
                    sphere.Position = mul(float4(cLights[i].Position.xyz, 1), cView).xyz;
                    if (SphereInFrustum(sphere, GroupFrustum, 0, 100000000))
                    {
                        AddLight(i);
                    }
                break;
                default:
                    AddLight(i);
                break;
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (input.GroupIndex == 0)
    {
        InterlockedAdd(uLightIndexCounter[0], LightCount, LightIndexStartOffset);
        uOutLightGrid[input.GroupId.xy] = uint2(LightIndexStartOffset, LightCount);
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint j = input.GroupIndex; j < LightCount; j += BLOCK_SIZE * BLOCK_SIZE)
    {
        uLightIndexList[LightIndexStartOffset + j] = LightList[j];
    }
}
