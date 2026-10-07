#include "CommonBindings.hlsli"
#include "TonemappingCommon.hlsli"

#define RootSig ROOT_SIG("CBV(b0), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1)), " \
                "DescriptorTable(SRV(t0, numDescriptors = 1))")

#define EPSILON 0.0001f
#define HISTOGRAM_THREADS_PER_DIMENSION 16

struct PassParameters
{
    uint Width;
    uint Height;
    float MinLogLuminance;
    float OneOverLogLuminanceRange;
};

ConstantBuffer<PassParameters> cPassParameters : register(b0);

Texture2D tHDRTexture : register(t0);
RWByteAddressBuffer uLuminanceHistogram : register(u0);

groupshared uint HistogramShared[NUM_HISTOGRAM_BINS];

uint HDRToHistogramBin(float3 hdrColor)
{
    float luminance = GetLuminance(hdrColor);
    if (luminance < EPSILON)
    {
        return 0;
    }

    float logLuminance = saturate((log2(luminance) - cPassParameters.MinLogLuminance) * cPassParameters.OneOverLogLuminanceRange);
    return (uint)(logLuminance * (NUM_HISTOGRAM_BINS - 1) + 1.0f);
}

[RootSignature(RootSig)]
[numthreads(HISTOGRAM_THREADS_PER_DIMENSION, HISTOGRAM_THREADS_PER_DIMENSION, 1)]
void CSMain(uint groupIndex : SV_GroupIndex, uint3 threadId : SV_DispatchThreadID)
{
    HistogramShared[groupIndex] = 0;

    GroupMemoryBarrierWithGroupSync();

    if (threadId.x < cPassParameters.Width && threadId.y < cPassParameters.Height)
    {
        float3 hdrColor = tHDRTexture.Load(int3(threadId.xy, 0)).rgb;
        uint binIndex = HDRToHistogramBin(hdrColor);
        InterlockedAdd(HistogramShared[binIndex], 1);
    }

    GroupMemoryBarrierWithGroupSync();

    uLuminanceHistogram.InterlockedAdd(groupIndex * 4, HistogramShared[groupIndex]);
}