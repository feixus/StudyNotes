#include "Common.hlsli"
#include "ShadingModels.hlsli"

RWTexture2D<float4> gOutput : register(u0);

RaytracingAccelerationStructure SceneBVH : register(t0);
Texture2D tDepth : register(t1);
StructuredBuffer<Light> tLights : register(t2);
ByteAddressBuffer tGeometryData : register(t3);
Texture2D tMaterialTextures[] : register(t200);

SamplerState sSceneSampler : register(s0);

struct Vertex
{
    float3 position;
    float2 texCoord;
    float3 normal;
    float3 tangent;
    float3 bitangent;
};

cbuffer HitData : register(b1)
{
    int DiffuseIndex;
    int NormalIndex;
    int RoughnessIndex;
    int MetallicIndex;
    uint VertexBufferOffset;
    uint IndexBufferOffset;
}

cbuffer ShaderParameters : register(b0)
{
    float4x4 cViewInverse;
    float4x4 cViewProjectionInverse;
}

struct RayPayload
{
    float3 output;
};

struct ShadowRayPayload
{
    uint hit;
};

float3 TangentSpaceNormalMapping(float3 sampledNormal, float3x3 TBN, float2 tex, bool invertY)
{
    sampledNormal.xy = sampledNormal.xy * 2.0f - 1.0f;

#ifdef NORMAL_BC5
    sampledNormal.z = sqrt(saturate(1.0f - dot(sampledNormal.xy, sampledNormal.xy)));
#endif

    if (invertY)
    {
        sampledNormal.y = -sampledNormal.y;
    }

    sampledNormal = normalize(sampledNormal);
    return mul(sampledNormal, TBN);
}

float3 ApplyAmbientLight(float3 diffuse, float ao, float3 lightColor)
{
    return ao * diffuse * lightColor;
}

// Angle >= Umbra -> 0
// Angle < Penumbra -> 1
//Gradient between Umbra and Penumbra
float DirectionAttenuation(float3 L, float3 direction, float cosUmbra, float cosPrenumbra)
{
    float cosAngle = dot(-normalize(L), direction);
    float falloff = saturate((cosAngle - cosUmbra) / (cosPrenumbra - cosUmbra));
    return falloff * falloff;
}

//Distance between rays is proportional to distance squared
//Extra windowing function to make light radius finite
//https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
float RadialAttenuation(float3 L, float range)
{
    float distSq = dot(L, L);
    float distanceAttenuatio = 1 / (distSq + 1);
    float windowing = Square(saturate(1 - Square(distSq * Square(rcp(range)))));
    return distanceAttenuatio * windowing;
}

