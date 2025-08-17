#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1)), " \
                "DescriptorTable(SRV(t0, numDescriptors = 2))"

RWTexture2D<float4> uOutTexture : register(u0);
ByteAddressBuffer tLuminanceHistogram : register(t0);
Texture2D tAverageLuminance : register(t1);

cbuffer LuminanceHistogramBuffer : register(b0)
{
    float cMinLogLuminance;
    float cInverseLogLuminanceRange;
}

[RootSignature(RootSig)]
[numthreads(1, 256, 1)]
void DrawLuminanceHistogram(uint groupIndex : SV_GroupIndex, uint3 threadId : SV_DispatchThreadID)
{
    // the count of every histogram bin(0~255)
    uint c = tLuminanceHistogram.Load(threadId.x * 4);
    
    GroupMemoryBarrierWithGroupSync();

    uint maxC = 1;
    for (int i = 0; i < 256; i++)
    {
        maxC = max(maxC, tLuminanceHistogram.Load(i * 4));
    }

    uint2 dimensions;
    uOutTexture.GetDimensions(dimensions.x, dimensions.y);
    // every histogram bin occupy 4 pixel, from left to right(256 * 4)
    uint2 s = uint2(dimensions.x, 0) + uint2(-1024 + threadId.x * 4, groupIndex);

    float avg = tAverageLuminance.Load(int3(0, 0, 0)).r;
    float t = (log2(avg) - cMinLogLuminance) * cInverseLogLuminanceRange;

    // megenta shows the average luminance position
    if (floor(t * 256) == threadId.x)
    {
        uOutTexture[s + uint2(1, 0)] = float4(1, 0, 1, 0);
        uOutTexture[s + uint2(2, 0)] = float4(1, 0, 1, 0);
    }
    // red show the histogram values
    else if (256 - groupIndex < ceil((float)c / maxC * 256))
    {
        uOutTexture[s + uint2(1, 0)] = float4(1, 0, 0, 0);
        uOutTexture[s + uint2(2, 0)] = float4(1, 0, 0, 0);
    }
    else
    {
        uOutTexture[s + uint2(1, 0)] = float4(0, 0, 0, 0);
        uOutTexture[s + uint2(2, 0)] = float4(0, 0, 0, 0);
    }

    uOutTexture[s] = float4(0, 0, 0, 0);
    uOutTexture[s + uint2(3, 0)] = float4(0, 0, 0, 0);
}