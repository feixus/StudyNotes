#include "Constants.hlsli"

//https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules
struct Light
{
	float3 Position;
    int Enabled;
	float3 Direction;
	int Type;
	float4 Color;
	float Range;
	float CosSpotLightAngle;
	float Attenuation;
    int ShadowIndex;
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
	float4 Center;
	float4 Extents;
};

bool SphereInAABB(Sphere sphere, AABB aabb)
{
    float3 dist = max(0, abs(sphere.Position - aabb.Center.xyz) - aabb.Extents.xyz);
    return dot(dist, dist) <= sphere.Radius * sphere.Radius;
}

bool SphereBehindPlane(Sphere sphere, Plane plane)
{
    return dot(plane.Normal, sphere.Position) + sphere.Radius < plane.DistanceToOrigin;
}

bool PointBehindPlane(float3 p, Plane plane)
{
    return dot(plane.Normal, p) < plane.DistanceToOrigin;
}

bool ConeBehindPlane(Cone cone, Plane plane)
{
    float3 furthestPointDir = cross(cross(plane.Normal, cone.Direction), cone.Direction);
    float3 furthestPointOnCircle = cone.Tip + cone.Direction * cone.Height - furthestPointDir * cone.Radius;
    return PointBehindPlane(cone.Tip, plane) && PointBehindPlane(furthestPointOnCircle, plane);
}

bool ConeInFrustum(Cone cone, Frustum frustum, float zNear, float zFar)
{
    Plane nearPlane, farPlane;
    nearPlane.Normal = float3(0, 0, 1);
    nearPlane.DistanceToOrigin = zNear;
    farPlane.Normal = float3(0, 0, -1);
    farPlane.DistanceToOrigin = -zFar;

    bool inside = !(ConeBehindPlane(cone, nearPlane) || ConeBehindPlane(cone, farPlane));
    for (int i = 0; i < 4 && inside; ++i)
    {
        inside = !ConeBehindPlane(cone, frustum.Planes[i]);
    }
    return inside;
}

bool SphereInFrustum(Sphere sphere, Frustum frustum, float depthNear, float depthFar)
{
    bool inside = sphere.Position.z + sphere.Radius > depthNear && sphere.Position.z - sphere.Radius < depthFar;
    for (int i = 0; i < 4 && inside; ++i)
    {
        inside = !SphereBehindPlane(sphere, frustum.Planes[i]);
    }

    return inside;
}

Plane CalculatePlane(float3 a, float3 b, float3 c)
{
	float3 v0 = b - a;
	float3 v1 = c - a;
	
	Plane plane;
	plane.Normal = normalize(cross(v1, v0));
	plane.DistanceToOrigin = dot(plane.Normal, a);
	return plane;
}

float LinearizeDepth(float z, float near, float far)
{
	float z_n = 2.0 * z - 1.0;
	return 2.0 * far * near / (near + far - z_n * (near - far));
}

void AABBFromMinMax(inout AABB aabb, float3 minimum, float3 maximum)
{
	aabb.Center = float4((minimum + maximum) * 0.5f, 0);
	aabb.Extents = float4(maximum, 0) - aabb.Center;
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
float2 SampleCube(const float3 v, out float faceIndex)
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

float GetCubeFaceIndex(const float3 v)
{
    float3 vAbs = abs(v);
    float faceIndex = 0;
    if (vAbs.z >= vAbs.x && vAbs.z >= vAbs.y)
    {
        faceIndex = v.z < 0.0 ? 5.0 : 4.0;
    }
    else if (vAbs.y >= vAbs.x)
    {
        faceIndex = v.y < 0.0 ? 3.0 : 2.0;
    }
    else
    {
        faceIndex = v.x < 0.0 ? 1.0 : 0.0;
    }
    return faceIndex;
}