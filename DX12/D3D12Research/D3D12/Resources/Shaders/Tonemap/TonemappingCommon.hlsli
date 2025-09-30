#ifndef __INCLUDE_TONEMAPPING_COMMON__
#define __INCLUDE_TONEMAPPING_COMMON__

#include "../Common.hlsli"

#define NUM_HISTOGRAM_BINS 256

#define TONEMAP_LUMINANCE 0

#if TONEMAP_LUMINANCE
#define TONEMAP_TYPE float
#else
#define TONEMAP_TYPE float3
#endif


TONEMAP_TYPE Reinhard(TONEMAP_TYPE x)
{
    return x / (1.0f + x);
}

TONEMAP_TYPE InverseReinhard(TONEMAP_TYPE x)
{
    return x / (1.0f - x);
}

TONEMAP_TYPE ReinhardExtended(TONEMAP_TYPE x, float maxWhite)
{
    return (x * (1.0f + x / Square(maxWhite))) / (1.0f + x);
}

TONEMAP_TYPE ACES_Fast(TONEMAP_TYPE x)
{
    // Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}

TONEMAP_TYPE Unreal3(TONEMAP_TYPE x)
{
    // Unreal 3, Documentation: "Color Grading"
    // Adapted to be close to Tonemap_ACES, with similar range
    // Gamma 2.2 correction is baked in, don't use with sRGB conversion!
    return x / (x + 0.155f) * 1.019f;
}

TONEMAP_TYPE Uncharted2(TONEMAP_TYPE x)
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

float GetLuminance(float3 rgb)
{
    return dot(rgb, float3(0.2126729f, 0.7151522f, 0.0721750f)); // Rec. 709 luminance
}

/*
photographic exposure equation: EV = log2(N^2 / t)
where N is the f-number(aperture), t is the exposure time in seconds(shutter speed), and ISO is film/sensor sensitivity.

light meter equation: EV_100 = log2(L * K / ISO)
where L is the luminance in cd/m^2, K is a reflected-light meter constant(typically around 12.5).

converting EV to an Exposure Multiplier:
on a real camera, scene-side exposure is: H = t * L * S / N^2

*/

//https://google.github.io/filament/Filament.md.html#imagingpipeline/physicallybasedcamera/exposurevalue
float EV100FromLuminance(float luminance)
{
    const float K = 12.5f; // reflected-light meter constant
    const float ISO = 100.0f; // exposure value at ISO 100, 
    return log2(luminance * (ISO / K));
}

float Exposure(float ev100)
{
    return 1.0f / (pow(2.0f, ev100) * 1.2f);
}

#endif