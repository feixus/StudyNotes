#include "Tonemap/TonemappingCommon.hlsli"
#include "Color.hlsli"

#define TAA_REPROJECT
#define TAA_AABB_ROUNDED
#define TAA_NEIGHBORHOOD_CLIP
//#define TAA_VELOCITY_CORRECT
#define TAA_TONEMAP
#define TAA_RESOLVE_CATMULL_ROM

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

// samples a texture with Catmull-Rom filtering, using 9 texture fetches instead of 16
float4 SampleTextureCatmullRom(in Texture2D tex, in SamplerState linearSampler, in float2 uv, in float2 texSize)
{
    // sample a 4x4 grid of texels surrounding the target UV coordinate.
    // do this by rounding down the sample location to get the exact center texel.
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;

    // compute the fractional offset from the center texel to original sample location.
    // which will feed into the Catmull-Rom filter to get filter weights.
    float2 frac = samplePos - texPos1;

    // compute the Catmull-Rom weights using the fractional offset. (four control points: P0, P1, P2, P3)
    float2 w0 = frac * (-0.5f + frac * (1.0f - 0.5f * frac));
    float2 w1 = 1.0f + frac * frac * (-2.5f + 1.5f * frac);
    float2 w2 = frac * (0.5f + frac * (2.0f - 1.5f * frac));
    float2 w3 = frac * frac * (-0.5f + 0.5f * frac);

    // work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples(P1 and P2) from the 4x4 grid. so only need 9 samples(3x3)
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    // compute the final UV coordinates we'll use for sampling the texture. 
    float2 texPos0 = texPos1 - 1;
    float2 texPos3 = texPos1 + 2;
    float2 texPos12 = texPos1 + offset12;

    texPos0 /= texSize;
    texPos3 /= texSize;
    texPos12 /= texSize;

    float4 result = 0.0f;
    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos0.y), 0.0f) * w0.x * w0.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos0.y), 0.0f) * w12.x * w0.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos0.y), 0.0f) * w3.x * w0.y;

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos12.y), 0.0f) * w0.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos12.y), 0.0f) * w12.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos12.y), 0.0f) * w3.x * w12.y;

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos3.y), 0.0f) * w0.x * w3.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos3.y), 0.0f) * w12.x * w3.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos3.y), 0.0f) * w3.x * w3.y;

    return result;
}

[RootSignature(RootSig)]
[numthreads(8, 8, 1)]
void CSMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixelIndex = DispatchThreadId.xy;
    float2 texCoord = cParameters.InvScreenDimensions * ((float2)pixelIndex + 0.5f);
    float2 pixelSize = cParameters.InvScreenDimensions;

    float3 cc = SampleColor(tCurrentColor, sPointSampler, texCoord + pixelSize * float2(0, 0));
    float3 currColor = cc;

    float2 velocity = 0;
#if defined(TAA_REPROJECT)
    float depth = tDepth.SampleLevel(sLinearSampler, texCoord, 0).r;
    float4 pos = float4(texCoord, depth, 1);
    float4 prevPos = mul(pos, cParameters.Reprojection);
    prevPos.xyz /= prevPos.w;
    velocity = (prevPos - pos).xy - cParameters.Jitter * cParameters.InvScreenDimensions;
#endif

#ifdef TAA_RESOLVE_CATMULL_ROM
    float2 dimensions;
    tPreviousColor.GetDimensions(dimensions.x, dimensions.y);
    float3 prevColor = SampleTextureCatmullRom(tPreviousColor, sLinearSampler, texCoord + velocity, dimensions).rgb;
#else
    float3 prevColor = SampleColor(tPreviousColor, sLinearSampler, texCoord + velocity);
#endif


#if defined(TAA_NEIGHBORHOOD_CLAMP) || defined(TAA_NEIGHBORHOOD_CLIP)
    float3 lt = SampleColor(tCurrentColor, sPointSampler, texCoord + pixelSize * float2(-1, -1));
    float3 ct = SampleColor(tCurrentColor, sPointSampler, texCoord + pixelSize * float2(0, -1));
    float3 rt = SampleColor(tCurrentColor, sPointSampler, texCoord + pixelSize * float2(1, -1));
    float3 lc = SampleColor(tCurrentColor, sPointSampler, texCoord + pixelSize * float2(-1, 0));
    float3 rc = SampleColor(tCurrentColor, sPointSampler, texCoord + pixelSize * float2(1, 0));
    float3 lb = SampleColor(tCurrentColor, sPointSampler, texCoord + pixelSize * float2(-1, 1));
    float3 cb = SampleColor(tCurrentColor, sPointSampler, texCoord + pixelSize * float2(0, 1));
    float3 rb = SampleColor(tCurrentColor, sPointSampler, texCoord + pixelSize * float2(1, 1));
    float3 aabb_min = min(min(min(lt, ct), min(rt, lc)), min(min(cc, rc), min(lb, min(cb, rb))));
    float3 aabb_max = max(max(max(lt, ct), max(rt, lc)), max(max(cc, rc), max(lb, max(cb, rb))));
    float3 aabb_avg = (lt + ct + rt + lc + cc + rc + lb + cb + rb) / 9.0f;

#if defined(TAA_AABB_ROUNDED) //Karis Siggraph 2014 - Shaped neighborhood clamp by averaging 2 neighborhoods
    float3 aabb_min2 = min(min(min(min(lc, cc), ct), rc), cb);
    float3 aabb_max2 = max(max(max(max(lc, cc), ct), rc), cb);
    float3 aabb_avg2 = (lc + cc + ct + rc + cb) / 5.0f;
    aabb_min = (aabb_min + aabb_min2) * 0.5f;
    aabb_max = (aabb_max + aabb_max2) * 0.5f;
    aabb_avg = (aabb_avg + aabb_avg2) * 0.5f;
#endif
#endif

#if defined(TAA_DEBUG_RED_HISTORY)
    prevColor = float3(1, 0, 0);
#endif

#if defined(TAA_NEIGHBORHOOD_CLAMP)
    prevColor = clamp(prevColor, aabb_min, aabb_max);
#elif defined(TAA_NEIGHBORHOOD_CLIP)
    prevColor = ClipAABB(aabb_min, aabb_max, float4(aabb_avg, 1), float4(prevColor, 1)).xyz;
#endif

    float blendFactor = 0.05f;

#if defined(TAA_VELOCITY_CORRECT)
    float2 dimensions;
    tDepth.GetDimensions(dimensions.x, dimensions.y);
    float subpixelCorrection = frac(max(abs(velocity.x) * dimensions.x, abs(velocity.y) * dimensions.y)) * 0.5f;
    blendFactor = saturate(lerp(blendFactor, 0.8f, subpixelCorrection));
#endif

    blendFactor = (texCoord.x < 0 || texCoord.x > 1 || texCoord.y < 0 || texCoord.y > 1) ? 1 : blendFactor;

#if defined(TAA_TONEMAP)    //Karis Siggraph 2014 - Cheap tonemap before/after
    currColor = Reinhard(currColor);
    prevColor = Reinhard(prevColor);
#endif

    currColor = lerp(prevColor, currColor, blendFactor);

#if defined(TAA_TONEMAP)
    currColor = InverseReinhard(currColor);
    prevColor = InverseReinhard(prevColor);
#endif

    uInOutColor[pixelIndex] = float4(currColor, 1);
}





