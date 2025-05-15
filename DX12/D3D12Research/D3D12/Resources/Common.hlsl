//https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules
struct Light
{
	float3 Position;
    int Enabled;
	float3 Direction;
	int Type;
	float Intensity;
	float Range;
	float SpotLightAngle;
	float Attenuation;
	float4 Color;
};

struct Plane
{
	float3 Normal;
	float DistanceToOrigin;
};

struct Frustum
{
	Plane Planes[4];
};

struct Sphere
{
	float3 Position;
	float Radius;
};

struct Cone
{
	float3 Tip;
	float Height;
	float3 Direction;
	float Radius;
};

struct AABB
{
	float3 Center;
	float3 Extents;
};

Plane CalculatePlane(float3 a, float3 b, float3 c)
{
	float3 v0 = b - a;
	float3 v1 = c - a;
	
	Plane plane;
	plane.Normal = normalize(cross(v1, v0));
	plane.DistanceToOrigin = dot(plane.Normal, a);
	return plane;
}

void AABBFromMinMax(inout AABB aabb, float3 minimum, float3 maximum)
{
	aabb.Center = (minimum + maximum) * 0.5f;
	aabb.Extents = abs(maximum - aabb.Center);
}

float3 HUEtoRGB(in float H)
{
	float R = abs(H * 6 - 3) - 1;
	float G = 2 - abs(H * 6 - 2);
	float B = 2 - abs(H * 6 - 4);
	return float3(R, G, B);
}

float4 ClipToView(float4 clip, float4x4 projectionInverse)
{
    float4 view = mul(clip, projectionInverse);
	// homegeneous coordinate to cartesion coordinate by perspective projection
	view /= view.w;
	return view;
}

float4 ScreenToView(float4 screen, float2 screenDimensions, float4x4 projectionInverse)
{
	// convert to normalized device coordinates
	float2 texCoord = screen.xy / screenDimensions;
	// convert to clip space
	float4 clip = float4(float2(texCoord.x, 1.0f - texCoord.y) * 2.0f - 1.0f, screen.z, screen.w);
	return ClipToView(clip, projectionInverse);
}

// emulates sampling a textureCube
// mapping a 3D direction vector to a 2D coordinate on one of the cube map faces.
//| Index | Face | Axis | Positive |
//| ----- | ---- | ---- | -------- |
//| 0     | +X   | `+X` | Right    |
//| 1     | -X   | `-X` | Left     |
//| 2     | +Y   | `+Y` | Top      |
//| 3     | -Y   | `-Y` | Bottom   |
//| 4     | +Z   | `+Z` | Front    |
//| 5     | -Z   | `-Z` | Back     |
float2 sampleCube(const float3 v, out float faceIndex)
{
    float3 vAbs = abs(v);
    float ma;
    float2 uv;
	if (vAbs.z >= vAbs.x && vAbs.z >= vAbs.y)
	{
		faceIndex = v.z < 0.0 ? 5.0 : 4.0;
		ma = 0.5 / vAbs.z;
		uv = float2(v.z < 0.0 ? -v.x : v.x, -v.y);
	}
	else if (vAbs.y >= vAbs.x)
	{
		faceIndex = v.y < 0.0 ? 3.0 : 2.0;
		ma = 0.5 / vAbs.y;
		uv = float2(v.x, v.y < 0.0 ? -v.z : v.z);
	}
	else
	{
		faceIndex = v.x < 0.0 ? 1.0 : 0.0;
		ma = 0.5 / vAbs.x;
		uv = float2(v.x < 0.0 ? -v.z : v.z, -v.y);
	}

	return uv * ma + 0.5;
}