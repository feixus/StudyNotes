#ifndef __INCLUDE_COMMON__
#define __INCLUDE_COMMON__

#include "Constants.hlsli"

// todo: SM6.6 replace with unpack_u8u32
float4 UIntToColor(uint c)
{
	return float4((float)(((c >> 24) & 0xFF) / 255.0f),
					   (float)(((c >> 16) & 0xFF) / 255.0f),
					   (float)(((c >> 8) & 0xFF) / 255.0f),
					   (float)(((c >> 0) & 0xFF) / 255.0f));
}

bool EnumHasAnyFlag(uint value, uint mask)
{
	return (value & mask) != 0;
}

bool EnumHasAllFlags(uint value, uint mask)
{
	return (value & mask) == mask;
}

enum LightFlags : uint
{
	LF_None = 0,
	LF_Enabled = 1 << 0,
	LF_CastShadows = 1 << 1,
	LF_Volumetrics = 1 << 2,
	LF_PointAttenuation = 1 << 3,
	LF_DirectionalAttenuation = 1 << 4,

	LF_LightTypeMask = LF_PointAttenuation | LF_DirectionalAttenuation,
	LF_PointLight = LF_PointAttenuation,
	LF_SpotLight = LF_PointAttenuation | LF_DirectionalAttenuation,
	LF_DirectionalLight = LF_None,
};

//https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules
struct Light
{
	float3 Position;
    int Flags;
	float3 Direction;
	uint Color;
	float2 SpotlightAngles;
	float Intensity;
	float Range;
    int ShadowIndex;
	float InvShadowSize;
	int LightTexture;

	float4 GetColor() { return UIntToColor(Color); }

	bool IsEnabled() { return EnumHasAllFlags(Flags, LF_Enabled); }
	bool CastShadows() { return EnumHasAllFlags(Flags, LF_CastShadows); }
	bool IsVolumetric() { return EnumHasAllFlags(Flags, LF_Volumetrics); }
	bool PointAttenuation() { return EnumHasAllFlags(Flags, LF_PointAttenuation); }
	bool DirectionalAttenuation() { return EnumHasAllFlags(Flags, LF_DirectionalAttenuation); }

	bool IsDirectional() { return (Flags & LF_LightTypeMask) == LF_DirectionalLight; }
	bool IsPoint() { return (Flags & LF_LightTypeMask) == LF_PointLight; }
	bool IsSpot() { return (Flags & LF_LightTypeMask) == LF_SpotLight; }
};

