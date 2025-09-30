#include "Tonemap/TonemappingCommon.hlsli"

#ifndef REPROJECT
#define REPROJECT
#endif

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 4), visibility = SHADER_VISIBILITY_ALL), " \
                "StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_POINT, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, visibility=SHADER_VISIBILITY_ALL), " \
                "StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, visibility=SHADER_VISIBILITY_ALL)"

struct ShaderParameters
{
    float4x4 Reprojection;
    float2 InvScreenDimensions;
    float2 Jitter;
};

ConstantBuffer<ShaderParameters> cParameters : register(b0);

Texture2D tVelocity : register(t0);
Texture2D tPreviousColor : register(t1);
Texture2D tCurrentColor : register(t2);
Texture2D tDepth : register(t3);

RWTexture2D<float4> uInOutColor : register(u0);

SamplerState sPointSampler : register(s0);
SamplerState sLinearSampler : register(s1);

// temporal reprojection in inside
float4 ClipAABB(float3 aabb_min, float3 aabb_max, float4 p, float4 q)
{
    // note: only clips towards aabb center (but fast)
    float3 center_clip = 0.5 * (aabb_max + aabb_min);
    float3 extent_clip = 0.5 * (aabb_max - aabb_min) + 0.00000001f;

    float4 v_clip = q - float4(center_clip, p.w);
    float3 v_unit = v_clip.xyz / extent_clip;
    float3 a_unit = abs(v_unit);
    float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0f)
    {
        return float4(center_clip, p.w) + v_clip / ma_unit;
    }
    return q; // point inside aabb
}

float3 SampleColor(Texture2D texture, SamplerState samplerState, float2 texCoord)
{
    return texture.SampleLevel(samplerState, texCoord, 0).rgb;
}

[RootSignature(RootSig)]
[numthreads(8, 8, 1)]
void CSMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixelIndex = DispatchThreadId.xy;
    float2 texCoord = cParameters.InvScreenDimensions * ((float2)pixelIndex + 0.5f);
    float2 pixelSize = cParameters.InvScreenDimensions;

    float3 neighborhood[9];
    float3 lt = SampleColor(tCurrentColor, sLinearSampler, texCoord + pixelSize * float2(-1, -1));
    float3 ct = SampleColor(tCurrentColor, sLinearSampler, texCoord + pixelSize * float2(0, -1));
    float3 rt = SampleColor(tCurrentColor, sLinearSampler, texCoord + pixelSize * float2(1, -1));
    float3 lc = SampleColor(tCurrentColor, sLinearSampler, texCoord + pixelSize * float2(-1, 0));
    float3 cc = SampleColor(tCurrentColor, sLinearSampler, texCoord + pixelSize * float2(0, 0));
    float3 rc = SampleColor(tCurrentColor, sLinearSampler, texCoord + pixelSize * float2(1, 0));
    float3 lb = SampleColor(tCurrentColor, sLinearSampler, texCoord + pixelSize * float2(-1, 1));
    float3 cb = SampleColor(tCurrentColor, sLinearSampler, texCoord + pixelSize * float2(0, 1));
    float3 rb = SampleColor(tCurrentColor, sLinearSampler, texCoord + pixelSize * float2(1, 1));
    float3 aabb_min = min(min(min(lt, ct), min(rt, lc)), min(min(cc, rc), min(lb, min(cb, rb))));
    float3 aabb_max = max(max(max(lt, ct), max(rt, lc)), max(max(cc, rc), max(lb, max(cb, rb))));
    float3 aabb_avg = (lt + ct + rt + lc + cc + rc + lb + cb + rb) / 9.0f;

    float3 currColor = cc;

#if defined(AABB_ROUNDED) //Karis Siggraph 2014 - Shaped neighborhood clamp by averaging 2 neighborhoods
    float3 aabb_min2 = min(min(min(min(lc, cc), ct), rc), cb);
    float3 aabb_max2 = max(max(max(max(lc, cc), ct), rc), cb);
    float3 aabb_avg2 = (lc + cc + ct + rc + cb) / 5.0f;
    aabb_min = (aabb_min + aabb_min2) * 0.5f;
    aabb_max = (aabb_max + aabb_max2) * 0.5f;
    aabb_avg = (aabb_avg + aabb_avg2) * 0.5f;
#endif

#if defined(REPROJECT)
    float depth = tDepth.SampleLevel(sLinearSampler, texCoord, 0).r;
    float4 pos = float4(texCoord, depth, 1);
    float4 prevPos = mul(pos, cParameters.Reprojection);
    prevPos.xyz /= prevPos.w;
    float2 velocity = (prevPos - pos).xy;
    texCoord += velocity;
#endif

    float3 prevColor = SampleColor(tPreviousColor, sLinearSampler, texCoord);

#if defined(DEBUG_RED_HISTORY)
    prevColor = float3(1, 0, 0);
#endif

#if defined(NEIGHBORHOOD_CLAMP)
    prevColor = clamp(prevColor, aabb_min, aabb_max);
#elif defined(NEIGHBORHOOD_CLIP)
    prevColor = ClipAABB(aabb_min, aabb_max, float4(aabb_avg, 1), float4(prevColor, 1)).xyz;
#endif

    float blendFactor = 0.05f;

#if defined(VELOCITY_CORRECT)
    float2 dimensions;
    tDepth.GetDimensions(dimensions.x, dimensions.y);
    float subpixelCorrection = frac(max(abs(velocity.x) * dimensions.x, abs(velocity.y) * dimensions.y)) * 0.5f;
    blendFactor = saturate(lerp(blendFactor, 0.8f, subpixelCorrection));
#endif

    blendFactor = (texCoord.x < 0 || texCoord.x > 1 || texCoord.y < 0 || texCoord.y > 1) ? 1 : blendFactor;

#if defined(TONEMAP)    //Karis Siggraph 2014 - Cheap tonemap before/after
    currColor = Reinhard(currColor);
    prevColor = Reinhard(prevColor);
#endif

    currColor = lerp(prevColor, currColor, blendFactor);

#if defined(TONEMAP)
    currColor = InverseReinhard(currColor);
    prevColor = InverseReinhard(prevColor);
#endif

    uInOutColor[pixelIndex] = float4(currColor, 1);
}





