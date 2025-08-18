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

	constexpr float ToKiloBytes = 1.0f / (1 << 10);
	constexpr float ToMegaBytes = 1.0f / (1 << 20);
	constexpr float ToGigaBytes = 1.0f / (1 << 30);

    template<typename T>
    T AlignUp(T value, T alignment)
    {
        return (value + ((T)alignment - 1)) & ~((T)alignment - 1);
    }

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
    T Clamp01(const T value)
    {
		if (value < 0)
			return 0;
		else if (value > 1)
			return 1;
        return value;
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

	void GetProjectionClipPlanes(const Matrix& projection, float& nearPlane, float& farPlane);
	void ReverseZProjection(Matrix& projection);

    Vector3 ScaleFromMatrix(const Matrix& m);

    Quaternion LookRotation(const Vector3& direction);

    std::string ToBase(unsigned int number, unsigned int base);
    std::string ToBinaryString(unsigned int number);
    std::string ToOctalString(unsigned int number);
    std::string ToHexString(unsigned int number);

    Vector3 RandVector();
    Vector3 RandCircleVector();

	inline uint32_t EncodeColor(const Color& color)
	{
		uint32_t output = 0;
		// unsigned int layout: AAAA RRRR GGGG BBBB
		output |= (unsigned char)(Clamp01(color.x) * 255.0f) << 16;
		output |= (unsigned char)(Clamp01(color.y) * 255.0f) << 8;
		output |= (unsigned char)(Clamp01(color.z) * 255.0f) << 0;
		output |= (unsigned char)(Clamp01(color.w) * 255.0f) << 24;
		return output;
	}

	inline Color DecodeColor(uint32_t color)
	{
		Color output;
		// unsigned int layout: AAAA RRRR GGGG BBBB
		output.x = ((color >> 16) & 0xFF) / 255.0f;
		output.y = ((color >> 8) & 0xFF) / 255.0f;;
		output.z = ((color >> 0) & 0xFF) / 255.0f;;
		output.w = ((color >> 24) & 0xFF) / 255.0f;;
		return output;
	}

    inline int32_t RoundUp(float value)
    {
        return (int32_t)ceil(value);
    }

	inline uint32_t DivideAndRoundUp(uint32_t nominator, uint32_t denominator)
	{
		return (uint32_t)ceil((float)nominator / denominator);
	}

    Color MakeFromColorTemperature(float Temp);
}