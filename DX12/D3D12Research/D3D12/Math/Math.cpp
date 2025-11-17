#include "stdafx.h"
#include "Math.h"

namespace Math
{
    float RandomRange(float min, float max)
    {
        return min + (max - min) * rand() / (float)RAND_MAX;
    }

    int RandomRange(int min, int max)
    {
        return min + rand() % (max - min + 1);
    }

	// create left-handed DX style perspective matrix
	// FoV is vertical FoV in radius
    Matrix CreatePerspectiveMatrix(float FoV, float aspectRatio, float nearZ, float farZ)
    {
		Matrix m;
		float sinFoV, cosFov;
		DirectX::XMScalarSinCos(&sinFoV, &cosFov, FoV * 0.5f);

		float B = cosFov / sinFoV;
		float A = B / aspectRatio;
		float C = farZ / (farZ - nearZ);
		float D = 1.0f; // needs to be -1 for right hand
		float E = -nearZ * C;	// positive in right hand

		m.m[0][0] = A;    m.m[0][1] = 0.0f; m.m[0][2] = 0.0f; m.m[0][3] = 0.0f;
		m.m[1][0] = 0.0f; m.m[1][1] = B;    m.m[1][2] = 0.0f; m.m[1][3] = 0.0f;
		m.m[2][0] = 0.0f; m.m[2][1] = 0.0f; m.m[2][2] = C;    m.m[2][3] = D;
		m.m[3][0] = 0.0f; m.m[3][1] = 0.0f; m.m[3][2] = E;    m.m[3][3] = 0.0f;

		return m;
    }

	// create left-handed DX style perspective off center matrix
	// FoV is vertical FoV in radius
    Matrix CreatePerspectiveOffCenterMatrix(float left, float right, float bottom, float top, float nearZ, float farZ)
    {
		Matrix m;
		float near2 = nearZ * nearZ;
		float oneOverWidth = 1.0f / (right - left);
		float oneOVerHeight = 1.0f / (bottom - top);

		float A = near2 * oneOverWidth;
		float B = near2 * oneOVerHeight;
		float C = farZ / (farZ - nearZ);
		float D = 1.0f; // needs to be -1 for right handed
		float E = -nearZ * C; // positive in right handed

		float F = -(left + right) * oneOverWidth;	// positive in right handed
		float G = -(top + bottom) * oneOVerHeight;	// positive in right handed

		m.m[0][0] = A;    m.m[0][1] = 0.0f; m.m[0][2] = 0.0f; m.m[0][3] = 0.0f;
		m.m[1][0] = 0.0f; m.m[1][1] = B;    m.m[1][2] = 0.0f; m.m[1][3] = 0.0f;
		m.m[2][0] = F;    m.m[2][1] = G;    m.m[2][2] = C;    m.m[2][3] = D;
		m.m[3][0] = 0.0f; m.m[3][1] = 0.0f; m.m[3][2] = E;    m.m[3][3] = 0.0f;

		return m;
    }

    Matrix CreateOrthographicMatrix(float width, float height, float nearZ, float farZ)
    {
#if WORLD_RIGHT_HANDLED
        return XMMatrixOrthographicRH(width, height, nearZ, farZ);
#else
        return XMMatrixOrthographicLH(width, height, nearZ, farZ);
#endif
    }

    Matrix CreateOrthographicOffCenterMatrix(float left, float right, float bottom, float top, float nearZ, float farZ)
    {
#if WORLD_RIGHT_HANDLED
        return XMMatrixOrthographicOffCenterRH(left, right, bottom, top, nearZ, farZ);
#else
        return XMMatrixOrthographicOffCenterLH(left, right, bottom, top, nearZ, farZ);
#endif
    }

    Matrix CreateLookToMatrix(const Vector3& position, const Vector3& direction, const Vector3& up)
    {
#if WORLD_RIGHT_HANDLED
        return DirectX::XMMatrixLookToRH(position, direction, up);
#else
        return DirectX::XMMatrixLookToLH(position, direction, up);
#endif
    }

    void GetProjectionClipPlanes(const Matrix& projection, float& nearZ, float& farZ)
    {
        nearZ = -projection._43 / projection._33;
        farZ = nearZ * projection._33 / (projection._33 - 1);
    }

