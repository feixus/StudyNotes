#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("CBV(b0), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1)), " \
                "DescriptorTable(SRV(t0, numDescriptors = 1))")

#define BLOCK_SIZE 8

#define TEXTURE_STORAGE float4
#define TEXTURE_INPUT_TYPE Texture2D<TEXTURE_STORAGE>
#define TEXTURE_OUTPUT_TYPE RWTexture2D<TEXTURE_STORAGE>

TEXTURE_OUTPUT_TYPE uOutput : register(u0);
TEXTURE_INPUT_TYPE tInput : register(t0);

struct PassParameters
{
    uint2 TargetDimensions;
    float2 TargetDimensionsInv;
};

ConstantBuffer<PassParameters> cPassParameters : register(b0);

[RootSignature(RootSig)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(uint3 threadID : SV_DispatchThreadID)
{
    if (threadID.x < cPassParameters.TargetDimensions.x && threadID.y < cPassParameters.TargetDimensions.y)
    {
        uOutput[threadID.xy] = tInput.SampleLevel(sLinearClamp, ((float2)threadID.xy + 0.5f) * cPassParameters.TargetDimensionsInv, 0);
    }
}