#include "Tonemap/TonemappingCommon.hlsli"

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 4), visibility = SHADER_VISIBILITY_ALL), " \
                "StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_POINT, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, visibility=SHADER_VISIBILITY_ALL), " \
                "StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, visibility=SHADER_VISIBILITY_ALL)"

cbuffer Parameters : register(b0)
{
    float4x4 cReprojection;
    float2 cInvScreenDimensions;
    float2 cJitter;
}

Texture2D tVelocity : register(t0);
Texture2D tPreviousColor : register(t1);
Texture2D tCurrentColor : register(t2);
Texture2D tDepth : register(t3);

RWTexture2D<float4> uInOutColor : register(u0);

SamplerState sPointSampler : register(s0);
SamplerState sLinearSampler : register(s1);

struct CS_INPUT
{
    uint3 DispatchThreadId : SV_DispatchThreadID;
};

// temporal reprojection in inside
float4 clip_aabb(float3 aabb_min, float3 aabb_max, float4 p, float4 q)
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

[RootSignature(RootSig)]
[numthreads(8, 8, 1)]
void CSMain(CS_INPUT input)
{
    float2 texCoord = cInvScreenDimensions * ((float2)input.DispatchThreadId.xy + 0.5f);
    float2 dimensions;
    tCurrentColor.GetDimensions(dimensions.x, dimensions.y);

    float2 dxdy = cInvScreenDimensions;
    float3 neighborhood[9];
    float3 average;

    int index = 0;
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            float3 color = tCurrentColor.SampleLevel(sPointSampler, texCoord + dxdy * float2(x, y), 0).rgb;
            neighborhood[index++] = color;
            average += color;
        }
    }

    float3 minn = 10000000000;
    float3 maxx = 0;
    for (int i= 0; i < 9; ++i)
    {
        minn = min(minn, neighborhood[i]);
        maxx = max(maxx, neighborhood[i]);
    }
    average /= 9.0f;

    float3 currColor = tCurrentColor.SampleLevel(sPointSampler, texCoord - cJitter * cInvScreenDimensions, 0).rgb;
    float2 velocity = tVelocity.SampleLevel(sPointSampler, texCoord, 0).rg;

    texCoord -= velocity;

    float3 prevColor = tPreviousColor.SampleLevel(sLinearSampler, texCoord, 0).rgb;
    prevColor = clip_aabb(minn, maxx, float4(clamp(average, minn, maxx), 1), float4(prevColor, 1)).xyz;

    float lum0 = GetLuminance(currColor);
    float lum1 = GetLuminance(prevColor);
    float unbiased_diff = abs(lum0 - lum1) / max(lum0, max(lum1, 0.2f));
    float unbiased_weight = 1.0 - unbiased_diff;
    float unbiased_weight_sqr = unbiased_weight * unbiased_weight;
    float blend = saturate(lerp(0.88f, 0.99f, unbiased_weight_sqr));

    if (texCoord.x < 0 || texCoord.x > 1 || texCoord.y < 0 || texCoord.y > 1)
    {
        blend = 1;
    }

    currColor = lerp(currColor, prevColor, blend);
    uInOutColor[input.DispatchThreadId.xy] = float4(currColor, 1);
}