	void ReverseZProjection(Matrix& projection)
	{
        float n, f;
        GetProjectionClipPlanes(projection, n, f);
		std::swap(n, f);	// bounding frustum need noraml near/far clip plane
        projection._33 = f / (f - n);
        projection._43 = -projection._33 * n;
	}

	Vector3 ScaleFromMatrix(const Matrix& m)
    {
        return Vector3(
            sqrtf(m._11 * m._11 + m._21 * m._21 + m._31 * m._31),
            sqrtf(m._12 * m._12 + m._22 * m._22 + m._32 * m._32),
            sqrtf(m._13 * m._13 + m._23 * m._23 + m._33 * m._33)
        );
    }

    Quaternion LookRotation(const Vector3& direction)
    {
        Vector3 v;
        direction.Normalize(v);
        float pitch = asin(-v.y);
        float yaw = atan2(v.x, v.z);
        return Quaternion::CreateFromYawPitchRoll(yaw, pitch, 0);
    }

    std::string ToBase(unsigned int number, unsigned int base, bool addPrefix)
    {
        char buffer[16];
        memset(buffer, 0, 16);
        char* pCurrent = buffer;
        uint32_t count = 0;
        while (number != 0)
        {
            unsigned int mod = number % base;
            if (mod > 9)
            {
                *pCurrent += (char)('A' + mod - 10);
            }
            else
            {
                *pCurrent++ = '0' + (char)mod;
            }
            number /= base;
            ++count;
        }

        constexpr uint32_t minPadding = 8;
        for (; count <= minPadding; ++count)
        {
            *pCurrent++ = '0';
        }
        
        if (addPrefix)
        {
            if (base == 2)
            {
				*pCurrent++ = 'b';
				*pCurrent = '0';
            }
            else if (base == 8)
            {
				*pCurrent++ = 'c';
				*pCurrent = '0';
            }
            else if (base == 16)
            {
				*pCurrent++ = 'x';
				*pCurrent = '0';
            }
        }
        std::string result = buffer;
        std::reverse(result.begin(), result.end());
        return result;
    }

    Vector3 RandVector()
    {
        Matrix randomMatrix = XMMatrixRotationRollPitchYaw(RandomRange(-PI, PI), RandomRange(-PI, PI), RandomRange(-PI, PI));
        return Vector3::Transform(Vector3(1, 0, 0), randomMatrix);
    }

    Vector3 RandCircleVector()
    {
        float angle = RandomRange(-PI, PI);
        return Vector3(cos(angle), sin(angle), 0);
    }

	Color MakeFromColorTemperature(float Temp)
	{
		constexpr float MAX_TEMPERATURE = 15000.0f;
		constexpr float MIN_TEMPERATURE = 1000.0f;
        Temp = Clamp(Temp, MIN_TEMPERATURE, MAX_TEMPERATURE);

        //[Krystek85] Algorithm works in the CIE 1960 (UCS) space,
        float u = (0.860117757f + 1.54118254e-4f * Temp + 1.28641212e-7f * Temp * Temp) / (1.0f + 8.42420235e-4f * Temp + 7.08145166e-7f * Temp * Temp);
        float v = (0.317398726f + 4.22806221e-5f * Temp + 4.20481691e-8f * Temp * Temp) / (1.0f + 2.89741816e-5f * Temp + 1.61456053e-7f * Temp * Temp);

        // UCS to xyY
        float x = 3.0f * u / (2.0f * u - 8.0f * v + 4.0f);
        float y = 2.0f * v / (2.0f * u - 8.0f * v + 4.0f);
        float z = 1.0f - x - y;

        // xyY to XYZ
        float Y = 1.0f;
        float X = Y / y * x;
        float Z = Y / y * z;

        // XYZ to RGB
        float r = X *  3.2404542f + Y * -1.5371385f + Z * -0.4985314f;
        float g = X * -0.9692660f + Y *  1.8760108f + Z *  0.0415560f;
        float b = X *  0.0556434f + Y * -0.2040259f + Z *  1.0572252f;

        return Color(r, g, b);
	}
}