float GetAttenuation(Light light, float3 wPos)
{
    float attenuation = 1.0f;

    if (light.Type >= LIGHT_POINT)
    {
        float3 L = light.Position - wPos;
        attenuation *= RadialAttenuation(L, light.Range);
        if (light.Type >= LIGHT_SPOT)
        {
            attenuation *= DirectionAttenuation(L, light.Direction, light.SpotlightAngles.y, light.SpotlightAngles.x);
        }
    }
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
    float3 b = float3(1.0f - attrib.barycentrics.x - attrib.barycentrics.y, attrib.barycentrics.x, attrib.barycentrics.y);
    uint3 indices = tGeometryData.Load3(IndexBufferOffset + PrimitiveIndex() * sizeof(uint3));
    Vertex v0 = tGeometryData.Load<Vertex>(VertexBufferOffset + indices.x * sizeof(Vertex));
    Vertex v1 = tGeometryData.Load<Vertex>(VertexBufferOffset + indices.y * sizeof(Vertex));
    Vertex v2 = tGeometryData.Load<Vertex>(VertexBufferOffset + indices.z * sizeof(Vertex));
    float2 texCoord = v0.texCoord * b.x + v1.texCoord * b.y + v2.texCoord * b.z;

    float3 N = v0.normal * b.x + v1.normal * b.y + v2.normal * b.z;
    float3 T = vo.tangent * b.x + v1.tangent * b.y + v2.tangent * b.z;
    float3 B = v0.bitangent * b.x + v1.bitangent * b.y + v2.bitangent * b.z;
    float3x3 TBN = float3x3(T, B, N);

    float wPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 V = normalize(wPos - cViewInverse[3].xyz);

    float3 diffuse = tMaterialTextures[DiffuseIndex].SampleLevel(sSceneSampler, texCoord, 0).rgb;
    float3 sampledNormal = tMaterialTextures[NormalIndex].SampleLevel(sSceneSampler, texCoord, 0).rgb;
    N = TangentSpaceNormalMapping(sampledNormal, TBN, texCoord, false);

    float roughness = 0.5f;
    float3 specularColor = ComputeF0(0.5f, diffuse, 0);

    LightResult totalResult = (LightResult)0;
    for (int i = 0; i < 3; ++i)
    {
        Light light = tLights[i];
        float attenuation = GetAttenuation(light, wPos);
        float3 L = normalize(light.Position - wPos);
        if (light.Type == LIGHT_DIRECTIONAL)
        {
            L = -light.Direction;
        }

        ShadowRayPayload payload = (ShadowRayPayload)0;

    #if 1
        RayDesc ray;
        ray.Origin = wPos + 0.001f * L;
        ray.Direction = L;
        ray.TMin = 0.0f;
        ray.TMax = length(wPos - light.Position);

        // trace the ray
        TraceRay(
            SceneBVH,                                                       // Acceleration structure
            RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_FORCE_OPAQUE,    // Ray flags
            0xFF,                                                           // InstanceInclusionMask
            1,                                                              // RayContributionToHitGroupIndex
            2,                                                              // MultiplierForGeometryContributionToHitGroupIndex
            1,                                                              // MissShaderIndex
            ray,                                                            // Ray
            payload                                                         // Payload
        );
        attenuation *= payload.hit;
    #endif

        LightResult result = DefaultLitBxDF(specularColor, roughness, diffuse, N, V, L, attenuation);
        float4 color = light.GetColor();
        totalResult.Diffuse += result.Diffuse * color.rgb * light.Intensity;
        totalResult.Specular += result.Specular * color.rgb * light.Intensity;
    }

    payload.output = totalResult.Diffuse + totalResult.Specular + ApplyAmbientLight(diffuse, 1.0f, 0.1f);
}

[shader("closesthit")]
void ShadowClosestHit(inout ShadowRayPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
    payload.hit = 0;
}

[shader("miss")]
void ShadowMiss(inout ShadowRayPayload payload : SV_RayPayload)
{
    payload.hit = 1;
}

[shader("miss")]
void Miss(inout RayPayload payload : SV_RayPayload)
{
    payload.output = 0;
}

[shader("raygeneration")]
void RayGen()
{
    RayPayload payload = (RayPayload)0;

    float2 dimInv = rcp(DispatchRaysDimensions().xy);
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint launchIndex1d = launchIndex.x + launchIndex.y * DispatchRaysDimensions().x;
    float2 texCoord = (float2)launchIndex * dimInv;

    float3 world = WorldFromDepth(texCoord, tDepth.SampleLevel(sSceneSampler, texCoord, 0).r, cViewProjectionInverse);
    float2 texCoord1 = texCoord + float2(dimInv.x, 0);
    float2 texCoord2 = texCoord + float2(0, -dimInv.y);
    float3 p1 = WorldFromDepth(texCoord1, tDepth.SampleLevel(sSceneSampler, texCoord1, 0).r, cViewProjectionInverse);
    float3 p2 = WorldFromDepth(texCoord2, tDepth.SampleLevel(sSceneSampler, texCoord2, 0).r, cViewProjectionInverse);
    float3 N = normalize(cross(p2 - world, p1 - world));

    float3 V = normalize(world - cViewInverse[3].xyz);
    float3 R = reflect(V, N);

    RayDesc ray;
    ray.Origin = world + 0.001f * R;
    ray.Direction = R;
    ray.TMin = 0.0f;
    ray.TMax = 10000;

    TraceRay(
        SceneBVH, 
        RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_FORCE_OPAQUE,
        0xFF,
        0,
        2,
        0,
        ray,
        payload
    );

    gOutput[launchIndex] = float4(payload.output, 1);
}