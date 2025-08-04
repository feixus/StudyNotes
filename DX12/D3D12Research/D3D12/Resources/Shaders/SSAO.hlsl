#include "Common.hlsli"
#include "RNG.hlsli"

#define SSAO_SAMPLES 64
#define BLOCK_SIZE 16

#define RootSig "CBV(b0, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 2), visibility = SHADER_VISIBILITY_ALL), " \
                "StaticSampler(s0, filter = FILTER_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_ALL), " \
                "StaticSampler(s1, filter = FILTER_COMPARISON_MIN_MAG_MIP_POINT, visibility = SHADER_VISIBILITY_ALL)"

cbuffer ShaderParameters : register(b0)
{
    float4x4 cProjectionInverse;
    float4x4 cProjection;
    float4x4 cView;
    uint2 cDimensions;
    float cNear;
    float cFar;
    float cAoPower;
    float cAoRadius;
    float cAoDepthThreshold;
    int cAoSamples;
}

Texture2D tDepthTexture : register(t0);
Texture2D tNormalsTexture : register(t1);
SamplerState sSampler : register(s0);

RWTexture2D<float> uAmbientOcclusion : register(u0);

struct CS_INPUT
{
    uint3 GroupId : SV_GroupID;
    uint3 GroupThreadId : SV_GroupThreadID;
    uint3 DispatchThreadId : SV_DispatchThreadID;
    uint GroupIndex : SV_GroupIndex;
};

[RootSignature(RootSig)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(CS_INPUT input)
{
    float2 texCoord = (float2)input.DispatchThreadId.xy / cDimensions;
    float depth = tDepthTexture.SampleLevel(sSampler, texCoord, 0).r;

    float4 viewPos = ScreenToView(float4(texCoord.xy, depth, 1), float2(1, 1), cProjectionInverse);
    // world to view space
    float3 normal = normalize(mul(tNormalsTexture.SampleLevel(sSampler, texCoord, 0).xyz, (float3x3)cView));

    // tangent space to view space 
    int state = SeedThread(input.DispatchThreadId.x + input.DispatchThreadId.y * cDimensions.x);
    float3 randomVec = float3(Random01(state), Random01(state), Random01(state)) * 2.0f - 1.0f;
    float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    float3 bitangent = cross(normal, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, normal);

    float occlusion = 0;

    for (int i = 0; i < cAoSamples; i++)
    {
        float2 point2d = HammersleyPoints(i, cAoSamples);
        float3 hemispherePoint = HemisphereSampleUniform(point2d.x, point2d.y);
        float3 newViewPos = viewPos.xyz + mul(hemispherePoint, TBN) * cAoRadius;
        float4 newTexCoord = mul(float4(newViewPos, 1), cProjection);
        newTexCoord.xyz /= newTexCoord.w;
        newTexCoord.xy = newTexCoord.xy * float2(0.5, -0.5) + float2(0.5, 0.5);

        if (newTexCoord.x >= 0 && newTexCoord.x <= 1 && newTexCoord.y >= 0 && newTexCoord.y <= 1)
        {
            float sampleDepth = tDepthTexture.SampleLevel(sSampler, newTexCoord.xy, 0).r;
            float4 sampleViewPos = ScreenToView(float4(newTexCoord.xy, sampleDepth, 1), float2(1, 1), cProjectionInverse);

            // discard the long distance samples
            float rangeCheck = smoothstep(0.0f, 1.0f, cAoRadius / (viewPos.z - sampleViewPos.z));
            // add depth bias to avoid self-occlusion
            occlusion += rangeCheck * (newViewPos.z >= (sampleViewPos.z + cAoDepthThreshold));
        }
    }
    occlusion /= cAoSamples;
    uAmbientOcclusion[input.DispatchThreadId.xy] = pow(saturate(1 - occlusion), cAoPower);
}
