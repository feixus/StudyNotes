#include "Common.hlsli"
#include "Lighting.hlsli"

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
                "CBV(b1, visibility = SHADER_VISIBILITY_ALL), " \
                "CBV(b2, visibility = SHADER_VISIBILITY_PIXEL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 3)), " \
                "DescriptorTable(SRV(t3, numDescriptors = 5), visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s1, filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_PIXEL, comparisonFunc = COMPARISON_GREATER)"

cbuffer PerObjectData : register(b0) // b-const buffer t-texture s-sampler
{
    float4x4 cWorld;
    float4x4 cWorldViewProjection;
}

cbuffer PerFrameData : register(b1)
{
    float4x4 cView;
    float4x4 cProjection;
    float4x4 cViewInverse;
    uint4 cClusterDimensions;
    float2 cScreenDimensions;
    float cNearZ;
    float cFarZ;
    float2 cClusterSize;
    float cSliceMagicA;
    float cSliceMagicB;
}

struct VSInput
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : TEXCOORD1;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 positionVS : POSITION_VS;
    float4 positionWS : POSITION_WS;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : TEXCOORD1;
    float poop : POOP;
};

Texture2D myDiffuseTexture : register(t0);
Texture2D myNormalTexture : register(t1);
Texture2D mySpecularTexture : register(t2);

SamplerState myDiffuseSampler : register(s0);

StructuredBuffer<uint2> tLightGrid : register(t4);
StructuredBuffer<uint> tLightIndexList : register(t5);
StructuredBuffer<Light> Lights : register(t6);
Texture2D tAO : register(t7);

uint GetSliceFromDepth(float depth)
{
    return (uint)(cSliceMagicA * log(depth) - cSliceMagicB);
}

LightResult DoLight(float4 pos, float4 vPos, float3 wPos, float3 N, float3 V, float3 diffuseColor, float3 specularColor, float roughness, float poop)
{
    uint3 clusterIndex3D = uint3(floor(pos.xy / cClusterSize), GetSliceFromDepth(vPos.z));
    uint clusterIndex1D = clusterIndex3D.x + cClusterDimensions.x * (clusterIndex3D.y + clusterIndex3D.z * cClusterDimensions.y);

    uint startOffset = tLightGrid[clusterIndex1D].x;
    uint lightCount = tLightGrid[clusterIndex1D].y; // lightCount = 0 means no light in this cluster
    LightResult totalResult = (LightResult)0;

    for (uint i = 0; i < lightCount; i++)
    {
        uint lightIndex = tLightIndexList[startOffset + i];
        Light light = Lights[lightIndex];

        LightResult result = DoLight(light, specularColor, diffuseColor, roughness, wPos, N, V, poop);
        totalResult.Diffuse += result.Diffuse;
        totalResult.Specular += result.Specular;
    }

    return totalResult;
}

#ifdef SHADOW
// volumetric scattering - Henyey-Greenstein phase function
#define G_SCATTERING 0.0001f
float ComputeScattering(float LoV)
{
    float result = 1.0f - G_SCATTERING * G_SCATTERING;
    result /= (4.0f * PI * pow(1.0f + G_SCATTERING * G_SCATTERING - (2.0f * G_SCATTERING) * LoV, 1.5f));
    return result;
}
#endif


[RootSignature(RootSig)]
PSInput VSMain(VSInput input)
{
    PSInput result;
    
    result.positionWS = mul(float4(input.position, 1.0f), cWorld);
    result.positionVS = mul(result.positionWS, cView);
    result.position = mul(float4(input.position, 1.0f), cWorldViewProjection);
    result.texCoord = input.texCoord;
    result.normal = normalize(mul(input.normal, (float3x3)cWorld));
    result.tangent = normalize(mul(input.tangent, (float3x3)cWorld));
    result.bitangent = normalize(mul(input.bitangent, (float3x3)cWorld));
    result.poop = result.positionVS.z;

    return result;
}

[earlydepthstencil]  // explicitly requests early depth stencil
float4 PSMain(PSInput input) : SV_TARGET
{
    float4 baseColor = myDiffuseTexture.Sample(myDiffuseSampler, input.texCoord);
    float3 specular = 0.5f;
    float metalness = 0;
    float r = 0.5f;

    float3 diffuseColor = ComputeDiffuseColor(baseColor.rgb, metalness);
    float3 specularColor = ComputeF0(specular.r, baseColor.rgb, metalness);

    float3x3 TBN = float3x3(normalize(input.tangent), normalize(input.bitangent), normalize(input.normal));
    float3 N = TangentSpaceNormalMapping(myNormalTexture, myDiffuseSampler, TBN, input.texCoord, true);
    float3 V = normalize(cViewInverse[3].xyz - input.positionWS.xyz);
    
    LightResult lightResults = DoLight(input.position, input.positionVS, input.positionWS.xyz, N, V, diffuseColor, specularColor, r, input.poop);

    float3 color = lightResults.Diffuse + lightResults.Specular;

    // constant ambient
    float ao = tAO.Sample(myDiffuseSampler, (float2)input.position.xy / cScreenDimensions).r;
    color += ApplyAmbientLight(diffuseColor, ao, 0.01f);
    
    /*float3 cameraPos = cViewInverse[3].xyz;
    float3 worldPos = input.positionWS.xyz;
    float3 rayVector = cameraPos - worldPos;
    float3 rayStep = rayVector / 300;
    float3 accumFog = 0.0f.xxx;

    float3 currentPosition = worldPos;
    for (int i = 0; i < 300; i++)
    {
        float4 worldInShadowCameraSpace = mul(float4(currentPosition, 1.0f), cLightViewProjection[0]);
        worldInShadowCameraSpace /= worldInShadowCameraSpace.w;
        worldInShadowCameraSpace.x = worldInShadowCameraSpace.x * 0.5f + 0.5f;
        worldInShadowCameraSpace.y = worldInShadowCameraSpace.y * -0.5f + 0.5f;
        float shadowMapValue = tShadowMapTexture.Sample(myDiffuseSampler, worldInShadowCameraSpace.xy).r;
        if (shadowMapValue < worldInShadowCameraSpace.z)
        {
            accumFog += ComputeScattering(dot(rayVector, Lights[0].Direction)).xxx * Lights[0].GetColor().rgb * Lights[0].Intensity;
        }
        currentPosition += rayStep;
    }
    accumFog /= 300.0f;
    color += accumFog;*/

    return float4(color, baseColor.a);
}