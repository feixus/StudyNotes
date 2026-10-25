#include "CommonBindings.hlsli"
#include "SkyCommon.hlsli"

// #define RootSig ROOT_SIG("RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
//                 "CBV(b0, visibility = SHADER_VISIBILITY_ALL)")

static const float4 CUBE[] = 
{
    float4(-1.0,1.0,1.0,1.0),
	float4(-1.0,-1.0,1.0,1.0),
	float4(-1.0,-1.0,-1.0,1.0),
	float4(1.0,1.0,1.0,1.0),
	float4(1.0,-1.0,1.0,1.0),
	float4(-1.0,-1.0,1.0,1.0),
	float4(1.0,1.0,-1.0,1.0),
	float4(1.0,-1.0,-1.0,1.0),
	float4(1.0,-1.0,1.0,1.0),
	float4(-1.0,1.0,-1.0,1.0),
	float4(-1.0,-1.0,-1.0,1.0),
	float4(1.0,-1.0,-1.0,1.0),
	float4(-1.0,-1.0,1.0,1.0),
	float4(1.0,-1.0,1.0,1.0),
	float4(1.0,-1.0,-1.0,1.0),
	float4(1.0,1.0,1.0,1.0),
	float4(-1.0,1.0,1.0,1.0),
	float4(-1.0,1.0,-1.0,1.0),
	float4(-1.0,1.0,-1.0,1.0),
	float4(-1.0,1.0,1.0,1.0),
	float4(-1.0,-1.0,-1.0,1.0),
	float4(-1.0,1.0,1.0,1.0),
	float4(1.0,1.0,1.0,1.0),
	float4(-1.0,-1.0,1.0,1.0),
	float4(1.0,1.0,1.0,1.0),
	float4(1.0,1.0,-1.0,1.0),
	float4(1.0,-1.0,1.0,1.0),
	float4(1.0,1.0,-1.0,1.0),
	float4(-1.0,1.0,-1.0,1.0),
	float4(1.0,-1.0,-1.0,1.0),
	float4(-1.0,-1.0,-1.0,1.0),
	float4(-1.0,-1.0,1.0,1.0),
	float4(1.0,-1.0,-1.0,1.0),
	float4(1.0,1.0,-1.0,1.0),
	float4(1.0,1.0,1.0,1.0),
	float4(-1.0,1.0,-1.0,1.0),
};

struct VSOutput
{
    float4 Position : SV_Position;
    float3 TexCoord : TEXCOORD;
};

// [RootSignature(RootSig)]
VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;

    float3 positionVS = mul(CUBE[vertexId].xyz, (float3x3)cViewUniforms.View);
    output.Position = mul(float4(positionVS, 1.0f), cViewUniforms.Projection);
    output.Position.z = 0.0001f;
    output.TexCoord = CUBE[vertexId].xyz;

    return output;
}

float4 PSMain(in VSOutput input) : SV_TARGET
{
    float3 dir = normalize(input.TexCoord);
    return float4(CIESky(dir), 1.0f);
}
