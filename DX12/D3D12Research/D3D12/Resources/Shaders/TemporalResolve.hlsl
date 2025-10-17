#include "Tonemap/TonemappingCommon.hlsli"
#include "Color.hlsli"

#define HISTORY_REJECT_NONE             0
#define HISTORY_REJECT_CLAMP            1
#define HISTORY_REJECT_CLIP             2   // [Karis14]
#define HISTORY_REJECT_VARIANCE_CLIP    3   // [Salvi16]

#define HISTORY_RESOLVE_BILINEAR        0
#define HISTORY_RESOLVE_CATMULL_ROM     1

#define COLOR_SPACE_RGB         0
#define COLOR_SPACE_YCOCG       1   // [Karis14]

#define MIN_BLEND_FACTOR 0.05f
#define MAX_BLEND_FACTOR 0.12f

#ifndef TAA_TEST
#define TAA_TEST 0
#endif

#define TAA_COLOR_SPACE             COLOR_SPACE_YCOCG
#define TAA_HISTORY_REJECT_METHOD   HISTORY_REJECT_CLIP             // use neighborhood clipping to reject history samples
#define TAA_RESOLVE_METHOD          HISTORY_RESOLVE_CATMULL_ROM     // history resolve filter
#define TAA_REPROJECT               1                               // use per pixel velocity to reproject previous frame
#define TAA_TONEMAP                 0                               // tonemap before resolving history to prevent high luminance pixels from overpowering
#define TAA_AABB_ROUNDED            1                               // use combine 3x3 neighborhood with plus-pattern neighborhood
#define TAA_VELOCITY_CORRECT        0                               // reduce blend factor when the subpixel motion is high to reduce blur under motion
#define TAA_DEBUG_RED_HISTORY       0
#define TAA_LUMINANCE_WEIGHT        0
#define TAA_DILATE_VELOCITY         1                        

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 4), visibility = SHADER_VISIBILITY_ALL), " \
                "StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_POINT, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, visibility=SHADER_VISIBILITY_ALL), " \
                "StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, visibility=SHADER_VISIBILITY_ALL)"

struct ShaderParameters
{
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

float3 TransformColor(float3 color)
{
#if TAA_COLOR_SPACE == COLOR_SPACE_RGB
    return color;
#elif TAA_COLOR_SPACE == COLOR_SPACE_YCOCG
    return RGB_to_YCoCg(color);
#else
    #error No color space defined
    return 0;
#endif
}

float3 ResolveColor(float3 color)
{
#if TAA_COLOR_SPACE == COLOR_SPACE_RGB
    return color;
#elif TAA_COLOR_SPACE == COLOR_SPACE_YCOCG
    return YCoCg_to_RGB(color);
#else
    #error No color space defined
    return 0;
#endif
}

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
    return TransformColor(texture.SampleLevel(textureSampler, uv, 0).rgb);
}

 // Jorge Jimenez in his SIGGRAPH 2016 presentation about Filmic SMAA: http://advances.realtimerendering.com/s2016/Filmic%20SMAA%20v7.pptx
 // 5 tap cubic texture filter
float3 FilterHistory(Texture2D tex, SamplerState textureSampler, float2 texCoord, float2 dimensions)
{
    float2 position = texCoord * dimensions;
    float2 centerPosition = floor(position - 0.5f) + 0.5f;
    float2 frac = position - centerPosition;
    float2 frac2 = frac * frac;
    float2 frac3 = frac * frac2;

    const float SHARPNESS = 50.0f;
    float c = SHARPNESS / 100.0f;
    float2 w0 = -c * frac3 + 2.0f * c * frac2 - c * frac;
    float2 w1 = (2.0f - c) * frac3 + (c - 3.0f) * frac2 + 1.0f;
    float2 w2 = (c - 2.0f) * frac3 + (3.0f - 2.0f * c) * frac2 + c * frac;
    float2 w3 = c * frac3 - c * frac2;

    float2 w12 = w1 + w2;
    float2 tc12 = cParameters.InvScreenDimensions * (centerPosition + w2 / w12);
    float3 centerColor = SampleColor(tex, textureSampler, tc12);

    float2 tc0 = cParameters.InvScreenDimensions * (centerPosition - 1.0f);
    float2 tc3 = cParameters.InvScreenDimensions * (centerPosition + 2.0f);
    float3 color = SampleColor(tex, textureSampler, float2(tc12.x, tc0.y)) * w12.x * w0.y +
                   SampleColor(tex, textureSampler, float2(tc0.x, tc12.y)) * w0.x * w12.y +
                   centerColor * w12.x * w12.y +
                   SampleColor(tex, textureSampler, float2(tc3.x, tc12.y)) * w3.x * w12.y +
                   SampleColor(tex, textureSampler, float2(tc12.x, tc3.y)) * w12.x * w3.y;
    return color;
}

