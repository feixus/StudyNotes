#include "stdafx.h"
#include "MathHelp.h"

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

    Matrix CreatePerspectiveMatrix(float FoV, float aspectRatio, float nearPlane, float farPlane)
    {
#if WORLD_RIGHT_HANDLED
        return XMMatrixPerspectiveFovRH(FoV, aspectRatio, nearPlane, farPlane);
#else
        return XMMatrixPerspectiveFovLH(FoV, aspectRatio, nearPlane, farPlane);
#endif
    }

    Matrix CreatePerspectiveOffCenterMatrix(float left, float right, float bottom, float top, float nearPlane, float farPlane)
    {
#if WORLD_RIGHT_HANDLED
        return XMMatrixPerspectiveOffCenterRH(left, right, bottom, top, nearPlane, farPlane);
#else
        return XMMatrixPerspectiveOffCenterLH(left, right, bottom, top, nearPlane, farPlane);
#endif
    }

    Matrix CreateOrthographicMatrix(float width, float height, float nearPlane, float farPlane)
    {
#if WORLD_RIGHT_HANDLED
        return XMMatrixOrthographicRH(width, height, nearPlane, farPlane);
#else
        return XMMatrixOrthographicLH(width, height, nearPlane, farPlane);
#endif
    }

    Matrix CreateOrthographicOffCenterMatrix(float left, float right, float bottom, float top, float nearPlane, float farPlane)
    {
#if WORLD_RIGHT_HANDLED
        return XMMatrixOrthographicOffCenterRH(left, right, bottom, top, nearPlane, farPlane);
#else
        return XMMatrixOrthographicOffCenterLH(left, right, bottom, top, nearPlane, farPlane);
#endif
    }

    void GetProjectionClipPlanes(const Matrix& projection, float& nearPlane, float& farPlane)
    {
        nearPlane = -projection._43 / projection._33;
        farPlane = nearPlane * projection._33 / (projection._33 - 1);
    }

	void ReverseZProjection(Matrix& projection)
	{
        float n, f;
        GetProjectionClipPlanes(projection, n, f);
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

    std::string ToBase(unsigned int number, unsigned int base)
    {
        std::stringstream nr;
        unsigned int count = 0;
        while (number != 0)
        {
            unsigned int mod = number % base;
            if (mod > 9)
            {
                nr << (char)('A' + mod - 10);
            }
            else
            {
                nr << mod;
            }
            number /= base;
            ++count;
        }

        for (; count <= 8; ++count)
        {
            nr << '0';
        }

        if (base == 2)
        {
            nr << "b0";
        }
        else if (base == 8)
        {
            nr << "o0";
        }
        else if (base == 16)
        {
            nr << "h0";
        }
        std::string result = nr.str();
        std::reverse(result.begin(), result.end());
        return result;
    }

    std::string ToBinaryString(unsigned int number)
    {
        return ToBase(number, 2);
    }

    std::string ToOctalString(unsigned int number)
    {
        return ToBase(number, 8);
    }

    std::string ToHexString(unsigned int number)
    {
        return ToBase(number, 16);
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
}