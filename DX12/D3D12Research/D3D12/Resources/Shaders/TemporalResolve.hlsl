#include "Tonemap/TonemappingCommon.hlsli"
#include "Color.hlsli"

#define HISTORY_REJECT_NONE 0
#define HISTORY_REJECT_CLAMP 1
#define HISTORY_REJECT_CLIP 2

#define HISTORY_RESOLVE_BILINEAR 0
#define HISTORY_RESOLVE_CATMULL_ROM 1

#define MIN_BLEND_FACTOR 0.05f
#define MAX_BLEND_FACTOR 0.12f

#define TAA_HISTORY_REJECT_METHOD   HISTORY_REJECT_CLIP             // use neighborhood clipping to reject history samples
#define TAA_RESOLVE_METHOD          HISTORY_RESOLVE_CATMULL_ROM     // history resolve filter
#define TAA_REPROJECT               1                               // use per pixel velocity to reproject previous frame
#define TAA_TONEMAP                 0                               // tonemap before resolving history to prevent high luminance pixels from overpowering
#define TAA_AABB_ROUNDED            1                               // use combine 3x3 neighborhood with plus-pattern neighborhood
#define TAA_VELOCITY_CORRECT        0                               // reduce blend factor when the subpixel motion is high to reduce blur under motion
#define TAA_DEBUG_RED_HISTORY       0
#define TAA_LUMINANCE_WEIGHT        0

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

float3 SampleColor(Texture2D texture, SamplerState textureSampler, float2 uv)
{
    return texture.SampleLevel(textureSampler, uv, 0).rgb;
}

// samples a texture with Catmull-Rom filtering, using 9 texture fetches instead of 16
float4 SampleTextureCatmullRom(in Texture2D tex, in SamplerState textureSampler, in float2 uv, in float2 texSize)
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
    result += tex.SampleLevel(textureSampler, float2(texPos0.x, texPos0.y), 0.0f) * w0.x * w0.y;
    result += tex.SampleLevel(textureSampler, float2(texPos12.x, texPos0.y), 0.0f) * w12.x * w0.y;
    result += tex.SampleLevel(textureSampler, float2(texPos3.x, texPos0.y), 0.0f) * w3.x * w0.y;

    result += tex.SampleLevel(textureSampler, float2(texPos0.x, texPos12.y), 0.0f) * w0.x * w12.y;
    result += tex.SampleLevel(textureSampler, float2(texPos12.x, texPos12.y), 0.0f) * w12.x * w12.y;
    result += tex.SampleLevel(textureSampler, float2(texPos3.x, texPos12.y), 0.0f) * w3.x * w12.y;

    result += tex.SampleLevel(textureSampler, float2(texPos0.x, texPos3.y), 0.0f) * w0.x * w3.y;
    result += tex.SampleLevel(textureSampler, float2(texPos12.x, texPos3.y), 0.0f) * w12.x * w3.y;
    result += tex.SampleLevel(textureSampler, float2(texPos3.x, texPos3.y), 0.0f) * w3.x * w3.y;

    return result;
}

[RootSignature(RootSig)]
[numthreads(8, 8, 1)]
void CSMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    const float2 dxdy = cParameters.InvScreenDimensions;
    uint2 pixelIndex = DispatchThreadId.xy;
    float2 texCoord = dxdy * ((float2)pixelIndex + 0.5f);

    float2 dimensions;
    tCurrentColor.GetDimensions(dimensions.x, dimensions.y);

    float3 cc = SampleColor(tCurrentColor, sPointSampler, texCoord);
    float3 currColor = cc;

#if TAA_HISTORY_REJECT_METHOD != HISTORY_REJECT_NONE
    // get a 3x3 neighborhood to clip/clamp against
    float3 lt = SampleColor(tCurrentColor, sPointSampler, texCoord + dxdy * float2(-1, -1));
    float3 ct = SampleColor(tCurrentColor, sPointSampler, texCoord + dxdy * float2(0, -1));
    float3 rt = SampleColor(tCurrentColor, sPointSampler, texCoord + dxdy * float2(1, -1));
    float3 lc = SampleColor(tCurrentColor, sPointSampler, texCoord + dxdy * float2(-1, 0));
    float3 rc = SampleColor(tCurrentColor, sPointSampler, texCoord + dxdy * float2(1, 0));
    float3 lb = SampleColor(tCurrentColor, sPointSampler, texCoord + dxdy * float2(-1, 1));
    float3 cb = SampleColor(tCurrentColor, sPointSampler, texCoord + dxdy * float2(0, 1));
    float3 rb = SampleColor(tCurrentColor, sPointSampler, texCoord + dxdy * float2(1, 1));
    float3 aabb_min = min(min(min(lt, ct), min(rt, lc)), min(min(cc, rc), min(lb, min(cb, rb))));
    float3 aabb_max = max(max(max(lt, ct), max(rt, lc)), max(max(cc, rc), max(lb, max(cb, rb))));
    float3 aabb_avg = (lt + ct + rt + lc + cc + rc + lb + cb + rb) / 9.0f;

