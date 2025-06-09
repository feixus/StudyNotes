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

SamplerState sSceneSampler : register(s0);

Texture3D tCloudsTexture : register(t2);
SamplerState sCloudsSampler : register(s1);

cbuffer Constants : register(b0)
{
    float4 cFrustumCorners[4];
    float4x4 cViewInverse;
    float cFarPlane;
    float cNearPlane;

    float cCloudScale;
    float cCloudThreshold;
    float3 cCloudOffset;
    float cCloudDensity;
}

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = float4(input.position.xy, 0, 1);
    output.texCoord = input.texCoord;
    output.ray = mul(cFrustumCorners[int(input.position.z)], cViewInverse);
    return output;
}

float2 rayBoxDistance(float3 boundsMin, float3 boundsMax, float3 rayOrigin, float3 rayDirection)
{
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

float GetLinearDepth(float c)
{
    return cFarPlane * cNearPlane / (cNearPlane + c * (cFarPlane - cNearPlane));
}

float SampleDensity(float3 position)
{
    float3 uvw = position * cCloudScale + cCloudOffset;
    float4 shape = tCloudsTexture.SampleLevel(sCloudsSampler, uvw, 0);
    return max(0, cCloudThreshold - shape.r) * cCloudDensity;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 rd = normalize(input.ray.xyz);
    float3 ro = cViewInverse[3].xyz;

    float4 color = tSceneTexture.Sample(sSceneSampler, input.texCoord);
    float depth = GetLinearDepth(tDepthTexture.Sample(sSceneSampler, input.texCoord).r);
    float maxDepth = depth * length(input.ray.xyz);

    float2 boxResult = rayBoxDistance(float3(-50, -5, -50), float3(50, 50, 50), ro, rd);

    float distanceTravelled = 0;
    float stepSize = boxResult.y / 100;
    float dstLimit = min(maxDepth - boxResult.x, boxResult.y);

    float totalDensity = 0;
    while (distanceTravelled < dstLimit)
    {
        float3 rayPos = ro + rd * (boxResult.x + distanceTravelled);
        totalDensity += SampleDensity(rayPos) * stepSize;
        distanceTravelled += stepSize;
    }

    float transmittance = exp(-totalDensity);
    return float4(color.xyz * transmittance, 1);
}
