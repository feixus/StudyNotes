#include "Common.hlsli"
#include "ShadingModels.hlsli"

#define SHADOWMAP_DX 0.000244140625f
#define MAX_SHADOW_CASTERS 8

cbuffer LightData : register(b2)
{
    float4x4 cLightViewProjection[MAX_SHADOW_CASTERS];
    float4 cShadowMapOffsets[MAX_SHADOW_CASTERS];
    float4 cCascadeDepths;
}

float3 TangentSpaceNormalMapping(Texture2D normalTexture, SamplerState normalSampler, float3x3 TBN, float2 tex, bool invertY)
{
    float3 sampledNormal = normalTexture.Sample(normalSampler, tex).rgb;
    sampledNormal.xy = sampledNormal.xy * 2.0f - 1.0f;
    if (invertY)
    {
        sampledNormal.y = -sampledNormal.y;
    }
    sampledNormal = normalize(sampledNormal);
    return mul(sampledNormal, TBN);
}

Texture2D tShadowMapTexture : register(t3);
SamplerComparisonState sShadowMapSampler : register(s1);

float DoShadow(float3 wPos, int shadowMapIndex)
{
    // clip space via perspective divide to ndc space(positive Y is up), then to texture space(positive Y is down)
    float4 lightPos = mul(float4(wPos, 1), cLightViewProjection[shadowMapIndex]);
    lightPos.xyz /= lightPos.w;
    lightPos.x = lightPos.x / 2.0f + 0.5f;
    lightPos.y = lightPos.y / -2.0f + 0.5f;
    lightPos.z += 0.0001f;
        
    float2 shadowMapStart = cShadowMapOffsets[shadowMapIndex].xy;
    float normalizeShadowMapSize = cShadowMapOffsets[shadowMapIndex].z;
        
    float2 texCoord = shadowMapStart + lightPos.xy * normalizeShadowMapSize;

    const float Dilation = 2.0f;
    float d1 = Dilation * SHADOWMAP_DX * 0.125f;
    float d2 = Dilation * SHADOWMAP_DX * 0.875f;
    float d3 = Dilation * SHADOWMAP_DX * 0.625f;
    float d4 = Dilation * SHADOWMAP_DX * 0.375f;
    float result = (
        2.0f * tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord, lightPos.z) +
        tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2(-d2,  d1), lightPos.z) +
        tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2(-d1, -d2), lightPos.z) +
        tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2( d2, -d1), lightPos.z) +
        tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2( d1,  d2), lightPos.z) +
        tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2(-d4,  d3), lightPos.z) +
        tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2(-d3, -d4), lightPos.z) +
        tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2( d4, -d3), lightPos.z) +
        tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2( d3,  d4), lightPos.z)
    ) / 10.0f;

    return result * result;
}

// Angle >= Umbra -> 0
// Angle < Penumbra -> 1
// gradient between Umbra and Penumbra
float DirectionalAttenuation(float3 L, float3 direction, float cosUmbra, float cosPenumbra)
{
    float cosAngle = dot(-normalize(L), direction);
    float falloff = saturate((cosAngle - cosUmbra) / (cosPenumbra - cosUmbra));
    return falloff * falloff;
}

// distance between rays is proportional to distance squared
// extra windowing function to make light radius finite
float RadialAttenuation(float3 L, float range)
{
    float distSq = dot(L, L);
    float distanceAttenuation = 1 / (distSq + 1);
    float windowing = Square(saturate(1 - Square(distSq * Square(rcp(range)))));
    return distanceAttenuation * windowing;
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
            attenuation *= DirectionalAttenuation(L, light.Direction, light.SpotlightAngles.y, light.SpotlightAngles.x);
        }
    }
    return attenuation;
}

float3 ApplyAmbientLight(float3 diffuse, float ao, float3 lightColor)
{
    return ao * diffuse * lightColor;
}

static float4 colors[4] = {
    float4(1, 0, 0, 1), // red
    float4(0, 1, 0, 1), // green
    float4(0, 0, 1, 1), // blue
    float4(1, 0, 1, 1)  // magenta
};

LightResult DoLight(Light light, float3 specularColor, float3 diffuseColor, float roughness, float4 pos, float3 wPos, float3 N, float3 V, float clipZ)
{
    float attenuation = GetAttenuation(light, wPos);
    float3 L = normalize(light.Position - wPos);
    LightResult result = DefaultLitBxDF(specularColor, roughness, diffuseColor, N, V, L, attenuation);

    if (light.Type == LIGHT_DIRECTIONAL)
    {
        float4 splits = clipZ < cCascadeDepths;
        int cascadeIndex = dot(splits, float4(1, 1, 1, 1));
        float shadowFactor = DoShadow(wPos, light.ShadowIndex + cascadeIndex);
        result.Diffuse *= shadowFactor;
        result.Specular *= shadowFactor;

#define VISUALIZE_CASCADES 0
#if VISUALIZE_CASCADES
        result.Diffuse += 0.2f * colors[cascadeIndex].xyz;
#endif
    }
    else if (light.ShadowIndex >= 0)
    {
        float shadowFactor = DoShadow(wPos, light.ShadowIndex);
        result.Diffuse *= shadowFactor;
        result.Specular *= shadowFactor;
    }

    float4 color = light.GetColor();
    result.Diffuse *= color.rgb * light.Intensity;
    result.Specular *= color.rgb * light.Intensity;

    return result;
}