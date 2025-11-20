#include "RNG.hlsli"
#include "Common.hlsli"
#include "CommonBindings.hlsli"
#incidence "RaytracingCommon.hlsli"

GlobalRootSignature GlobalRootSig = 
{
    "CBV(b0),"
    "DescriptorTable(UAV(u0, numDescriptors=1)),"
    "DescriptorTable(SRV(t0, numDescriptors=1)),"
    GLOBAL_BINDLESS_TABLE
    "staticSampler(s0, filter=FILTER_MIN_MAG_LINEAR_MIP_POINT)"
};

RWTexture2D<float> uOutput : register(u0);
Texture2D tSceneDepth : register(t0);
SamplerState sSceneSampler : register(s0);

struct Data
{
    float4x4 ViewInverse;
    float4x4 ProjectionInverse;
    float4x4 ViewProjectionInverse;
    float Power;
    float Radius;
    int Samples;
    uint TLASIndex;
    uint FrameIndex;
};

ConstantBuffer<Data> cData : register(b0);

struct RayPayload
{
    float hit;
};

// Utility function to get a vector perpendicular to an input vector 
// From Michael M. Stark - https://blog.selfshadow.com/2011/10/17/perp-vectors/
float3 GetPerpendicularVector(float3 u)
{
    float3 a = abs(u);
    uint uyx = sign(a.x - a.y);
    uint uzx = sign(a.x - a.z);
    uint uzy = sign(a.y - a.z);

    uint xm = uyx & uzx;
    uint ym = (1^xm) & uzy;
    uint zm = 1 ^ (xm & ym);

    float3 v = cross(u, float3(xm, ym, zm));
    return v;
}

// get a cosine-weighted random vector centered around a specified normal direction.
float3 GetCosHemisphereSample(inout uint randSeed, float3 hitNorm)
{
    // get 2 random numbers to generate the sample
    float2 randVal = float2(Random01(randSeed), Random01(randSeed));

    // cosine weighted hemisphere sampling
    float3 bitangent = GetPerpendicularVector(hitNorm);
    float3 tangent = cross(bitangent, hitNorm);
    float r = sqrt(randVal.x);
    float phi = 2.0f * 3.14159265f * randVal.y;

    // get our cosine-weighted hemisphere lobe sample direction
    return tangent * (r * cos(phi)) + bitangent * (r * sin(phi)) + hitNorm * sqrt(1.0f - randVal.x);
}

[shader("miss")]
void Miss(inout RayPayload payload : SV_RayPayload)
{
   payload.hit = 0.0f;
}

[shader("raygeneration")]
void RayGen()
{
    uint2 launchDim = DispatchRaysDimensions().xy;
    float2 dimInv = rcp((float2)launchDim);
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint launchIndexId = launchIndex.x + launchIndex.y * launchDim.x;
    float2 texCoord = (float2)launchIndex * dimInv;

    float depth = tSceneDepth.SampleLevel(sSceneSampler, texCoord, 0).r;
    float3 world = WorldFromDepth(texCoord, depth, cData.ViewProjectionInverse);
    float3 normal = NormalFromDepth(tSceneDepth, sSceneSampler, texCoord, dimInv, cData.ProjectionInverse);
    normal = mul(normal, (float3x3)cData.ViewInverse);

    uint randSeed = SeedThread(launchIndexId + cData.FrameIndex * launchDim.x * launchDim.y);
    float accumulateAo = 0;
    for (int i = 0; i < cData.Samples; i++)
    {
        RayPayload payload = {1.0f};
        float3 randomDirection = GetCosHemisphereSample(randSeed, normal);

        RayDesc ray;
        ray.Origin = world;
        ray.Direction = randomDirection;
        ray.TMin = RAY_BIAS;
        ray.TMax = cData.Radius;

        // trace the ray
        TraceRay(
            // AccelerationStructure
            tTLASTable[cData.TLASIndex],
            
            // RayFlags
            RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
            
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

        accumulateAo += payload.hit;
    }

    accumulateAo /= cData.Samples;
    uOutput[launchIndex] = pow(1 - accumulateAo, cData.Power);
}
