#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                "CBV(b0, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 4), visibility = SHADER_VISIBILITY_PIXEL)")
                
struct VSInput
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
    float4 ray : RAY;
};

Texture2D tSceneTexture : register(t0);
Texture2D tDepthTexture : register(t1);
Texture3D tCloudsTexture : register(t2);
Texture2D tVerticalDensity : register(t3);

cbuffer Constants : register(b0)
{
    float4 cNoiseWeights;
    float4 cFrustumCorners[4];
    float4x4 cViewInverse;
    float cFarPlane;
    float cNearPlane;

    float cCloudScale;
    float cCloudThreshold;
    float3 cCloudOffset;
    float cCloudDensity;

    float3 cMinExtents;
    float3 cMaxExtents;
    float3 cSunDirection;
    float3 cSunColor;
}

[RootSignature(RootSig)]
PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = float4(input.position.xy, 0, 1);
    output.texCoord = input.texCoord;
    output.ray = mul(cFrustumCorners[int(input.position.z)], cViewInverse);
    return output;
}

//https://jcgt.org/published/0007/03/04/
float2 RayBoxDistance(float3 boundsMin, float3 boundsMax, float3 rayOrigin, float3 rayDirection)
{
    // ray(t) = rayOrigin + t * rayDirection
    float3 t0 = (boundsMin - rayOrigin) / rayDirection;
    float3 t1 = (boundsMax - rayOrigin) / rayDirection;
    float3 tmin = min(t0, t1);
    float3 tmax = max(t0, t1);

    float distanceA = max(max(tmin.x, tmin.y), tmin.z);
    float distanceB = min(min(tmax.x, tmax.y), tmax.z);

    float distanceToBox = max(distanceA, 0);
    float diatanceInsideBox = max(0, distanceB - distanceToBox);
    return float2(distanceToBox, diatanceInsideBox);
}

// non-linear depth buffer value to linear view-space depth (reversed-Z perspective projection in left hand) - linearizing the native depth values
//https://iolite-engine.com/blog_posts/reverse_z_cheatsheet
float GetLinearDepth(float c)
{
    return cFarPlane * cNearPlane / (cNearPlane + c * (cFarPlane - cNearPlane));
}

float SampleDensity(float3 position)
{
    float3 uvw = position * cCloudScale + cCloudOffset;
    float4 shape = tCloudsTexture.SampleLevel(sLinearClamp, uvw, 0);
    float s = shape.r * cNoiseWeights.x + shape.g * cNoiseWeights.y + shape.b * cNoiseWeights.z + shape.a * cNoiseWeights.w;
    return max(0, cCloudThreshold - s) * cCloudDensity;
}

float3 LightMarch(float3 position)
{
    float3 lightDirection = -cSunDirection;
    float boxDistance = RayBoxDistance(cMinExtents, cMaxExtents, position, lightDirection).y;
    float stepSize = boxDistance / 6;
    float totalDensity = 0;
    float offset = InterleavedGradientNoise(position.xy);
    position -= lightDirection * offset;
    for (int i= 0; i < 6; i++)
    {
        position += lightDirection * stepSize;
        totalDensity += max(0, SampleDensity(position) * stepSize);
    }

    float transmittance = exp(-totalDensity * 8.0f);
    return 0.01f + transmittance * (1 - 0.01f);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 ro = cViewInverse[3].xyz;
    float3 rd = normalize(input.ray.xyz);
    float2 boxResult = RayBoxDistance(cMinExtents, cMaxExtents, ro, rd);

    float4 color = tSceneTexture.Sample(sLinearClamp, input.texCoord);

    float depth = GetLinearDepth(tDepthTexture.Sample(sLinearClamp, input.texCoord).r);
    float maxDepth = depth * length(input.ray.xyz);

    float distanceTravelled = 0;
    float stepSize = boxResult.y / 150;
    float dstLimit = min(maxDepth - boxResult.x, boxResult.y);

    float totalDensity = 0;
    float3 totalLight = 0;
    float transmittance = 1;

    float offset = InterleavedGradientNoise(input.texCoord);
    ro += offset - 1;

    while (distanceTravelled < dstLimit)
    {
        float3 rayPos = ro + rd * (boxResult.x + distanceTravelled);
        float height = (cMaxExtents.y - rayPos.y) / (cMaxExtents.y - cMinExtents.y);
        float densityMultiplier = tVerticalDensity.Sample(sLinearClamp, float2(0, height)).r;
        float density = SampleDensity(rayPos) * stepSize * densityMultiplier;
        if (density > 0)
        {
            totalLight += LightMarch(rayPos) * stepSize * densityMultiplier * density * 3;
            transmittance *= exp(-density * stepSize * 0.03f);
            if (transmittance < 0.01f)
            {
                break;
            }
        }
        distanceTravelled += stepSize;
    }
    return float4(color.xyz * transmittance + totalLight * cSunColor.rgb, 1);
}
