#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("CBV(b0), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1)), " \
                "DescriptorTable(SRV(t0, numDescriptors = 1))")

#define BLOCK_SIZE 16

struct PassParameters
{
    float Near;
    float Far;
};

ConstantBuffer<PassParameters> cPassParameters : register(b0);

RWTexture2D<float> uOutput : register(u0);
Texture2D<float> tInput : register(t0);

[RootSignature(RootSig)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(uint3 threadID : SV_DispatchThreadID)
{
    uint width, height;
    uOutput.GetDimensions(width, height);
    if (threadID.x < width && threadID.y < height)
    {
        uOutput[threadID.xy] = LinearizeDepth01(tInput[threadID.xy], cPassParameters.Near, cPassParameters.Far);
    }
}