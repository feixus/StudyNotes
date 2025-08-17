#include "Common.hlsli"

#define RootSig "CBV(b0, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 2), visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL)"


#define TONEMAP_GAMMA 1.0
#define TONEMAP_OPERATOR 2
#define TONEMAP_REINHARD 0
#define TONEMAP_REINHARD_EXTENDED 1
#define TONEMAP_ACES_FAST 2
#define TONEMAP_UNREAL3 3
#define TONEMAP_UNCHARTED2 4

cbuffer Parameters : register(b0)
{
    float cWhitePoint;
    uint cTonemapper;
}

struct PSInput
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD;
};

float3 ConvertRGB2XYZ(float3 rgb)
{
    // https://web.archive.org/web/20191027010220/http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
    float3 xyz;
    xyz.x = dot(float3(0.4124, 0.3576, 0.1805), rgb);  // rougnly corresponds to red-green perception
    xyz.y = dot(float3(0.2126, 0.7152, 0.0722), rgb);  // represents luminance(brightness)
    xyz.z = dot(float3(0.0193, 0.1192, 0.9505), rgb);  // roughly corresponds to blue-yellow perception
    // device-independent: XYZ can used as an intermediate space for color space conversion
    return xyz;
}

float3 ConvertXYZ2RGB(float3 xyz)
{
    float3 rgb;
    rgb.x = dot(float3(3.2406, -1.5372, -0.4986), xyz);
    rgb.y = dot(float3(-0.9689, 1.8758, 0.0415), xyz);
    rgb.z = dot(float3(0.0557, -0.2040, 1.0570), xyz);
    return rgb;
}

float3 ConvertXYZ2Yxy(float3 xyz)
{
    //  https://web.archive.org/web/20191027010144/http://www.brucelindbloom.com/index.html?Eqn_XYZ_to_xyY.html
    float inv = 1.0 / dot(xyz, float3(1, 1, 1));
    return float3(xyz.y, xyz.x * inv, xyz.y * inv);
}

float3 ConvertRGB2Yxy(float3 rgb)
{
    float3 xyz = ConvertRGB2XYZ(rgb);
    return ConvertXYZ2Yxy(xyz);
}

float3 ConvertYxy2XYZ(float3 _Yxy)
{
    // https://web.archive.org/web/20191027010036/http://www.brucelindbloom.com/index.html?Eqn_xyY_to_XYZ.html
    float3 xyz;
    xyz.x = _Yxy.x * _Yxy.y / _Yxy.z;
    xyz.y = _Yxy.x;
    xyz.z = (1 - _Yxy.y - _Yxy.z) * _Yxy.x / _Yxy.z;
    return xyz;
}

float3 ConvertYxy2RGB(float3 _Yxy)
{
    float3 xyz = ConvertYxy2XYZ(_Yxy);
    return ConvertXYZ2RGB(xyz);
}

float Reinhard(float x)
{
    return x / (1.0f + x);
}

float ReinhardExtended(float x, float maxWhite)
{
    return (x * (1.0f + x / Square(maxWhite))) / (1.0f + x);
}

float ACES_Fast(float x)
{
    // Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}

float Unreal3(float x)
{
    // Unreal 3, Documentation: "Color Grading"
    // Adapted to be close to Tonemap_ACES, with similar range
    // Gamma 2.2 correction is baked in, don't use with sRGB conversion!
    return x / (x + 0.155f) * 1.019f;
}

float Uncharted2(float x)
{
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    const float W = 11.2; // white point, 11.2 is the default value in Uncharted2

    return (x * (A * x + C * B) + D * E) / (x * (A * x + B) + F * D) - E / F;
}

Texture2D tColorTexture : register(t0);
SamplerState sColorSampler : register(s0);

Texture2D tAverageLuminance : register(t1);

[RootSignature(RootSig)]
PSInput VSMain(uint index : SV_VertexID)
{
    PSInput output;
    output.position.x = (float)(index / 2) * 4.0f - 1.0f;
    output.position.y = (float)(index % 2) * 4.0f - 1.0f;
    output.position.z = 0.0f;
    output.position.w = 1.0f;

    output.texCoord.x = (float)(index / 2) * 2.0f;
    output.texCoord.y = 1.0f - (float)(index % 2) * 2.0f;

    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 rgb = tColorTexture.Sample(sColorSampler, input.texCoord).rgb;
    float avgLum = tAverageLuminance.Load(uint3(0, 0, 0)).r;

    float3 Yxy = ConvertRGB2Yxy(rgb);

    //https://google.github.io/filament/Filament.md.html#imagingpipeline/physicallybasedcamera/exposurevalue
    /*const float ISO = 100.0f;
    const float K = 12.5f;
    const float q = 0.65f;
    float EV = log2((avgLum * ISO) / K);
    float EV100 = EV - log2(ISO / 100);
    float lMax = pow(2, EV100) * 1.2f;
    float newLuminance = Yxy.x / lMax;*/

    float newLuminance = Yxy.x / (9.6 * avgLum + 0.0001f);

    // tonemap on luminance only
    switch(cTonemapper)
    {
    case TONEMAP_REINHARD:
        Yxy.x = Reinhard(newLuminance);
        return float4(LinearToSrgbFast(ConvertYxy2RGB(Yxy)), 1.0f);
    case TONEMAP_REINHARD_EXTENDED:
        Yxy.x = ReinhardExtended(newLuminance, cWhitePoint);
        return float4(LinearToSrgbFast(ConvertYxy2RGB(Yxy)), 1.0f);
    case TONEMAP_ACES_FAST:
        Yxy.x = ACES_Fast(newLuminance);
        return float4(LinearToSrgbFast(ConvertYxy2RGB(Yxy)), 1.0f);
    case TONEMAP_UNREAL3:
        Yxy.x = Unreal3(newLuminance);
        return float4(ConvertYxy2RGB(Yxy), 1.0f);
    case TONEMAP_UNCHARTED2:
        Yxy.x = Uncharted2(newLuminance);
        return float4(LinearToSrgbFast(ConvertYxy2RGB(Yxy)), 1.0f);
    }
    
    return float4(0, 0, 0, 0);
}