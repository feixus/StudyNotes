#include "Common.hlsli"
#include "Constants.hlsli"

#define RootSig "CBV(b0, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u0, numDescriptors = 5), visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 2), visibility = SHADER_VISIBILITY_ALL)"
                

#define MAX_LIGHTS_PER_TILE 256
#define BLOCK_SIZE 16

cbuffer ShaderParameters : register(b0)
{
	float4x4 cView;
    float4x4 cProjectionInverse;
	uint4 cNumThreadGroups;
    float2 cScreenDimensionsInv;
    uint cLightCount;
}

StructuredBuffer<Light> tLights : register(t1);

Texture2D tDepthTexture : register(t0);
globallycoherent RWStructuredBuffer<uint> uLightIndexCounter : register(u0);
RWStructuredBuffer<uint> uOpaqueLightIndexList : register(u1);
RWTexture2D<uint2> uOpaqueOutLightGrid : register(u2);

RWStructuredBuffer<uint> uTransparentLightIndexList : register(u3);
RWTexture2D<uint2> uTransparentOutLightGrid : register(u4);

groupshared uint MinDepth;
groupshared uint MaxDepth;
groupshared Frustum GroupFrustum;
groupshared AABB GroupAABB;

groupshared uint OpaqueLightCount;
groupshared uint OpaqueLightIndexStartOffset;
groupshared uint OpaqueLightList[MAX_LIGHTS_PER_TILE];

groupshared uint TransparentLightCount;
groupshared uint TransparentLightIndexStartOffset;
groupshared uint TransparentLightList[MAX_LIGHTS_PER_TILE];

#if SPLITZ_CULLING
groupshared uint DepthMask;
#endif

struct CS_INPUT
{
	uint3 GroupId : SV_GroupID;						// 3D index of the group in the dispatch
	uint3 GroupThreadId : SV_GroupThreadID;			// local 3D index of a thread
	uint3 DispatchThreadId : SV_DispatchThreadID;	// global 3D index of a thread
	uint GroupIndex : SV_GroupIndex;				// index of the thread in the group
};

void AddLightForOpaque(uint lightIndex)
{
    uint index;
    InterlockedAdd(OpaqueLightCount, 1, index);
    if (index < 1024)
    {
        OpaqueLightList[index] = lightIndex;
    }
}

void AddLightForTransparent(uint lightIndex)
{
    uint index;
    InterlockedAdd(TransparentLightCount, 1, index);
    if (index < 1024)
    {
        TransparentLightList[index] = lightIndex;
    }
}

uint CreateLightMask(float depthRangeMin, float depthRange, Sphere sphere)
{
    float fMin = sphere.Position.z - sphere.Radius;
    float fMax = sphere.Position.z + sphere.Radius;
    uint maskIndexStart = max(0, min(31, floor((fMin - depthRangeMin) * depthRange)));
    uint maskIndexEnd = max(0, min(31, floor((fMax - depthRangeMin) * depthRange)));

    // set all 32 bits is 1
    uint mask = 0xFFFFFFFF;
    // set all valid bits is 1 from indexStart to indexEnd, other is set 0
    mask >>= 31 - (maskIndexEnd - maskIndexStart);
    // correct the origin position for valid bits
    mask <<= maskIndexStart;
    return mask;
}

