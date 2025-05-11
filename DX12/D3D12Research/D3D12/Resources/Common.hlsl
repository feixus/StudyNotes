struct Light
{
    int Enabled;
	float3 Position;
	float3 Direction;
	float Intensity;
	float4 Color;
	float Range;
	float SpotLightAngle;
	float Attenuation;
	uint Type;
};

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

struct Sphere
{
	float3 Position;
	float Radius;
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

bool SphereBehindPlane(Sphere sphere, Plane plane)
{
    return dot(plane.Normal, sphere.Position) + sphere.Radius < plane.DistanceToOrigin;
}

bool SphereInFrustum(Sphere sphere, Frustum frustum, float depthNear, float depthFar)
{
    bool inside = sphere.Position.z + sphere.Radius > depthNear && sphere.Position.z - sphere.Radius < depthFar;
    
	inside = inside ? !SphereBehindPlane(sphere, frustum.Left) : false;
    inside = inside ? !SphereBehindPlane(sphere, frustum.Right) : false;
    inside = inside ? !SphereBehindPlane(sphere, frustum.Top) : false;
    inside = inside ? !SphereBehindPlane(sphere, frustum.Bottom) : false;

    return inside;
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