#if TAA_AABB_ROUNDED //Karis Siggraph 2014 - Shaped neighborhood clamp by averaging 2 neighborhoods
    // average 3x3 neighborhood with 5 sample plus pattern neighborhood to remove 'filtered' look
    float3 aabb_min2 = min(min(min(min(lc, cc), ct), rc), cb);
    float3 aabb_max2 = max(max(max(max(lc, cc), ct), rc), cb);
    float3 aabb_avg2 = (lc + cc + ct + rc + cb) / 5.0f;
    aabb_min = (aabb_min + aabb_min2) * 0.5f;
    aabb_max = (aabb_max + aabb_max2) * 0.5f;
    aabb_avg = (aabb_avg + aabb_avg2) * 0.5f;
#endif
#endif

    float2 historyUV = texCoord;
#if TAA_REPROJECT
    float depth = tDepth.SampleLevel(sPointSampler, texCoord, 0).r;
    float4 pos = float4(texCoord, depth, 1);
    float4 prevPos = mul(pos, cParameters.Reprojection);
    prevPos.xyz /= prevPos.w;
    historyUV += (prevPos - pos).xy - cParameters.Jitter * dxdy;
#endif

#if TAA_RESOLVE_METHOD == HISTORY_RESOLVE_CATMULL_ROM    //Karis Siggraph 2014 - Shaped neighborhood clamp by averaging 2 neighborhoods
    // catmull rom filter to avoid blurry result from bilinear filter
    float3 prevColor = SampleTextureCatmullRom(tPreviousColor, sLinearSampler, historyUV, dimensions).rgb;
#elif TAA_RESOLVE_METHOD == HISTORY_RESOLVE_BILINEAR
    float3 prevColor = SampleColor(tPreviousColor, sLinearSampler, historyUV);
#endif

#if TAA_DEBUG_RED_HISTORY
    prevColor = float3(1, 0, 0);
#endif

#if TAA_HISTORY_REJECT_METHOD == HISTORY_REJECT_CLAMP
    prevColor = clamp(prevColor, aabb_min, aabb_max);
#elif TAA_HISTORY_REJECT_METHOD == HISTORY_REJECT_CLIP    //Karis Siggraph 2014 - Clip instead of clamp
    prevColor = ClipAABB(aabb_min, aabb_max, float4(aabb_avg, 1), float4(prevColor, 1)).xyz;
#endif

    float blendFactor = MIN_BLEND_FACTOR;

#if defined(TAA_VELOCITY_CORRECT)
    float subpixelCorrection = frac(max(abs(historyUV.x) * dimensions.x, abs(historyUV.y) * dimensions.y)) * 0.5f;
    blendFactor = saturate(lerp(blendFactor, 0.8f, subpixelCorrection));
#endif

#if TAA_LUMINANCE_WEIGHT    // feedback weight from unbiased luminance diff (TLottes)
    float lum0 = GetLuminance(currColor);
    float lum1 = GetLuminance(prevColor);
    float unbiased_diff = abs(lum0 - lum1) / max(lum0, max(lum1, 0.2));
    float unbiased_weight = unbiased_diff;
    float unbiased_weight_sqr = unbiased_weight * unbiased_weight;
    blendFactor = lerp(MIN_BLEND_FACTOR, MAX_BLEND_FACTOR, blendFactor);
#endif

#if TAA_TONEMAP
    currColor = Reinhard(currColor);
    prevColor = Reinhard(prevColor);
#endif

    currColor = lerp(prevColor, currColor, blendFactor);

#if TAA_TONEMAP
    currColor = InverseReinhard(currColor);
#endif

    uInOutColor[pixelIndex] = float4(currColor, 1);
}





