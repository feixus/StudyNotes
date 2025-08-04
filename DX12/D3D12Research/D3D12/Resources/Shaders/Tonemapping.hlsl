#define RootSig "CBV(b0, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 2), visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL)"


#define TONEMAP_GAMMA 1.0


cbuffer Parameters : register(b0)
{
    float cWhitePoint;
}

struct PSInput
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD;
};

float3 convertRGB2XYZ(float3 rgb)
{
    // https://web.archive.org/web/20191027010220/http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
    float3 xyz;
    xyz.x = dot(float3(0.4124, 0.3576, 0.1805), rgb);  // rougnly corresponds to red-green perception
    xyz.y = dot(float3(0.2126, 0.7152, 0.0722), rgb);  // represents luminance(brightness)
    xyz.z = dot(float3(0.0193, 0.1192, 0.9505), rgb);  // roughly corresponds to blue-yellow perception
    // device-independent: XYZ can used as an intermediate space for color space conversion
    return xyz;
}

float3 convertXYZ2RGB(float3 xyz)
{
    float3 rgb;
    rgb.x = dot(float3(3.2406, -1.5372, -0.4986), xyz);
    rgb.y = dot(float3(-0.9689, 1.8758, 0.0415), xyz);
    rgb.z = dot(float3(0.0557, -0.2040, 1.0570), xyz);
    return rgb;
}

float3 convertXYZ2Yxy(float3 xyz)
{
    //  https://web.archive.org/web/20191027010144/http://www.brucelindbloom.com/index.html?Eqn_XYZ_to_xyY.html
    float inv = 1.0 / dot(xyz, float3(1, 1, 1));
    return float3(xyz.y, xyz.x * inv, xyz.y * inv);
}

float3 convertYxy2XYZ(float3 _Yxy)
{
    // https://web.archive.org/web/20191027010036/http://www.brucelindbloom.com/index.html?Eqn_xyY_to_XYZ.html
    float3 xyz;
    xyz.x = _Yxy.x * _Yxy.y / _Yxy.z;
    xyz.y = _Yxy.x;
    xyz.z = (1 - _Yxy.y - _Yxy.z) * _Yxy.x / _Yxy.z;
    return xyz;
}

float3 convertRGB2Yxy(float3 rgb)
{
    float3 xyz = convertRGB2XYZ(rgb);
    return convertXYZ2Yxy(xyz);
}

float3 convertYxy2RGB(float3 _Yxy)
{
    float3 xyz = convertYxy2XYZ(_Yxy);
    return convertXYZ2RGB(xyz);
}

float reinhard2(float x, float whiteSqr)
{
    return (x * (1.0 + x / whiteSqr)) / (1.0 + x);
}

float3 toGamma(float3 rgb)
{
    return pow(rgb, 1.0 / 2.2);
}

// Reinhard tonemapping
float4 tonemap_reinhard(in float3 color)
{
    color *= 16;
    color = color / (1 + color);
    float3 ret = pow(color, TONEMAP_GAMMA);
    return float4(ret, 1.0f);
}

// Uncharted 2 tonemapping
float3 tonemap_uncharted2(in float3 x)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;

    return (x * (A * x + C * B) + D * E) / (x * (A * x + B) + F * D) - E / F;
}

float3 tonemap_uc2(in float3 color)
{
    float W = 11.2;
    color *= 16; // hardcoded exposure adjustment

    float exposure_bias = 2.0f;
    float3 curr = tonemap_uncharted2(color * exposure_bias);

    float3 white_scale = 1.0f / tonemap_uncharted2(W);
    float3 ccolor = curr * white_scale;

    return pow(abs(ccolor), TONEMAP_GAMMA); // gamma
}

float3 tonemap_filmic(in float3 color)
{
    color = max(0, color - 0.004f);
    color = (color * (6.2f * color + 0.5f)) / (color * (6.2f * color + 1.7f) + 0.06f);

    // result has 1/2.2 baked in
    return pow(color, TONEMAP_GAMMA);
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

    float3 Yxy = convertRGB2Yxy(rgb);
    float lp = Yxy.x / (9.6 * avgLum + 0.0001f);
    Yxy.x = reinhard2(lp, cWhitePoint);

    rgb = convertYxy2RGB(Yxy);
    return float4(toGamma(rgb), 1.0f);
}