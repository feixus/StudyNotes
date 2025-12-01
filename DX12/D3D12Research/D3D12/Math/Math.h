#pragma once

namespace Colors
{
    constexpr Color Transparent = Color(0.0f, 0.0f, 0.0f, 0.0f);
    constexpr Color Black       = Color(0.0f, 0.0f, 0.0f, 1.0f);
    constexpr Color White       = Color(1.0f, 1.0f, 1.0f, 1.0f);
    constexpr Color Red         = Color(1.0f, 0.0f, 0.0f, 1.0f);
    constexpr Color Green       = Color(0.0f, 1.0f, 0.0f, 1.0f);
    constexpr Color Blue        = Color(0.0f, 0.0f, 1.0f, 1.0f);
    constexpr Color Yellow      = Color(1.0f, 1.0f, 0.0f, 1.0f);
    constexpr Color Cyan        = Color(0.0f, 1.0f, 1.0f, 1.0f);
    constexpr Color Magenta     = Color(1.0f, 0.0f, 1.0f, 1.0f);
    constexpr Color Gray        = Color(0.5f, 0.5f, 0.5f, 1.0f);
}

namespace Math
{
    constexpr float PI = 3.14159265358979323846f;
    constexpr float INVPI = 0.31830988618379067154f;
    constexpr float INV2PI = 0.15915494309189533576f;
    constexpr float PIDIV2 = 1.57079632679489661923f;
    constexpr float PIDIV4 = 0.78539816339744830962f;

    constexpr float RadiansToDegrees = 180.0f / PI;
    constexpr float DegreesToRadians = PI / 180.0f;

	constexpr float BytesToKiloBytes = 1.0f / (1 << 10);
	constexpr float BytesToMegaBytes = 1.0f / (1 << 20);
	constexpr float BytesToGigaBytes = 1.0f / (1 << 30);

	constexpr uint32_t KilobytesToBytes = 1 << 10;
	constexpr uint32_t MegabytesToBytes = 1 << 20;
	constexpr uint32_t GigabytesToBytes = 1 << 30;

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
    Matrix CreateLookToMatrix(const Vector3& position, const Vector3& direction, const Vector3& up);

	void GetProjectionClipPlanes(const Matrix& projection, float& nearPlane, float& farPlane);
	void ReverseZProjection(Matrix& projection);

    Vector3 ScaleFromMatrix(const Matrix& m);

    Quaternion LookRotation(const Vector3& direction);

    std::string ToBase(unsigned int number, unsigned int base, bool addPrefix = true);

    inline std::string ToBinary(unsigned int number, bool addPrefix = true)
    {
        return ToBase(number, 2, addPrefix);
    }

    inline std::string ToHex(unsigned int number, bool addPrefix = true)
    {
        return ToBase(number, 16, addPrefix);
    }

    Vector3 RandVector();
    Vector3 RandCircleVector();

    inline uint32_t EncodeColor(float r, float g, float b, float a = 1.0f)
    {
        uint32_t output = 0;
        // unsigned int layout: RRRR GGGG BBBB AAAA
        output |= (unsigned char)(Clamp01(r) * 255.0f) << 24;
        output |= (unsigned char)(Clamp01(g) * 255.0f) << 16;
        output |= (unsigned char)(Clamp01(b) * 255.0f) << 8;
        output |= (unsigned char)(Clamp01(a) * 255.0f) << 0;
        return output;
    }

	inline uint32_t EncodeColor(const Color& color)
	{
		return EncodeColor(color.x, color.y, color.z, color.w);
	}

	inline Color DecodeColor(uint32_t color)
	{
		Color output;
        constexpr float rcp_255 = 1.0f / 255.0f;
		// unsigned int layout: RRRR GGGG BBBB AAAA
		output.x = ((color >> 24) & 0xFF) * rcp_255;
		output.y = ((color >> 16) & 0xFF) * rcp_255;
		output.z = ((color >> 8) & 0xFF) * rcp_255;
		output.w = ((color >> 0) & 0xFF) * rcp_255;
		return output;
	}

    inline int32_t RoundUp(float value)
    {
        return (int32_t)ceilf(value);
    }

	inline uint32_t DivideAndRoundUp(uint32_t nominator, uint32_t denominator)
	{
		return (uint32_t)ceilf((float)nominator / denominator);
	}

    Color MakeFromColorTemperature(float Temp);

    struct Halton
    {
        static constexpr int FloorConstExpr(const float val)
        {
            // casting to int truncates the value, which is floor(val) for positive values
            // but we have to substract 1 for negative values (unless val is already floored == recasted int val)
            const auto val_int = (int64_t)val;
            const float fval_int = (float)val_int;
            return (int)(val >= (float)0 ? fval_int : (val == fval_int ? val_int : val_int - (float)1));
        }

        constexpr float operator()(int index, int base) const
        {
            float f = 1;
            float r = 0;
            while (index > 0)
            {
                f /= base;
                r += f * (index % base);
                index = FloorConstExpr((float)index / base);
            }
            return r;
        }
    };

    template<uint32_t SIZE, uint32_t BASE>
    struct HaltonSequence
    {
        constexpr HaltonSequence() : Sequence{}
        {
            Halton generator;
            for (int i = 0; i < SIZE; ++i)
            {
                Sequence[i] = generator(i + 1, BASE);
            }
        }

        constexpr float operator[](int32_t index) const
        {
            return Sequence[index % SIZE];
        }

    private:
        float Sequence[SIZE];
    };
}