// https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
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

    return float4(TransformColor(result.rgb), result.a);
}

#define THREAD_GROUP_ROW_SIZE 8
#define THREAD_GROUP_SIZE (THREAD_GROUP_ROW_SIZE * THREAD_GROUP_ROW_SIZE)
#define GSM_ROW_SIZE (1 + THREAD_GROUP_ROW_SIZE + 1)
#define GSM_SIZE (GSM_ROW_SIZE * GSM_ROW_SIZE)

// ┌─────────────────────────────┐
// │ B  B  B  B  B  B  B  B  B  B │  Border row (row 0)
// │ B  •  •  •  •  •  •  •  •  B │  Row 1
// │ B  •  •  •  •  •  •  •  •  B │  Row 2
// │ B  •  •  •  •  •  •  •  •  B │  ...
// │ B  •  •  •  •  •  •  •  •  B │  ...
// │ B  •  •  •  •  •  •  •  •  B │  ...
// │ B  •  •  •  •  •  •  •  •  B │  ...
// │ B  •  •  •  •  •  •  •  •  B │  Row 8
// │ B  •  •  •  •  •  •  •  •  B │  Row 9
// │ B  B  B  B  B  B  B  B  B  B │  Border row (row 9)
// └─────────────────────────────┘
//   B = Border  • = Thread data

groupshared float3 gsColors[GSM_SIZE];
groupshared float gsDepths[GSM_SIZE];

