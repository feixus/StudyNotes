#define RootSig "CBV(b0, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL)"

struct ShaderParameters
{
    float4x4 ReprojectionMatrix;
    float2 InvScreenDimensions;
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
    float2 uv = cParams.InvScreenDimensions * (input.DispatchThreadID.xy + 0.5f);
    float depth = tDepthTexture[input.DispatchThreadID.xy].r;
    float4 pos = float4(uv, depth, 1);
    float4 prevPos = mul(pos, cParams.ReprojectionMatrix);
    prevPos.xyz /= prevPos.w;
    uVelocity[input.DispatchThreadID.xy] = (prevPos - pos).xy;
}