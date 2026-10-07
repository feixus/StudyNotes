#include "CommonBindings.hlsli"
#include "Constants.hlsli"

#define RootSig ROOT_SIG("CBV(b0), " \
                "DescriptorTable(UAV(u0, numDescriptors = 5)), " \
                "DescriptorTable(SRV(t0, numDescriptors = 2))")
                

#define MAX_LIGHTS_PER_TILE 256
#define BLOCK_SIZE 16
#define SPLITZ_CULLING 1

struct ShaderParameters
{
	float4x4 View;
    float4x4 ProjectionInverse;
	uint4 NumThreadGroups;
    float2 ScreenDimensionsInv;
    uint LightCount;
};

ConstantBuffer<ShaderParameters> cShaderParameters : register(b0);

Texture2D tDepthTexture : register(t0);
StructuredBuffer<Light> tSceneLights : register(t1);

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
void CSMain(uint3 groupId : SV_GroupID, uint3 threadID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    int2 texCoord = threadID.xy;
    float fDepth = tDepthTexture[texCoord].r;
    
    // convert to uint because cant use interlocked functions on float
    uint depth = asuint(fDepth);
    
    // initialize the groupshared data only on the first thread of the group
    if (groupIndex == 0)
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
    if (groupIndex == 0)
    {
        float3 viewSpace[8];
        viewSpace[0] = ScreenToView(float4(groupId.xy * BLOCK_SIZE, fMinDepth, 1.0), cShaderParameters.ScreenDimensionsInv, cShaderParameters.ProjectionInverse);
        viewSpace[1] = ScreenToView(float4(float2(groupId.x + 1, groupId.y) * BLOCK_SIZE, fMinDepth, 1.0), cShaderParameters.ScreenDimensionsInv, cShaderParameters.ProjectionInverse);
        viewSpace[2] = ScreenToView(float4(float2(groupId.x, groupId.y + 1) * BLOCK_SIZE, fMinDepth, 1.0), cShaderParameters.ScreenDimensionsInv, cShaderParameters.ProjectionInverse);
        viewSpace[3] = ScreenToView(float4(float2(groupId.x + 1, groupId.y + 1) * BLOCK_SIZE, fMinDepth, 1.0), cShaderParameters.ScreenDimensionsInv, cShaderParameters.ProjectionInverse);
        viewSpace[4] = ScreenToView(float4(groupId.xy * BLOCK_SIZE, fMaxDepth, 1.0), cShaderParameters.ScreenDimensionsInv, cShaderParameters.ProjectionInverse);
        viewSpace[5] = ScreenToView(float4(float2(groupId.x + 1, groupId.y) * BLOCK_SIZE, fMaxDepth, 1.0), cShaderParameters.ScreenDimensionsInv, cShaderParameters.ProjectionInverse);
        viewSpace[6] = ScreenToView(float4(float2(groupId.x, groupId.y + 1) * BLOCK_SIZE, fMaxDepth, 1.0), cShaderParameters.ScreenDimensionsInv, cShaderParameters.ProjectionInverse);
        viewSpace[7] = ScreenToView(float4(float2(groupId.x + 1, groupId.y + 1) * BLOCK_SIZE, fMaxDepth, 1.0), cShaderParameters.ScreenDimensionsInv, cShaderParameters.ProjectionInverse);

        GroupFrustum.Planes[0] = CalculatePlane(float3(0, 0, 0), viewSpace[6], viewSpace[4]);
        GroupFrustum.Planes[1] = CalculatePlane(float3(0, 0, 0), viewSpace[5], viewSpace[7]);
        GroupFrustum.Planes[2] = CalculatePlane(float3(0, 0, 0), viewSpace[4], viewSpace[5]);
        GroupFrustum.Planes[3] = CalculatePlane(float3(0, 0, 0), viewSpace[7], viewSpace[6]);

        float3 minAABB = min(viewSpace[0], min(viewSpace[1], min(viewSpace[2], min(viewSpace[3], min(viewSpace[4], min(viewSpace[5], min(viewSpace[6], viewSpace[7])))))));
        float3 maxAABB = max(viewSpace[0], max(viewSpace[1], max(viewSpace[2], max(viewSpace[3], max(viewSpace[4], max(viewSpace[5], max(viewSpace[6], viewSpace[7])))))));
        AABBFromMinMax(GroupAABB, minAABB, maxAABB);
    }
    
    // convert depth values to view space
    float minDepthVS = ScreenToView(float4(0, 0, fMinDepth, 1), cShaderParameters.ScreenDimensionsInv, cShaderParameters.ProjectionInverse).z;
    float maxDepthVS = ScreenToView(float4(0, 0, fMaxDepth, 1), cShaderParameters.ScreenDimensionsInv, cShaderParameters.ProjectionInverse).z;
    float nearClipVS = ScreenToView(float4(0, 0, 1, 1), cShaderParameters.ScreenDimensionsInv, cShaderParameters.ProjectionInverse).z;

#if SPLITZ_CULLING
    // save all the depth in a bitmask on the thread group
    float depthVS = ScreenToView(float4(0, 0, fDepth, 1), cShaderParameters.ScreenDimensionsInv, cShaderParameters.ProjectionInverse).z;
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
    for (uint i = groupIndex; i < cShaderParameters.LightCount; i += BLOCK_SIZE * BLOCK_SIZE)
    {
        Light light = tSceneLights[i];
        if (light.IsPoint())
        {
            Sphere sphere;
            sphere.Radius = light.Range;
            sphere.Position = mul(float4(light.Position, 1.0f), cShaderParameters.View).xyz;

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
        else if (light.IsSpot())
        {
            Sphere sphere;
            sphere.Radius = light.Range * 0.5f / pow(light.SpotlightAngles.y, 2);
            sphere.Position = mul(float4(light.Position, 1.0f), cShaderParameters.View).xyz + mul(light.Direction, (float3x3)cShaderParameters.View) * sphere.Radius;

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
        else
        {
            AddLightForTransparent(i);
            AddLightForOpaque(i);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // populate the light grid only on the first thread in the group
    if (groupIndex == 0)
    {
        InterlockedAdd(uLightIndexCounter[0], OpaqueLightCount, OpaqueLightIndexStartOffset);
        uOpaqueOutLightGrid[groupId.xy] = uint2(OpaqueLightIndexStartOffset, OpaqueLightCount);

        InterlockedAdd(uLightIndexCounter[1], TransparentLightCount, TransparentLightIndexStartOffset);
        uTransparentOutLightGrid[groupId.xy] = uint2(TransparentLightIndexStartOffset, TransparentLightCount);
    }

    GroupMemoryBarrierWithGroupSync();

    // distribute populating the light index light amonst threads in the thread group
    for (uint j = groupIndex; j < OpaqueLightCount; j += BLOCK_SIZE * BLOCK_SIZE)
    {
        uOpaqueLightIndexList[OpaqueLightIndexStartOffset + j] = OpaqueLightList[j];
    }

    for (uint k = groupIndex; k < TransparentLightCount; k += BLOCK_SIZE * BLOCK_SIZE)
    {
        uTransparentLightIndexList[TransparentLightIndexStartOffset + k] = TransparentLightList[k];
    }
}
