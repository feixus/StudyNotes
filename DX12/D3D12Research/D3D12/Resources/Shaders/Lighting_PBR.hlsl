#ifndef H_LIGHTING_PBR
#define H_LIGHTING_PBR

#include "Common.hlsli"
#include "ShadingModels.hlsli"

cbuffer LightData : register(b2)
{
    float4x4 cLightViewProjection[MAX_SHADOW_CASTERS];
    float4 cShadowMapOffsets[MAX_SHADOW_CASTERS];
}

float DoAttenuation(Light light, float d)
{
    return 1.0f - smoothstep(light.Range * light.Attenuation, light.Range, d);
}

#ifdef SHADOW
Texture2D tShadowMapTexture : register(t3);
SamplerComparisonState sShadowMapSampler : register(s2);

float DoShadow(float3 wPos, int shadowMapIndex)
{
    float4 lightViewProjection = cLightViewProjection[shadowMapIndex];

    float4 lightPos = mul(float4(wPos, 1), lightViewProjection);
    lightPos.xyz /= lightPos.w;
    lightPos.x = lightPos.x * 0.5f + 0.5f;
    lightPos.y = -lightPos.y * 0.5f + 0.5f;

    float shadowFactor = 0;
    int hKernel = (PCF_KERNEL_SIZE - 1) / 2;

    float2 shadowMapStart = cShadowMapOffsets[shadowMapIndex].xy;
    float normalizedShadowMapSize = cShadowMapOffsets[shadowMapIndex].z;

    for (int x = -hKernel; x <= hKernel; x++)
    {
        for (int y = -hKernel; y <= hKernel; y++)
        {
            float2 texCoord = shadowMapStart + lightPos.xy * normalizedShadowMapSize + float2(SHADOWMAP_DX * x, SHADOWMAP_DX * y);
            shadowFactor += tShadowMapTexture.SampleCmp(sShadowMapSampler, texCoord, lightPos.z);
        }
    }
    return shadowFactor / (PCF_KERNEL_SIZE * PCF_KERNEL_SIZE);
}
#endif

float GetAttenuation(Light light, float3 wPos)
{
    float attenuation = 1.0f;
    if (light.Type >= LIGHT_POINT)
    {
        float3 L = light.Position - wPos;
        float d = length(L);
        L = L / d;
        attenuation *= DoAttenuation(light, d);

        if (light.Type >= LIGHT_SPOT)
        {
            float minCos = cos(radians(light.SpotLightAngle));
            float maxCos = lerp(minCos, 1.0f, 1 - light.Attenuation);
            float cosAngle = dot(-L, light.Direction);
            float spotIntensity = smoothstep(minCos, maxCos, cosAngle);

            attenuation *= spotIntensity;
        }
    }
    return attenuation;
}

LightResult DoLight(Light light, float3 specularColor, float3 diffuseColor, float roughness, float3 wPos, float3 N, float3 V)
{
    float attenuation = GetAttenuation(light, wPos);
    float3 L = normalize(light.Position - wPos);
    return DefaultLitBxDF(specularColor, roughness, diffuseColor, N, V, L, attenuation);
}

#endif