[RootSignature(RootSig)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(CS_INPUT input)
{
    int2 texCoord = input.DispatchThreadId.xy;
    float fDepth = tDepthTexture[texCoord].r;
    
    // convert to uint because cant use interlocked functions on float
    uint depth = asuint(fDepth);
    
    // initialize the groupshared data only on the first thread of the group
    if (input.GroupIndex == 0)
    {
        MinDepth = 0xffffffff;
        MaxDepth = 0;
        OpaqueLightCount = 0;
        TransparentLightCount = 0;
#if SPLITZ_CULLING
        DepthMask = 0;
#endif
    }
    
    // wait for thread 0 to finish with initializing the groupshared data
    GroupMemoryBarrierWithGroupSync();

    // find the min and max depth values in the threadgroup
    InterlockedMin(MinDepth, depth);
    InterlockedMax(MaxDepth, depth);
    
    // wait for all the threads to finish
    GroupMemoryBarrierWithGroupSync();
    
    float fMinDepth = asfloat(MaxDepth);
    float fMaxDepth = asfloat(MinDepth);

    // generate the frustum and AABB for the threadgroup
    if (input.GroupIndex == 0)
    {
        float3 viewSpace[8];
        viewSpace[0] = ScreenToView(float4(input.GroupId.xy * BLOCK_SIZE, fMinDepth, 1.0), cScreenDimensionsInv, cProjectionInverse).xyz;
        viewSpace[1] = ScreenToView(float4(float2(input.GroupId.x + 1, input.GroupId.y) * BLOCK_SIZE, fMinDepth, 1.0), cScreenDimensionsInv, cProjectionInverse).xyz;
        viewSpace[2] = ScreenToView(float4(float2(input.GroupId.x, input.GroupId.y + 1) * BLOCK_SIZE, fMinDepth, 1.0), cScreenDimensionsInv, cProjectionInverse).xyz;
        viewSpace[3] = ScreenToView(float4(float2(input.GroupId.x + 1, input.GroupId.y + 1) * BLOCK_SIZE, fMinDepth, 1.0), cScreenDimensionsInv, cProjectionInverse).xyz;
        viewSpace[4] = ScreenToView(float4(input.GroupId.xy * BLOCK_SIZE, fMaxDepth, 1.0), cScreenDimensionsInv, cProjectionInverse).xyz;
        viewSpace[5] = ScreenToView(float4(float2(input.GroupId.x + 1, input.GroupId.y) * BLOCK_SIZE, fMaxDepth, 1.0), cScreenDimensionsInv, cProjectionInverse).xyz;
        viewSpace[6] = ScreenToView(float4(float2(input.GroupId.x, input.GroupId.y + 1) * BLOCK_SIZE, fMaxDepth, 1.0), cScreenDimensionsInv, cProjectionInverse).xyz;
        viewSpace[7] = ScreenToView(float4(float2(input.GroupId.x + 1, input.GroupId.y + 1) * BLOCK_SIZE, fMaxDepth, 1.0), cScreenDimensionsInv, cProjectionInverse).xyz;

        GroupFrustum.Planes[0] = CalculatePlane(float3(0, 0, 0), viewSpace[6], viewSpace[4]);
        GroupFrustum.Planes[1] = CalculatePlane(float3(0, 0, 0), viewSpace[5], viewSpace[7]);
        GroupFrustum.Planes[2] = CalculatePlane(float3(0, 0, 0), viewSpace[4], viewSpace[5]);
        GroupFrustum.Planes[3] = CalculatePlane(float3(0, 0, 0), viewSpace[7], viewSpace[6]);

        float3 minAABB = min(viewSpace[0], min(viewSpace[1], min(viewSpace[2], min(viewSpace[3], min(viewSpace[4], min(viewSpace[5], min(viewSpace[6], viewSpace[7])))))));
        float3 maxAABB = max(viewSpace[0], max(viewSpace[1], max(viewSpace[2], max(viewSpace[3], max(viewSpace[4], max(viewSpace[5], max(viewSpace[6], viewSpace[7])))))));
        AABBFromMinMax(GroupAABB, minAABB, maxAABB);
    }
    
    // convert depth values to view space
    float minDepthVS = ScreenToView(float4(0, 0, fMinDepth, 1), cScreenDimensionsInv, cProjectionInverse).z;
    float maxDepthVS = ScreenToView(float4(0, 0, fMaxDepth, 1), cScreenDimensionsInv, cProjectionInverse).z;
    float nearClipVS = ScreenToView(float4(0, 0, 1, 1), cScreenDimensionsInv, cProjectionInverse).z;

#if SPLITZ_CULLING
    // save all the depth in a bitmask on the thread group
    float depthVS = ScreenToView(float4(0, 0, fDepth, 1), cScreenDimensionsInv, cProjectionInverse).z;
    float depthRange = 31.0f / (maxDepthVS - minDepthVS);
    uint cellIndex = max(0, min(31, floor((depthVS - minDepthVS) * depthRange)));
    InterlockedOr(DepthMask, 1u << cellIndex);
#endif

    // clipping plane for minimum depth value
    Plane minPlane;
    minPlane.Normal = float3(0.0f, 0.0f, 1.0f);
    minPlane.DistanceToOrigin = minDepthVS;
    
    GroupMemoryBarrierWithGroupSync();

    // perform the light culling
    for (uint i = input.GroupIndex; i < cLightCount; i += BLOCK_SIZE * BLOCK_SIZE)
    {
        Light light = tLights[i];
        switch (light.Type)
        {
        case LIGHT_POINT:
        {
            Sphere sphere;
            sphere.Radius = light.Range;
            sphere.Position = mul(float4(light.Position, 1.0f), cView).xyz;

            if (SphereInFrustum(sphere, GroupFrustum, nearClipVS, maxDepthVS))
            {
                AddLightForTransparent(i);

                if (SphereInAABB(sphere, GroupAABB))
                {
#if SPLITZ_CULLING
                    if (CreateLightMask(minDepthVS, depthRange, sphere) & DepthMask)
#endif
                    {
                        AddLightForOpaque(i);
                    }
                }
            }
        }
        break;
        case LIGHT_SPOT:
        {
            Sphere sphere;
            sphere.Radius = light.Range * 0.5f / pow(light.SpotlightAngles.y, 2);
            sphere.Position = mul(float4(light.Position, 1.0f), cView).xyz + mul(light.Direction, (float3x3)cView) * sphere.Radius;

            if (SphereInFrustum(sphere, GroupFrustum, nearClipVS, maxDepthVS))
            {
                AddLightForTransparent(i);

                if (SphereInAABB(sphere, GroupAABB))
                {
#if SPLITZ_CULLING
                    if (CreateLightMask(minDepthVS, depthRange, sphere) & DepthMask)
#endif
                    {
                        AddLightForOpaque(i);
                    }
                }
            }
        }
        break;
        case LIGHT_DIRECTIONAL:
        {
            AddLightForTransparent(i);
            AddLightForOpaque(i);
        }
        break;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // populate the light grid only on the first thread in the group
    if (input.GroupIndex == 0)
    {
        InterlockedAdd(uLightIndexCounter[0], OpaqueLightCount, OpaqueLightIndexStartOffset);
        uOpaqueOutLightGrid[input.GroupId.xy] = uint2(OpaqueLightIndexStartOffset, OpaqueLightCount);

        InterlockedAdd(uLightIndexCounter[1], TransparentLightCount, TransparentLightIndexStartOffset);
        uTransparentOutLightGrid[input.GroupId.xy] = uint2(TransparentLightIndexStartOffset, TransparentLightCount);
    }

    GroupMemoryBarrierWithGroupSync();

    // distribute populating the light index light amonst threads in the thread group
    for (uint j = input.GroupIndex; j < OpaqueLightCount; j += BLOCK_SIZE * BLOCK_SIZE)
    {
        uOpaqueLightIndexList[OpaqueLightIndexStartOffset + j] = OpaqueLightList[j];
    }

    for (uint k = input.GroupIndex; k < TransparentLightCount; k += BLOCK_SIZE * BLOCK_SIZE)
    {
        uTransparentLightIndexList[TransparentLightIndexStartOffset + k] = TransparentLightList[k];
    }
}
