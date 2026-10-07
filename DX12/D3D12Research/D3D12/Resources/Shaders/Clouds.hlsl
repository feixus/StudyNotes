#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                "CBV(b0, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 4), visibility = SHADER_VISIBILITY_PIXEL)")
                
struct VertexInput
{
    float3 Position : POSITION;
    float2 UV : TEXCOORD;
};

struct InterpolantsVSToPS
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD;
    float4 Ray : RAY;
};

struct PassParameters
{
    float4 NoiseWeights;
    float4 FrustumCorners[4];
    float4x4 ViewInverse;
    float FarPlane;
    float NearPlane;

    float CloudScale;
    float CloudThreshold;
    float3 CloudOffset;
    float CloudDensity;

    float3 MinExtents;
    float3 MaxExtents;
    float3 SunDirection;
    float3 SunColor;
};

ConstantBuffer<PassParameters> cPassParameters : register(b0); 

Texture2D tSceneTexture : register(t0);
Texture2D tDepthTexture : register(t1);
Texture3D tCloudsTexture : register(t2);
Texture2D tVerticalDensity : register(t3);

[RootSignature(RootSig)]
InterpolantsVSToPS VSMain(VertexInput input)
{
    InterpolantsVSToPS output;
    output.Position = float4(input.Position.xy, 0, 1);
    output.UV = input.UV;
    output.Ray = mul(cPassParameters.FrustumCorners[int(input.Position.z)], cPassParameters.ViewInverse);
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
    return cPassParameters.FarPlane * cPassParameters.NearPlane / (cPassParameters.NearPlane + c * (cPassParameters.FarPlane - cPassParameters.NearPlane));
}

float SampleDensity(float3 position)
{
    float3 uvw = position * cPassParameters.CloudScale + cPassParameters.CloudOffset;
    float4 shape = tCloudsTexture.SampleLevel(sLinearClamp, uvw, 0);
    float s = shape.r * cPassParameters.NoiseWeights.x + shape.g * cPassParameters.NoiseWeights.y + shape.b * cPassParameters.NoiseWeights.z + shape.a * cPassParameters.NoiseWeights.w;
    return max(0, cPassParameters.CloudThreshold - s) * cPassParameters.CloudDensity;
}

float3 LightMarch(float3 position)
{
    float3 lightDirection = -cPassParameters.SunDirection;
    float boxDistance = RayBoxDistance(cPassParameters.MinExtents, cPassParameters.MaxExtents, position, lightDirection).y;
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

float4 PSMain(InterpolantsVSToPS input) : SV_Target
{
    float3 ro = cPassParameters.ViewInverse[3].xyz;
    float3 rd = normalize(input.Ray.xyz);
    float2 boxResult = RayBoxDistance(cPassParameters.MinExtents, cPassParameters.MaxExtents, ro, rd);

    float4 color = tSceneTexture.Sample(sLinearClamp, input.UV);

    float depth = GetLinearDepth(tDepthTexture.Sample(sLinearClamp, input.UV).r);
    float maxDepth = depth * length(input.Ray.xyz);

    float distanceTravelled = 0;
    float stepSize = boxResult.y / 150;
    float dstLimit = min(maxDepth - boxResult.x, boxResult.y);

    float totalDensity = 0;
    float3 totalLight = 0;
    float transmittance = 1;

    float offset = InterleavedGradientNoise(input.UV);
    ro += offset - 1;

    while (distanceTravelled < dstLimit)
    {
        float3 rayPos = ro + rd * (boxResult.x + distanceTravelled);
        float height = (cPassParameters.MaxExtents.y - rayPos.y) / (cPassParameters.MaxExtents.y - cPassParameters.MinExtents.y);
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
    return float4(color.xyz * transmittance + totalLight * cPassParameters.SunColor.rgb, 1);
}
