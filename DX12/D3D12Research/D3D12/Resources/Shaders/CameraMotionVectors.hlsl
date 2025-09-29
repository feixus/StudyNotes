#define RootSig "CBV(b0, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL)"

struct ShaderParameters
{
    float4x4 ReprojectionMatrix;
    float2 InvScreenSize;
};

ConstantBuffer<ShaderParameters> cParams : register(b0);

RWTexture2D<float2> uVelocity : register(u0);
Texture2D tDepthTexture : register(t0);

struct CS_INPUT
{
    uint3 DispatchThreadID : SV_DispatchThreadID;
};

[RootSignature(RootSig)]
[numthreads(8, 8, 1)]
void CSMain(CS_INPUT input)
{
    float4 uv = float4((input.DispatchThreadID.xy + 0.5f) * cParams.InvScreenSize, 0, 1);
    uv.z = tDepthTexture.Load(int3(input.DispatchThreadID.xy, 0)).r;
    float4 prevUv = mul(uv, cParams.ReprojectionMatrix);
    uVelocity[input.DispatchThreadID.xy] = (uv - prevUv).xy;
}