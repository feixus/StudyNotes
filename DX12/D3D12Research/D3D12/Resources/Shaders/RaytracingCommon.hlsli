#ifndef __INCLUDE_RAYTRACING_COMMON__
#define __INCLUDE_RAYTRACING_COMMON__

struct RayCone
{
    float Width;
    float SpreadAngle;
};

RayCone PropagateRayCone(RayCone cone, float surfaceSpreadAngle, float hitT)
{
    RayCone newCone;
    newCone.Width = cone.Width + hitT * cone.SpreadAngle;
    newCone.SpreadAngle = cone.SpreadAngle + surfaceSpreadAngle;
    return newCone;
}

// Texture Level of Detail Strategies for Real-Time Ray Tracing
// Ray Tracing Gems - Tomas Akenine-Möller
float ComputeRayConeMip(RayCone cone, float3 vertexPositions[3], float2 vertexUVs[3], float2 textureDimensions)
{
    // triangle surface area
    float3 normal = cross(vertexPositions[2] - vertexPositions[0], vertexPositions[1] - vertexPositions[0]);
    float invWorldArea = rsqrt(dot(normal, normal));
    float3 triangleNormal = normal * invWorldArea;

    // UV area (2D cross product)
    float2 duv0 = vertexUVs[2] - vertexUVs[0];
    float2 duv1 = vertexUVs[1] - vertexUVs[0];
    float uvArea = abs(duv0.x * duv1.y - duv0.y * duv1.x);

    float triangleLODConstant = 0.5f * log2(uvArea * invWorldArea);
    float lambda = triangleLODConstant;
    lambda += log2(abs(cone.Width));
    lambda += 0.5f * log2(textureDimensions.x * textureDimensions.y);
    lambda -= log2(abs(dot(WorldRayDirection(), triangleNormal)));
    return lambda;
}

#endif