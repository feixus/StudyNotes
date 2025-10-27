#include "Common.hlsli"
#include "ShadingModels.hlsli"
#include "Lighting.hlsli"
#include "SkyCommon.hlsli"

GlobalRootSignature GlobalRootSig = 
{
    "CBV(b0, visibility=SHADER_VISIBILITY_ALL),"
    "DescriptorTable(UAV(u0, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL),"
    "DescriptorTable(SRV(t5, numDescriptors=6), visibility=SHADER_VISIBILITY_ALL),"
    "DescriptorTable(SRV(t500, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL),"
    "DescriptorTable(SRV(t1000, numDescriptors=128, space = 2), visibility=SHADER_VISIBILITY_ALL),"
    "staticSampler(s0, filter=FILTER_MIN_MAG_LINEAR_MIP_POINT, visibility=SHADER_VISIBILITY_ALL)"
};

RWTexture2D<float4> uOutput : register(u0);

struct ViewData
{
    float4x4 ViewInverse;
    float4x4 ViewProjectionInverse;
    uint NumLights;
};
ConstantBuffer<ViewData> cViewData : register(b0);

struct HitData
{
    int DiffuseIndex;
    int NormalIndex;
    int RoughnessIndex;
    int MetallicIndex;
    uint VertexBufferOffset;
    uint IndexBufferOffset;
};
ConstantBuffer<HitData> cHitData : register(b1);

struct Vertex
{
    float3 position;
    float2 texCoord;
    float3 normal;
    float3 tangent;
    float3 bitangent;
};

struct RayPayload
{
    float3 output;
};

struct ShadowRayPayload
{
    uint hit;
};

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
    // resolve geometry data
    uint3 indices = tGeometryData.Load3(cHitData.IndexBufferOffset + PrimitiveIndex() * sizeof(uint3));
    float3 b = float3(1.0f - attrib.barycentrics.x - attrib.barycentrics.y, attrib.barycentrics.x, attrib.barycentrics.y);
    Vertex v0 = tGeometryData.Load<Vertex>(cHitData.VertexBufferOffset + indices.x * sizeof(Vertex));
    Vertex v1 = tGeometryData.Load<Vertex>(cHitData.VertexBufferOffset + indices.y * sizeof(Vertex));
    Vertex v2 = tGeometryData.Load<Vertex>(cHitData.VertexBufferOffset + indices.z * sizeof(Vertex));
    float2 texCoord = v0.texCoord * b.x + v1.texCoord * b.y + v2.texCoord * b.z;

    float3 N = v0.normal * b.x + v1.normal * b.y + v2.normal * b.z;
    float3 T = v0.tangent * b.x + v1.tangent * b.y + v2.tangent * b.z;
    float3 B = v0.bitangent * b.x + v1.bitangent * b.y + v2.bitangent * b.z;
    float3x3 TBN = float3x3(T, B, N);

    // get material data
    float3 diffuse = tMaterialTextures[cHitData.DiffuseIndex].SampleLevel(sDiffuseSampler, texCoord, 0).rgb;
    float3 sampledNormal = tMaterialTextures[cHitData.NormalIndex].SampleLevel(sDiffuseSampler, texCoord, 0).rgb;
    float metalness = tMaterialTextures[cHitData.MetallicIndex].SampleLevel(sDiffuseSampler, texCoord, 0).r;
    float roughness = 0.5f;
    float specular = 0.5f;

    float3 specularColor = ComputeF0(specular, diffuse, metalness);
    float3 wPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 V = normalize(-WorldRayDirection());
    N = TangentSpaceNormalMapping(sampledNormal, TBN, false);

    LightResult totalResult = (LightResult)0;
    for (int i = 0; i < cViewData.NumLights; ++i)
    {
        Light light = tLights[i];
        float attenuation = GetAttenuation(light, wPos);
        if (attenuation <= 0.0f)
        {
            continue;
        }

        float3 L = normalize(light.Position - wPos);
        if (light.Type == LIGHT_DIRECTIONAL)
        {
            L = -light.Direction;
        }

    #define SHADOW_RAY 1
    #if SHADOW_RAY
        RayDesc ray;
        ray.Origin = wPos + 0.001f * L;
        ray.Direction = L;
        ray.TMin = 0.0f;
        ray.TMax = length(wPos - light.Position);

        ShadowRayPayload payload = (ShadowRayPayload)0;
        // trace the ray
        TraceRay(
            tAccelerationStructure,                                         // Acceleration structure
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
    payload.hit = CIESky(WorldRayDirection(), -tLights[0].Direction);
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
    float2 texCoord = (float2)launchIndex * dimInv;

    float3 world = WorldFromDepth(texCoord, tDepth.SampleLevel(sDiffuseSampler, texCoord, 0).r, cViewData.ViewProjectionInverse);
    float4 reflectionSample = tSceneNormals.SampleLevel(sDiffuseSampler, texCoord, 0);
    float3 N = reflectionSample.rgb;
    float reflectivity = reflectionSample.a;
    if (reflectivity > 0)
    {
        float3 V = normalize(world - cViewData.ViewInverse[3].xyz);
        float3 R = reflect(V, N);

        RayDesc ray;
        ray.Origin = world + 0.001f * R;
        ray.Direction = R;
        ray.TMin = 0.0f;
        ray.TMax = 10000;

        TraceRay(
            tAccelerationStructure, 
            RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_FORCE_OPAQUE,
            0xFF,
            0,
            2,
            0,
            ray,
            payload
        );
    }

    float4 colorSample = tPreviousSceneColor.SampleLevel(sDiffuseSampler, texCoord, 0);
    uOutput[launchIndex] = colorSample + float4(reflectivity * payload.output, 0);
}