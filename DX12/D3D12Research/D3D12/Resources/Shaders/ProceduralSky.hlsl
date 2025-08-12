#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                "CBV(b0, visibility = SHADER_VISIBILITY_ALL)"

static const float4 CUBE[] = 
{
    float4(-1.0,1.0,1.0,1.0),
	float4(-1.0,-1.0,1.0,1.0),
	float4(-1.0,-1.0,-1.0,1.0),
	float4(1.0,1.0,1.0,1.0),
	float4(1.0,-1.0,1.0,1.0),
	float4(-1.0,-1.0,1.0,1.0),
	float4(1.0,1.0,-1.0,1.0),
	float4(1.0,-1.0,-1.0,1.0),
	float4(1.0,-1.0,1.0,1.0),
	float4(-1.0,1.0,-1.0,1.0),
	float4(-1.0,-1.0,-1.0,1.0),
	float4(1.0,-1.0,-1.0,1.0),
	float4(-1.0,-1.0,1.0,1.0),
	float4(1.0,-1.0,1.0,1.0),
	float4(1.0,-1.0,-1.0,1.0),
	float4(1.0,1.0,1.0,1.0),
	float4(-1.0,1.0,1.0,1.0),
	float4(-1.0,1.0,-1.0,1.0),
	float4(-1.0,1.0,-1.0,1.0),
	float4(-1.0,1.0,1.0,1.0),
	float4(-1.0,-1.0,-1.0,1.0),
	float4(-1.0,1.0,1.0,1.0),
	float4(1.0,1.0,1.0,1.0),
	float4(-1.0,-1.0,1.0,1.0),
	float4(1.0,1.0,1.0,1.0),
	float4(1.0,1.0,-1.0,1.0),
	float4(1.0,-1.0,1.0,1.0),
	float4(1.0,1.0,-1.0,1.0),
	float4(-1.0,1.0,-1.0,1.0),
	float4(1.0,-1.0,-1.0,1.0),
	float4(-1.0,-1.0,-1.0,1.0),
	float4(-1.0,-1.0,1.0,1.0),
	float4(1.0,-1.0,-1.0,1.0),
	float4(1.0,1.0,-1.0,1.0),
	float4(1.0,1.0,1.0,1.0),
	float4(-1.0,1.0,-1.0,1.0),
};

cbuffer VSConstants : register(b0)
{
    float4x4 cView;
    float4x4 cProjection;
    float3 cBias;
    float3 cSunDirection;
}

struct VSInput
{
    uint vertexId : SV_VertexID;
};

struct VSOutput
{
    float4 PositionCS : SV_Position;
    float3 TexCoord : TEXCOORD;
    float3 cBias : BIAS;
};

[RootSignature(RootSig)]
VSOutput VSMain(in VSInput input)
{
    VSOutput output;

    float3 positionVS = mul(CUBE[input.vertexId].xyz, (float3x3)cView);
    output.PositionCS = mul(float4(positionVS, 1.0f), cProjection);
    output.PositionCS.z = 0.0001f;
    output.TexCoord = CUBE[input.vertexId].xyz;

    return output;
}

float AngleBetween(float3 dir0, float3 dir1)
{
    return acos(dot(dir0, dir1));
}

//A Practical Analytic Model for Daylight
//A. J. Preetham, Peter Shirley, Brian Smits
//https://dl.acm.org/doi/pdf/10.1145/311535.311545
// clear sky or overcast sky only
// spectral radiance of the sun and sky in a given direction
// how the spectral radiance of a distant object is changed as it travels through air to he viewer
float3 CIESky(float3 dir, float3 sunDir)
{
    float3 skyDir = float3(dir.x, saturate(dir.y), dir.z);
    float gamma = AngleBetween(skyDir, sunDir);
    float S = AngleBetween(sunDir, float3(0, 1, 0));
    float theta = AngleBetween(skyDir, float3(0, 1, 0));

    float cosTheta = cos(theta);
    float cosS = cos(S);
    float cosGamma = cos(gamma);

    // sky luminance model
    float numerator = (0.91f + 10 * exp(-3 * gamma) + 0.45f * cosGamma * cosGamma) * (1 - exp(-0.32f / cosTheta));
    float denominator = (0.91f + 10 * exp(-3 * S) + 0.45f * cosS * cosS) * (1 - exp(-0.32f));
    float luminance = numerator / max(denominator, 0.0001f);

    // clear sky model only calculates luminance, so we'll pick a strong blue color for the sky
    const float3 SkyColor = float3(0.2f, 0.5f, 1.0f) * 1;
    const float3 SunColor = float3(1.0f, 0.8f, 0.3f) * 1500;
    const float SunWidth = 0.04f;

    float3 color = SkyColor;

    // draw a circle for the sun
    float sunGamma = AngleBetween(dir, sunDir);
    color = lerp(SunColor, SkyColor, saturate(abs(sunGamma) / SunWidth));

    return max(color * luminance, 0);
}

float4 PSMain(in VSOutput input) : SV_TARGET
{
    float3 dir = normalize(input.TexCoord);
    return float4(CIESky(dir, cSunDirection), 1.0f);
}