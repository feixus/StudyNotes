#include "RNG.hlsli"
#include "Common.hlsli"

#define RPP 64

// raytracing output texture, accessed as a UAV
RWTexture2D<float> gOutput : register(u0);

// raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0);

Texture2D tDepth : register(t1);

SamplerState sSceneSampler : register(s0);

cbuffer ShaderParameters : register(b0)
{
    float4x4 cViewInverse;
    float4x4 cProjectionInverse;
    float4 cRandomVectors[RPP];
    float cPower;
    float cRadius;
    int cSamples;
}

struct RayPayload
{
    float hitDistance;
};

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
    payload.hitDistance = RayTCurrent();
}

[shader("miss")]
void Miss(inout RayPayload payload : SV_RayPayload)
{
   payload.hitDistance = 0;
}

[shader("raygeneration")]
void RayGen()
{
    RayPayload payload = (RayPayload)0;

    float2 dimInv = rcp((float)DispatchRaysDimensions().xy);
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint launchIndexId = launchIndex.x + launchIndex.y * DispatchRaysDimensions().x;
    float2 texCoord = (float2)launchIndex * dimInv;

    float depth = tDepth.SampleLevel(sSceneSampler, texCoord, 0).r;
    float3 world = WorldFromDepth(texCoord, depth, mul(cProjectionInverse, cViewInverse));
    float3 normal = NormalFromDepth(tDepth, sSceneSampler, texCoord, dimInv, cProjectionInverse);

    uint state = SeedThread(launchIndexId);
    float3 randomVec = float3(Random01(state), Random01(state), Random01(state)) * 2.0f - 1.0f;

    float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    float3 bitangent = cross(normal, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, normal);

    float accumulateAo = 0;
    for (int i = 0; i < cSamples; i++)
    {
        float3 n = mul(cRandomVectors[Random(state, 0, RPP - 1)].xyz, TBN);
        RayDesc ray;
        ray.Origin = world + 0.001f * n;
        ray.Direction = n;
        ray.TMin = 0.0f;
        ray.TMax = cRadius;

        // trace the ray
        TraceRay(
            // AccelerationStructure
            SceneBVH,
            
            // RayFlags
            RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_FORCE_OPAQUE,
            
            // parameter name: InstanceInclusionMask
            // instance inclusion mask used to mask out some geometry to this ray by and-ing the mask with a geometry mask
            // the 0xFF flag then indicates no geometry will be masked
            0xFF,
        
            // parameter name: RayContributionToHitGroupIndex
            // depending on the type of ray, a given object can have several hit groups attached
            //(ie. what to do when hitting to compute regular shading, and what to do when hitting to compute shadow)
            //these hit groups are specified sequentially in the SBT, so the value below indicates which offset (on 4 bits) to apply to the hit group for this ray
            // in this sample we only have one hit group per object, hence an offset of 0
            0,
        
            // parameter name: MultiplierForGeometryContributionToHitGroupIndex
            // the offsets in the SBT can be computed from the object ID, its instance ID, but also simply by the order the objects have beed pushed in the acceleration structure
            // this allows the application to group shaders in the SBT in the same order as they are added in the AS
            // in which case the value below represents the stride(4 bits representing the number of hit groups) between two consecutive objects.
            0,
        
            // parameter name: MissShaderIndex
            // index of the miss shader to use in case several consecutive miss shaders are present in the SBT.
            // this allows to change the behavior of the program when no geometry have been hit
            // for example one to return a sky color regular rendering, and another returning a full visibility value for shadow rays.
            // this sample has only one miss shader, hence an index 0
            0,
        
            // parameter name: Ray, ray information to trace
            ray,
            
            // parameter name : Payload
            // payload associated to the ray, which will be used to  communicate between the hit/miss shaders and the raygen
            payload
        );

        accumulateAo += payload.hitDistance != 0;
    }

    accumulateAo /= cSamples;
    gOutput[launchIndex] = pow(1 - accumulateAo, cPower);
}