[RootSignature(RootSig)]
[numthreads(THREAD_GROUP_ROW_SIZE, THREAD_GROUP_ROW_SIZE, 1)]
void CSMain(
    uint3 ThreadId : SV_DispatchThreadID,
    uint GroupIndex : SV_GroupIndex,
    uint3 GroupThreadId : SV_GroupThreadID,
    uint3 GroupId : SV_GroupID)
{
    const float2 dxdy = cParameters.InvScreenDimensions;
    uint2 pixelIndex = ThreadId.xy;
    float2 texCoord = dxdy * ((float2)pixelIndex + 0.5f);

    float2 dimensions;
    tCurrentColor.GetDimensions(dimensions.x, dimensions.y);

    int gsLocation = GroupThreadId.x + GroupThreadId.y * GSM_ROW_SIZE + GSM_ROW_SIZE + 1;
    int gsPrefetchLocation0 = GroupThreadId.x + GroupThreadId.y * THREAD_GROUP_ROW_SIZE; // 0-63
    int gsPrefetchLocation1 = gsPrefetchLocation0 + GSM_SIZE - THREAD_GROUP_SIZE; // 36-99
    int2 prefetchLocation0 = int2(pixelIndex.x & -8, pixelIndex.y & -8) - 1 + int2(gsPrefetchLocation0 % 10, gsPrefetchLocation0 / 10);
    int2 prefetchLocation1 = int2(pixelIndex.x & -8, pixelIndex.y & -8) - 1 + int2(gsPrefetchLocation1 % 10, gsPrefetchLocation1 / 10);

    gsColors[gsPrefetchLocation0] = TransformColor(tCurrentColor[prefetchLocation0].rgb);
    gsColors[gsPrefetchLocation1] = TransformColor(tCurrentColor[prefetchLocation1].rgb);
    gsDepths[gsPrefetchLocation0] = tDepth[prefetchLocation0].r;
    gsDepths[gsPrefetchLocation1] = tDepth[prefetchLocation1].r;

    GroupMemoryBarrierWithGroupSync();

    float3 cc = gsColors[gsLocation];
    float3 currColor = cc;

#if TAA_HISTORY_REJECT_METHOD != HISTORY_REJECT_NONE
    // get a 3x3 neighborhood to clip/clamp against
    float3 lt = gsColors[gsLocation - 11];
    float3 ct = gsColors[gsLocation - 10];
    float3 rt = gsColors[gsLocation -  9];
    float3 lc = gsColors[gsLocation -  1];
    float3 rc = gsColors[gsLocation +  1];
    float3 lb = gsColors[gsLocation +  9];
    float3 cb = gsColors[gsLocation + 10];
    float3 rb = gsColors[gsLocation + 11];

#if TAA_HISTORY_REJECT_METHOD == HISTORY_REJECT_CLAMP || TAA_HISTORY_REJECT_METHOD == HISTORY_REJECT_CLIP
    float3 aabb_min = min(min(min(lt, ct), min(rt, lc)), min(min(cc, rc), min(lb, min(cb, rb))));
    float3 aabb_max = max(max(max(lt, ct), max(rt, lc)), max(max(cc, rc), max(lb, max(cb, rb))));
    float3 aabb_avg = (lt + ct + rt + lc + cc + rc + lb + cb + rb) / 9.0f;

    #if TAA_AABB_ROUNDED
        // [Karis14] average 3x3 neighborhood with 5 sample plus pattern neighborhood to remove 'filtered' look
        float3 aabb_min2 = min(min(min(min(lc, cc), ct), rc), cb);
        float3 aabb_max2 = max(max(max(max(lc, cc), ct), rc), cb);
        float3 aabb_avg2 = (lc + cc + ct + rc + cb) / 5.0f;
        aabb_min = (aabb_min + aabb_min2) * 0.5f;
        aabb_max = (aabb_max + aabb_max2) * 0.5f;
        aabb_avg = (aabb_avg + aabb_avg2) * 0.5f;
    #endif

#elif TAA_HISTORY_REJECT_METHOD == HISTORY_REJECT_VARIANCE_CLIP
    // [Salvi16] - use first and second moment to clip history color
    float3 mean = (lt + ct + rt + lc + cc + rc + lb + cb + rb) / 9.0f;
    float3 mean2 = (Square(lt) + Square(ct) + Square(rt) + Square(lc) + Square(cc) + Square(rc) + Square(lb) + Square(cb) + Square(rb)) / 9.0f;
    float3 sigma = sqrt(mean2 - Square(mean));
    const float gamma = 1.0f;
    float3 aabb_min = mean - gamma * sigma;
    float3 aabb_max = mean + gamma * sigma;
    float3 aabb_avg = mean;
#endif
#endif

    float2 uvReproj = texCoord;

#if TAA_REPROJECT
    #if TAA_DILATE_VELOCITY
        // [Karis14] - use closest pixel to move edge along
        float4 crossDepths = float4(
            gsDepths[gsLocation - 11],
            gsDepths[gsLocation -  9],
            gsDepths[gsLocation + 11],
            gsDepths[gsLocation +  9]);
        float3 minOffset = float3(-1.0f, -1.0f, crossDepths.x);
        if (crossDepths.y > minOffset.z)
        {
            minOffset = float3(1.0f, -1.0f, crossDepths.y);
        }
        if (crossDepths.z > minOffset.z)
        {
            minOffset = float3(-1.0f, 1.0f, crossDepths.z);
        }
        if (crossDepths.w > minOffset.z)
        {
            minOffset = float3(1.0f, 1.0f, crossDepths.w);
        }

        float2 velocity = tVelocity.SampleLevel(sPointSampler, uvReproj + minOffset.xy * dxdy, 0).xy;
    #else
        float2 velocity = tVelocity.SampleLevel(sPointSampler, uvReproj, 0).xy;
    #endif

    uvReproj = texCoord + velocity;
#endif

#if TAA_RESOLVE_METHOD == HISTORY_RESOLVE_CATMULL_ROM
    // [Karis14] catmull-rom cubic filter to avoid blurry result from bilinear filter
    float3 prevColor = SampleTextureCatmullRom(tPreviousColor, sLinearSampler, uvReproj, dimensions).rgb;
    // float3 prevColor = FilterHistory(tPreviousColor, sLinearSampler, uvReproj, dimensions);
#elif TAA_RESOLVE_METHOD == HISTORY_RESOLVE_BILINEAR
    float3 prevColor = SampleColor(tPreviousColor, sLinearSampler, uvReproj);
#else
    #error No history resolve method defined
    float3 prevColor = 0;
#endif

#if TAA_DEBUG_RED_HISTORY
    prevColor = TransformColor(float3(1, 0, 0));
#endif

#if TAA_HISTORY_REJECT_METHOD == HISTORY_REJECT_CLAMP
    prevColor = clamp(prevColor, aabb_min, aabb_max);
#elif TAA_HISTORY_REJECT_METHOD == HISTORY_REJECT_CLIP    
    //Karis Siggraph 2014 - Clip instead of clamp
    prevColor = ClipAABB(aabb_min, aabb_max, float4(aabb_avg, 1), float4(prevColor, 1)).xyz;
#elif TAA_HISTORY_REJECT_METHOD == HISTORY_REJECT_VARIANCE_CLIP
    // [Salvi16]
    prevColor = ClipAABB(aabb_min, aabb_max, float4(aabb_avg, 1), float4(prevColor, 1)).xyz;
#endif

    float blendFactor = MIN_BLEND_FACTOR;

#if defined(TAA_VELOCITY_CORRECT)
    // [Xu16] reduce blend factor when the motion is more subpixel
    float subpixelCorrection = frac(max(abs(velocity.x) * dimensions.x, abs(velocity.y) * dimensions.y)) * 0.5f;
    blendFactor = saturate(lerp(blendFactor, 0.8f, subpixelCorrection));
#endif

#if TAA_LUMINANCE_WEIGHT    
    // [Lottes] feedback weight from unbiased luminance diff
    #if TAA_COLOR_SPACE == COLOR_SPACE_RGB
        float lum0 = GetLuminance(currColor);
        float lum1 = GetLuminance(prevColor);
    #else
        float lum0 = currColor.x;
        float lum1 = prevColor.x;
    #endif

    float unbiased_diff = abs(lum0 - lum1) / max(lum0, max(lum1, 0.2));
    float unbiased_weight = unbiased_diff;
    float unbiased_weight_sqr = unbiased_weight * unbiased_weight;
    blendFactor = lerp(MIN_BLEND_FACTOR, MAX_BLEND_FACTOR, unbiased_weight_sqr);
#endif

#if TAA_TONEMAP
    currColor = Reinhard(currColor);
    prevColor = Reinhard(prevColor);
#endif

    currColor = lerp(prevColor, currColor, blendFactor);

#if TAA_TONEMAP
    currColor = InverseReinhard(currColor);
#endif

    currColor = ResolveColor(currColor);

    uInOutColor[pixelIndex] = float4(currColor, 1);
}

// [Karis14] Brian Karis, "High Quality Temporal Supersampling", Siggraph 2014
// [Salvi16] Marco Salvi, "An Excursion in Temporal Supersampling", GDC 2016
// [Lottes] Timothy Lottes, "Temporal Reprojection Anti-Aliasing", NVIDIA
// [Xu16] Ke Xu, "Temporal Antialiasing in Uncharted 4", GDC 2016
/*
    Color Space: using YCoCg color space which better separates luminance from chrominance.
    History Rejection: uses neighborhood clipping to reject bad history samples.
    Resolve Methos: uses Catmull-Rom filtering for shaprper results than bilinear.
    Velocity Dilation: uses the closest depth pixel's velocity to move edges along.
*/


