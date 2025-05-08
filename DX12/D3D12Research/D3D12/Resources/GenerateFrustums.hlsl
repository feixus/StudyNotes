cbuffer ShaderParameters : register(b0)
{
	float4x4 cProjectionInverse;
	float2 cScreenDimensions;
	float2 padding;
	uint4 cNumThreadGroups;
	uint4 cNumThreads;
}

struct Plane
{
	float3 Normal;
	float DistanceToOrigin;
};

struct Frustum
{
	Plane Left;
	Plane Right;
	Plane Top;
	Plane Bottom;
};

RWStructuredBuffer<Frustum> uOutFrustums : register(u0);

float4 ClipToView(float4 clip)
{
	float4 view = mul(cProjectionInverse, clip);
	// homegeneous coordinate to cartesion coordinate by perspective projection
	view /= view.w;
	return view;
}

float4 ScreenToView(float4 screen)
{
	// convert to normalized device coordinates
	float2 texCoord = screen.xy / cScreenDimensions;
	// convert to clip space
	float4 clip = float4(float2(texCoord.x, 1.0f - texCoord.y) * 2.0f - 1.0f, screen.z, screen.w);
	return ClipToView(clip);
}

Plane CalculatePlane(float3 a, float3 b, float3 c)
{
	float3 v0 = b - a;
	float3 v1 = c - a;
	
	Plane plane;
	plane.Normal = normalize(cross(v0, v1));
	plane.DistanceToOrigin = dot(plane.Normal, a);
	return plane;
}

struct CS_INPUT
{
	uint3 GroupId : SV_GroupID;						// 3D index of the group in the dispatch
	uint3 GroupThreadId : SV_GroupThreadID;			// local 3D index of a thread
	uint3 DispatchThreadId : SV_DispatchThreadID;	// global 3D index of a thread
	uint GroupIndex : SV_GroupIndex;				// index of the thread in the group
};

[numthreads(16, 16, 1)]
void CSMain(CS_INPUT input)
{
	float3 eyePos = float3(0.0f, 0.0f, 0.0f);

	float4 screenSpace[4];
	screenSpace[0] = float4( input.DispatchThreadId.xy * 16, 1.0f, 1.0f);
	screenSpace[1] = float4( float2(input.DispatchThreadId.x + 1, input.DispatchThreadId.y ) * 16, 1.0f, 1.0f);
	screenSpace[2] = float4( float2(input.DispatchThreadId.x, input.DispatchThreadId.y + 1) * 16, 1.0f, 1.0f);
	screenSpace[3] = float4( float2(input.DispatchThreadId.x + 1, input.DispatchThreadId.y + 1) * 16, 1.0f, 1.0f);

	float4 viewSpace[4];
	for (int i = 0; i < 4; i++)
	{
		viewSpace[i] = ScreenToView(screenSpace[i]);
	}

	Frustum frustum;
    frustum.Left = CalculatePlane(eyePos, viewSpace[2].xyz, viewSpace[0].xyz);
    frustum.Right = CalculatePlane(eyePos, viewSpace[3].xyz, viewSpace[1].xyz);
    frustum.Top = CalculatePlane(eyePos, viewSpace[0].xyz, viewSpace[1].xyz);
    frustum.Bottom = CalculatePlane(eyePos, viewSpace[2].xyz, viewSpace[3].xyz);

	uint arrayIndex = input.DispatchThreadId.x + (input.DispatchThreadId.y * cNumThreads.x);
	uOutFrustums[arrayIndex] = frustum;
}