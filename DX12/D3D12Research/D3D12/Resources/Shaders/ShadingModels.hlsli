#ifndef __INCLUDE_SHADING_MODELS__
#define __INCLUDE_SHADING_MODELS__

#include "Common.hlsli"

float DielectricSpecularToF0(float specular)
{
    return 0.08f * specular;
}

// note from Filament: vec3 f0 = 0.16 * reflectance * reflectance * (1.0 - metallic) + baseColor * metallic
float3 ComputeF0(float specular, float3 baseColor, float metalness)
{
    return lerp(DielectricSpecularToF0(specular).xxx, baseColor, metalness.xxx);
}

float3 ComputeDiffuseColor(float3 baseColor, float metalness)
{
    return baseColor * (1.0f - metalness);
}

float3 Diffuse_Lambert(float3 albedo)
{
    return albedo / PI;
}

// GGX / Trowbridge-Reitz
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float D_GGX(float a2, float NoH)
{
    float d = (NoH * a2 - NoH) * NoH + 1;
    return a2 / max(PI * d * d, 0.00001f);
}

// Approximation of joint Smith term for GGX
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float Vis_SmithJointApprox(float a2, float NoV, float NoL)
{
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1 - a) + a);
    float Vis_SmithL = NoV * (NoL * (1 - a) + a);
    return 0.5 * rcp(Vis_SmithV + Vis_SmithL);
}

// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
float3 F_Schlick(float3 SpecularColor, float VoH)
{
    float Fc = Pow5(1 - VoH);
    // return Fc + (1 - Fc) * SpecularColor;

    // anything less than 2% is physically impossible and is instead considered to be shadowing
    return saturate( 50.0 * SpecularColor.g) * Fc + (1 - Fc) * SpecularColor;
}

struct LightResult
{
    float3 Diffuse;
    float3 Specular;
};

// microfacet specular BRDF model
/*
        D * F * G
    --------------------  = D * Vis * F
        4 * NoL * NoV


                G
    Vis = -------------------
            4 * NoL * NoV

- F(l, h) - Fresnel Reflectance - what fraction of light is reflected as opposed to refracted (how reflective is the surface)
- D(h) - Normal Distribution Function - how many microfacets are pointing in the right direction
- G(l, v, h) - Geometry/ShadowMasking Function - how many light rays are actually reaching the view
*/

float3 SpecularGGX(float Roughness, float3 SpecularColor, float NoL, float NoH, float NoV, float VoH)
{
    float a2 = Pow4(Roughness);

    // Generalized microfacet specular
    float D = D_GGX(a2, NoH);
    float Vis = Vis_SmithJointApprox(a2, NoV, NoL);
    float3 F = F_Schlick(SpecularColor, VoH);

    return (D * Vis) * F;
}

LightResult DefaultLitBxDF(float3 SpecularColor, float Roughness, float3 DiffuseColor, half3 N, half3 V, half3 L, float Falloff)
{
    LightResult lighting = (LightResult)0;
    if (Falloff <= 0)
    {
        return lighting;
    }

    float NoL = saturate(dot(N, L));
    if (NoL <= 0)
    {
        return lighting;
    }

    float3 H = normalize(V + L);
    float NoH = saturate(dot(N, H));
    float NoV = saturate(abs(dot(N, V)) + 1e-5);
    float VoH = saturate(dot(V, H));

    lighting.Diffuse = (Falloff * NoL) * Diffuse_Lambert(DiffuseColor);
    lighting.Specular = (Falloff * NoL) * SpecularGGX(Roughness, SpecularColor, NoL, NoH, NoV, VoH);

    return lighting;
}

#endif