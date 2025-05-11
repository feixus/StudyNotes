#include "Common.hlsl"
#include "Constants.hlsl"

cbuffer ShaderParameters : register(b0)
{
	float4x4 cProjectionInverse;
	float2 cScreenDimensions;
	float2 padding;
	uint4 cNumThreadGroups;
	uint4 cNumThreads;
}

RWStructuredBuffer<Frustum> uOutFrustums : register(u0);

struct CS_INPUT
{
	uint3 GroupId : SV_GroupID;						// 3D index of thread group in the dispatch
	uint3 GroupThreadId : SV_GroupThreadID;			// 3D index of thread in each thread group
	uint3 DispatchThreadId : SV_DispatchThreadID;	// 3D index of thread in the dispatch
	uint GroupIndex : SV_GroupIndex;				// index of the thread in the thread group
};

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(CS_INPUT input)
{
	float3 eyePos = float3(0.0f, 0.0f, 0.0f);

	// compute the 4 corner points on the far clipping plane to use as the frustum vertices 
	float4 screenSpace[4];
	screenSpace[0] = float4(input.DispatchThreadId.xy * BLOCK_SIZE, 1.0f, 1.0f);
	screenSpace[1] = float4(float2(input.DispatchThreadId.x + 1, input.DispatchThreadId.y ) * BLOCK_SIZE, 1.0f, 1.0f);
	screenSpace[2] = float4(float2(input.DispatchThreadId.x, input.DispatchThreadId.y + 1) * BLOCK_SIZE, 1.0f, 1.0f);
	screenSpace[3] = float4(float2(input.DispatchThreadId.x + 1, input.DispatchThreadId.y + 1) * BLOCK_SIZE, 1.0f, 1.0f);

	float3 viewSpace[4];
	for (int i = 0; i < 4; i++)
	{
		viewSpace[i] = ScreenToView(screenSpace[i], cScreenDimensions, cProjectionInverse).xyz;
	}

	Frustum frustum;
    frustum.Left = CalculatePlane(eyePos, viewSpace[2], viewSpace[0]);
    frustum.Right = CalculatePlane(eyePos, viewSpace[1], viewSpace[3]);
    frustum.Top = CalculatePlane(eyePos, viewSpace[0], viewSpace[1]);
    frustum.Bottom = CalculatePlane(eyePos, viewSpace[3], viewSpace[2]);

	if (input.DispatchThreadId.x < cNumThreads.x && input.DispatchThreadId.y < cNumThreads.y)
    {
		uint arrayIndex = input.DispatchThreadId.x + (input.DispatchThreadId.y * cNumThreads.x);
		uOutFrustums[arrayIndex] = frustum;
    }
}