#ifndef __INCLUDE_SHADING_MODELS__
#define __INCLUDE_SHADING_MODELS__

#include "Common.hlsli"
#include "CommonBindings.hlsli"
#include "BRDF.hlsli"

// Kulla17 - Energy conervation due to multiple scattering
float3 EnergyCompensationFromMultipleScattering(float3 specularColor, float roughness, float ndotv)
{
    float gloss = Pow4(1 - roughness);
    float3 DFG = EnvDFGPolynomial(specularColor, gloss, ndotv);
    return 1.0f + specularColor * (1.0f /DFG.y - 1.0f);
}

// 0.08 is a max F0 we define for dielectrics which matches with Crystalware and gems(0.05 ~ 0.08)
// this means we cannot represent Diamond-like surfaces as they have an F0 of 0.1 ~ 0.2
float DielectricSpecularToF0(float specular)
{
    return 0.08f * specular;
}

// Cool list of IOR values for different surfaces
// https://pixelandpoly.com/ior.html
float IORToF0(float ior)
{
    return Square((ior - 1) / (ior + 1));
}

float IORToSpecular(float ior)
{
    return IORToF0(ior) / 0.08f;
}

// note from Filament: vec3 f0 = 0.16 * reflectance * reflectance * (1.0 - metallic) + baseColor * metallic
// F0 is the base specular reflectance of a surface at normal incidence
// For dielectrics, this is monochromatic commonly between 0.02(water) and 0.08(gems) and derived from a separate specular value
// For conductors, this is based on the base color we provided
float3 ComputeF0(float specular, float3 baseColor, float metalness)
{
    return lerp(DielectricSpecularToF0(specular).xxx, baseColor, metalness);
}

float3 ComputeDiffuseColor(float3 baseColor, float metalness)
{
    return baseColor * (1.0f - metalness);
}

struct LightResult
{
    float3 Diffuse;
    float3 Specular;
};

LightResult DefaultLitBxDF(float3 specularColor, float specularRoughness, float3 diffuseColor, half3 N, half3 V, half3 L, float falloff)
{
    LightResult lighting = (LightResult)0;
    if (falloff <= 0.0f)
    {
        return lighting;
    }

    float NdotL = saturate(dot(N, L));
    if (NdotL == 0.0f)
    {
        return lighting;
    }

    float3 H = normalize(V + L);
    float NdotV = saturate(abs(dot(N, V)) + 1e-5);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    // generalized microfacet specular BRDF
    specularRoughness = clamp(specularRoughness, 0.0001f, 1.0f);
    float a = Square(specularRoughness);
    float a2 = Square(a);
    float D = D_GGX(a2, NdotH);
    float Vis = Vis_SmithJointApprox(a2, NdotV, NdotL);
    float3 F = F_Schlick(specularColor, VdotH);

    lighting.Specular = (falloff * NdotL) * (D * Vis) * F;

#if 0
    // Kulla17 - Energy conervation due to multiple scattering
    float3 energyCompensation = EnergyCompensationFromMultipleScattering(specularColor, specularRoughness, NdotV);
    lighting.Specular *= energyCompensation;
#endif

    lighting.Diffuse = (falloff * NdotL) * Diffuse_Lambert(diffuseColor);

    return lighting;
}

#endif