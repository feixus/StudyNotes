#include "Common.hlsl"
#include "Constants.hlsl"

cbuffer ShaderParameters : register(b0)
{
	float4x4 cView;
	uint4 cNumThreadGroups;
    float4x4 cProjectionInverse;
}

cbuffer LightData : register(b1)
{
    Light cLights[LIGHT_COUNT];
}

StructuredBuffer<Frustum> tInFrustums : register(t0);
Texture2D tDepthTexture : register(t1);
RWStructuredBuffer<uint> uLightIndexCounter : register(u0);
RWStructuredBuffer<uint> uLightIndexList : register(u1);
RWTexture2D<uint2> uOutLightGrid : register(u2);

groupshared uint MinDepth;
groupshared uint MaxDepth;
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
    float fDepth = tDepthTexture.Load(int3(texCoord, 0)).r;
    
    // convert to uint because cant use interlocked functions on float
    uint depth = asuint(fDepth);
    
    // initialize the groupshared data only on the first thread of the group
    if (input.GroupIndex == 0)
    {
        MinDepth = 0xffffffff;
        MaxDepth = 0;
        LightCount = 0;
        GroupFrustum = tInFrustums[input.GroupId.x + input.GroupId.y * cNumThreadGroups.x];
    }
    
    // wait for thread 0 to finish with initializing the groupshared data
    GroupMemoryBarrierWithGroupSync();

    // find the min and max depth values in the threadgroup
    InterlockedMin(MinDepth, depth);
    InterlockedMax(MaxDepth, depth);
    
    // wait for all the threads to finish
    GroupMemoryBarrierWithGroupSync();
    
    float fMinDepth = asfloat(MinDepth);
    float fMaxDepth = asfloat(MaxDepth);
    
    // convert depth values to view space
    float minDepthVS = ClipToView(float4(0, 0, fMinDepth, 1), cProjectionInverse).z;
    float maxDepthVS = ClipToView(float4(0, 0, fMaxDepth, 1), cProjectionInverse).z;
    float nearClipVS = ClipToView(float4(0, 0, 0, 1), cProjectionInverse).z;

    // clipping plane for minimum depth value
    Plane minPlane;
    minPlane.Normal = float3(0.0f, 0.0f, 1.0f);
    minPlane.DistanceToOrigin = minDepthVS;
    
    // perform the light culling
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
                    // need the light position in view space
                    if (SphereInFrustum(sphere, GroupFrustum, nearClipVS, maxDepthVS))
                    {
                        if (!SphereBehindPlane(sphere, minPlane))
                        {
                            AddLight(i);
                        }
                    }
                    break;
                default:
                    AddLight(i);
                    break;
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // populate the light grid only on the first thread in the group
    if (input.GroupIndex == 0)
    {
        InterlockedAdd(uLightIndexCounter[0], LightCount, LightIndexStartOffset);
        uOutLightGrid[input.GroupId.xy] = uint2(LightIndexStartOffset, LightCount);
    }

    GroupMemoryBarrierWithGroupSync();

    // distribute populating the light index light amonst threads in the thread group
    for (uint j = input.GroupIndex; j < LightCount; j += BLOCK_SIZE * BLOCK_SIZE)
    {
        uLightIndexList[LightIndexStartOffset + j] = LightList[j];
    }
}