#define MAX_SHADOW_CASTERS 32
struct ShadowData
{
    float4x4 LightViewProjection[MAX_SHADOW_CASTERS];
    float4 CascadeDepths;
    uint NumCascades;
    uint ShadowMapOffset;
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

struct Ray
{
	float3 Origin;
	float3 Direction;
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

// clip space(-1, 1) coordinates to view space
float3 ClipToView(float4 clip, float4x4 projectionInverse)
{
	// view space position
    float4 view = mul(clip, projectionInverse);
	// homegeneous coordinate to cartesion coordinate by perspective projection
	view /= view.w;
	return view.xyz;
}

float3 ViewFromDepth(float2 uv, float depth, float4x4 projectionInverse)
{
	// convert to clip space
	float4 clip = float4(float2(uv.x, 1.0f - uv.y) * 2.0f - 1.0f, depth, 1.0f);
	return ClipToView(clip, projectionInverse);
}

float3 NormalFromDepth(Texture2D depthTexture, SamplerState depthSampler, float2 uv, float2 invDimensions, float4x4 inverseProjection)
{
	float3 vpos0 = ViewFromDepth(uv, depthTexture.SampleLevel(depthSampler, uv, 0).x, inverseProjection);
	float3 vpos1 = ViewFromDepth(uv + float2(invDimensions.x, 0), depthTexture.SampleLevel(depthSampler, uv + float2(invDimensions.x, 0), 0).x, inverseProjection);
	float3 vpos2 = ViewFromDepth(uv + float2(0, -invDimensions.y), depthTexture.SampleLevel(depthSampler, uv + float2(0, -invDimensions.y), 0).x, inverseProjection);
	return normalize(cross(vpos2 - vpos0, vpos1 - vpos0));	
}

float3 WorldFromDepth(float2 uv, float depth, float4x4 viewProjectionInverse)
{
	float4 clip = float4(float2(uv.x, 1.0f - uv.y) * 2.0f - 1.0f, depth, 1.0f);
	float4 world = mul(clip, viewProjectionInverse);
	return world.xyz / world.w;
}

// view space depth [0, 1]
float LinearizeDepth01(float z, float near, float far)
{
	return far / (far + z * (near - far));
}

// view space depth [0, far plane]
float LinearizeDepth(float z, float near, float far)
{
	return near * LinearizeDepth01(z, near, far);
}

// view space depth [0, far plane] to NDC [0, 1]
float LinearDepthToNDC(float z, float4x4 projection)
{
	return (z * projection[2][2] + projection[3][2]) / z;
}

void AABBFromMinMax(inout AABB aabb, float3 minimum, float3 maximum)
{
	aabb.Center = float4((minimum + maximum) * 0.5f, 0);
	aabb.Extents = float4(maximum, 0) - aabb.Center;
}

// screen space coordinates(0, width/height) to view space
float3 ScreenToView(float4 screen, float2 screenDimensionsInv, float4x4 projectionInverse)
{
	// convert to normalized device coordinates
	float2 screenNormalized = screen.xy * screenDimensionsInv;
	return ViewFromDepth(screenNormalized, screen.z, projectionInverse);
}

// view space position to screen UVs(0, 1). Non-linear Z
float3 ViewToWindow(float3 view, float4x4 projection)
{
	float4 proj = mul(float4(view, 1), projection);
	proj.xyz /= proj.w;
	proj.x = (proj.x + 1) * 0.5f;
	proj.y = (1 - proj.y) * 0.5f;
	return proj.xyz;
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

float Pow4(float x)
{
	float xx = x * x;
	return xx * xx;
}

float Pow5(float x)
{
	float xx = x * x;
	return xx * xx * x;
}

template<typename T>
T Square(T x)
{
	return x * x;
}

// follow IEC 61966-2-1 sRGB standard
float SrgbToLinear(float y)
{
	if (y <= 0.04045f)
		return y / 12.92f;
	else
		return pow((y + 0.055f) / 1.055f, 2.4f);
}

float SrgbToLinearFast(float y)
{
	return pow(y, 2.2f);
}

float LinearToSrgb(float x)
{
	if (x <= 0.0031308f)
		return x * 12.92f;
	else
		return 1.055f * pow(x, 1.0f / 2.4f) - 0.055f;
}

float LinearToSrgbFast(float x)
{
	return pow(x, 1.0f / 2.2f);
}

float3 LinearToSrgbFast(float3 rgb)
{
	return pow(rgb, 1.0f / 2.2f);
}

//From "NEXT GENERATION POST PROCESSING IN CALL OF DUTY: ADVANCED WARFARE"
//http://advances.realtimerendering.com/s2014/index.html
float InterleavedGradientNoise(float2 uv)
{
	const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
	return frac(magic.z * frac(dot(uv, magic.xy)));
}

float InterleavedGradientNoise(float2 uv, float offset)
{
	uv += offset * (float2(47, 17) * 0.695f);
	const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
	return frac(magic.z * frac(dot(uv, magic.xy)));
}

//Louis Bavoil
//https://developer.nvidia.com/blog/optimizing-compute-shaders-for-l2-locality-using-thread-group-id-swizzling/
void SwizzleThreadID(uint2 dispatchDimensions, uint2 numThreads, int2 groupId, int2 groupThreadIndex, out uint2 swizzledvThreadGroupID, out uint2 swizzledvThreadID)
{
	//Divide the 2d-Dispatch_Grid into tiles of dimension [N, Dispatch_Grid_Dim.y]
    //“CTA” (Cooperative Thread Array) == Thread Group in DirectX terminology
    uint2 Dispatch_Grid_Dim = dispatchDimensions; //Get this from the C++ side.
    uint2 CTA_Dim = numThreads; // This already known in HLSL
    uint Number_of_CTAs_to_launch_in_x_dim = 16; //Launch 16 CTAs in x-dimension
    // A perfect tile is one with dimensions = [Number_of_CTAs_to_launch_in_x_dim, Dispatch_Grid_Dim.y]
    uint Number_of_CTAs_in_a_perfect_tile = Number_of_CTAs_to_launch_in_x_dim * (Dispatch_Grid_Dim.y);
    //Possible number of perfect tiles
    uint Number_of_perfect_tiles = (Dispatch_Grid_Dim.x)/Number_of_CTAs_to_launch_in_x_dim;
    //Total number of CTAs present in the perfect tiles
    uint Total_CTAs_in_all_perfect_tiles = Number_of_perfect_tiles * Number_of_CTAs_to_launch_in_x_dim * Dispatch_Grid_Dim.y - 1;
    uint vThreadGroupIDFlattened = (Dispatch_Grid_Dim.x)*groupId.y + groupId.x;
    //Tile_ID_of_current_CTA : current CTA to TILE-ID mapping.
    uint Tile_ID_of_current_CTA = (vThreadGroupIDFlattened)/Number_of_CTAs_in_a_perfect_tile;
    uint Local_CTA_ID_within_current_tile = (vThreadGroupIDFlattened)%Number_of_CTAs_in_a_perfect_tile;
    uint Local_CTA_ID_y_within_current_tile;
    uint Local_CTA_ID_x_within_current_tile;
    if(Total_CTAs_in_all_perfect_tiles < vThreadGroupIDFlattened)
    {
        //Path taken only if the last tile has imperfect dimensions and CTAs from the last tile are launched. 
        uint X_dimension_of_last_tile = (Dispatch_Grid_Dim.x)%Number_of_CTAs_to_launch_in_x_dim;
        Local_CTA_ID_y_within_current_tile = (Local_CTA_ID_within_current_tile) / X_dimension_of_last_tile;
        Local_CTA_ID_x_within_current_tile = (Local_CTA_ID_within_current_tile) % X_dimension_of_last_tile;
    }
    else
    {
        Local_CTA_ID_y_within_current_tile = (Local_CTA_ID_within_current_tile) / Number_of_CTAs_to_launch_in_x_dim;
        Local_CTA_ID_x_within_current_tile = (Local_CTA_ID_within_current_tile) % Number_of_CTAs_to_launch_in_x_dim;
    }
    uint Swizzled_vThreadGroupIDFlattened = Tile_ID_of_current_CTA * Number_of_CTAs_to_launch_in_x_dim + Local_CTA_ID_y_within_current_tile * Dispatch_Grid_Dim.x + Local_CTA_ID_x_within_current_tile;
    swizzledvThreadGroupID.y = Swizzled_vThreadGroupIDFlattened / Dispatch_Grid_Dim.x;
    swizzledvThreadGroupID.x = Swizzled_vThreadGroupIDFlattened % Dispatch_Grid_Dim.x;
    swizzledvThreadID.x = (CTA_Dim.x)*swizzledvThreadGroupID.x + groupThreadIndex.x; 
    swizzledvThreadID.y = (CTA_Dim.y)*swizzledvThreadGroupID.y + groupThreadIndex.y;
}

float ScreenFade(float2 uv)
{
	float2 fade = max(12.0f * abs(uv - 0.5f) - 5.0f, 0.0f);
	return saturate(1.0f - dot(fade, fade));
}

bool RaySphereIntersect(Ray ray, Sphere sphere, out float intersectionA, out float intersectionB)
{
	intersectionA = 0;
	intersectionB = 0;

	float3 L = ray.Origin - sphere.Position;
	float a = dot(ray.Direction, ray.Direction);
	float b = 2.0f * dot(ray.Direction, L);
	float c = dot(L, L) - sphere.Radius * sphere.Radius;
	float D = b * b - 4.0f * a * c;
	if (D < 0)
	{
		return false;
	}
	if (D == 0)
	{
		intersectionA = -0.5f * b / a;
		intersectionB = intersectionA;
	}
	else
	{
		float q = (b > 0) ? -0.5f * (b + sqrt(D)) : -0.5f * (b - sqrt(D));
		intersectionA = q / a;
		intersectionB = c / q;
	}
	if (intersectionA > intersectionB)
	{
		float temp = intersectionA;
		intersectionA = intersectionB;
		intersectionB = temp;
	}
	return true;
}

#endif