#pragma once

namespace Math
{
    constexpr float PI = 3.14159265358979323846f;
    constexpr float INVPI = 0.31830988618379067154f;
    constexpr float INV2PI = 0.15915494309189533576f;
    constexpr float PIDIV2 = 1.57079632679489661923f;
    constexpr float PIDIV4 = 0.78539816339744830962f;

    constexpr float ToDegrees = 180.0f / PI;
    constexpr float ToRadians = PI / 180.0f;

    template<typename T>
    constexpr T Max(const T& a, const T& b)
    {
        return a > b ? a : b;
    }

    template<typename T>
    constexpr T Min(const T& a, const T& b)
    {
        return a < b ? a : b;
    }

    template<typename T>
    constexpr T Max(const T& a, const T& b, const T& c)
    {
        return Max(a, Max(b, c));
    }

    template<typename T>
    constexpr T Min(const T& a, const T& b, const T& c)
    {
        return Min(a, Min(b, c));
    }

    template<typename T>
    constexpr T Clamp(const T& value, const T& min, const T& max)
    {
        return Min(Max(value, min), max);
    }

    template<typename T>
    constexpr void ClampMin(T& value, const T lo)
    {
        if (value < lo)
            value = lo;
    }

    template<typename T>
    constexpr T ClampMin(const T& value, const T lo)
    {
        return value < lo ? lo : value;
    }

    template<typename T>
    constexpr T ClampMax(T& value, const T hi)
    {
        if (value > hi)
            value = hi;
    }

    template<typename T>
    constexpr T ClampMax(const T& value, const T hi)
    {
        return value > hi ? hi : value;
    }

    template<typename T>
    constexpr void Clamp01(T& value)
    {
        if (value < (T)0)
            value = (T)0;
        else if (value > (T)1)
            value = (T)1;
    }

    template<typename T>
    constexpr T Clamp01(const T value)
    {
        return Clamp(value, (T)0, (T)1);
    }

    template<typename T>
    constexpr T Average(const T& a, const T& b)
    {
        return (a + b) / (T)2;
    }

    template<typename T>
    constexpr T Average(const T& a, const T& b, const T& c)
    {
        return (a + b + c) / (T)3;
    }

    template<typename T>
    constexpr T Lerp(const T& a, const T& b, const T& t)
    {
        return a + (b - a) * t;
    }

    template<typename T>
    constexpr T InverseLerp(const T& a, const T& b, const T& value)
    {
        return (value - a) / (b - a);
    }

    float RandomRange(float min, float max);
    int RandomRange(int min, int max);

    Matrix CreatePerspectiveMatrix(float FoV, float aspectRatio, float nearPlane, float farPlane);
    Matrix CreatePerspectiveOffCenterMatrix(float left, float right, float bottom, float top, float nearPlane, float farPlane);
    Matrix CreateOrthographicMatrix(float width, float height, float nearPlane, float farPlane);
    Matrix CreateOrthographicOffCenterMatrix(float left, float right, float bottom, float top, float nearPlane, float farPlane);

    Vector3 ScaleFromMatrix(const Matrix& m);

    Quaternion LookRotation(const Vector3& direction);

    std::string ToBase(unsigned int number, unsigned int base);
    std::string ToBinaryString(unsigned int number);
    std::string ToOctalString(unsigned int number);
    std::string ToHexString(unsigned int number);

    Vector3 RandVector();
    Vector3 RandCircleVector();

    using HexColor = unsigned int;
    // convert between 4 float colors and unsigned int hex colors
    struct HexColorConverter
    {
        Color operator()(HexColor color) const
        {
            Color output;
            // unsigned int layout: AAAA RRRR GGGG BBBB
            output.x = (float)((color >> 16) & 0xFF) / 255.0f;
            output.y = (float)((color >> 8) & 0xFF) / 255.0f;
            output.z = (float)(color & 0xFF) / 255.0f;
            output.w = (float)((color >> 24) & 0xFF) / 255.0f;
            return output;
        }

        HexColor operator()(const Color& color) const
        {
            HexColor output{0};
            // unsigned int layout: AAAA RRRR GGGG BBBB
            output |= (unsigned int)(color.x * 255.0f) << 16;
            output |= (unsigned int)(color.y * 255.0f) << 8;
            output |= (unsigned int)(color.z * 255.0f);
            output |= (unsigned int)(color.w * 255.0f) << 24;
            return output;
        }
    };
}