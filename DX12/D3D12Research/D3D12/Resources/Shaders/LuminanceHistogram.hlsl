#define RootSig "CBV(b0, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1)), " \
                "DescriptorTable(SRV(t0, numDescriptors = 1))"

#define EPSILON 0.0001f
#define HISTOGRAM_THREADS_PER_DIMENSION 16
#define NUM_HISTOGRAM_BINS 256

Texture2D tHDRTexture : register(t0);
RWByteAddressBuffer uLuminanceHistogram : register(u0);

cbuffer LuminanceHistogramBuffer : register(b0)
{
    uint cWidth;
    uint cHeight;
    float cMinLogLuminance;
    float cOneOverLogLuminanceRange;
};

float GetLuminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

uint HDRToHistogramBin(float3 hdrColor)
{
    float luminance = GetLuminance(hdrColor);
    if (luminance < EPSILON)
    {
        return 0;
    }

    float logLuminance = saturate((log2(luminance) - cMinLogLuminance) * cOneOverLogLuminanceRange);
    return (uint)(logLuminance * 254 + 1.0f);
}

groupshared uint HistogramShared[NUM_HISTOGRAM_BINS];

[RootSignature(RootSig)]
[numthreads(HISTOGRAM_THREADS_PER_DIMENSION, HISTOGRAM_THREADS_PER_DIMENSION, 1)]
void CSMain(uint groupIndex : SV_GroupIndex, uint3 threadId : SV_DispatchThreadID)
{
    HistogramShared[groupIndex] = 0;

    GroupMemoryBarrierWithGroupSync();

    if (threadId.x < cWidth && threadId.y < cHeight)
    {
        float3 hdrColor = tHDRTexture.Load(uint3(threadId.xy, 0)).rgb;
        uint binIndex = HDRToHistogramBin(hdrColor);
        InterlockedAdd(HistogramShared[binIndex], 1);
    }

    GroupMemoryBarrierWithGroupSync();

    uLuminanceHistogram.InterlockedAdd(groupIndex * 4, HistogramShared[groupIndex]);
}