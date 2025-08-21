#pragma once

#define __d3d12_h__
#include "External/SimpleMath/SimpleMath.h"

using BoundingBox = DirectX::BoundingBox;
using BoundingFrustum = DirectX::BoundingFrustum;
using Vector2 = DirectX::SimpleMath::Vector2;
using Vector3 = DirectX::SimpleMath::Vector3;
using Vector4 = DirectX::SimpleMath::Vector4;
using Matrix = DirectX::SimpleMath::Matrix;
using Quaternion = DirectX::SimpleMath::Quaternion;
using Color = DirectX::SimpleMath::Color;
using Ray = DirectX::SimpleMath::Ray;

struct IntVector2
{
	IntVector2() : x(0), y(0) {}
	IntVector2(int32_t x, int32_t y) : x(x), y(y) {}
	IntVector2(const Vector2& v) : x((int32_t)v.x), y((int32_t)v.y) {}

	int32_t x, y;
};

struct IntVector3
{
	IntVector3() : x(0), y(0), z(0) {}
	IntVector3(int32_t x, int32_t y, int32_t z) : x(x), y(y), z(z) {}
	IntVector3(const Vector3& v) : x((int32_t)v.x), y((int32_t)v.y), z((int32_t)v.z) {}

	int32_t x, y, z;
};

template<typename T>
struct RectT
{
	RectT() : Left(T()), Top(T()), Right(T()), Bottom(T()) {}

	RectT(const T left, const T top, const T right, const T bottom)
		: Left(left), Top(top), Right(right), Bottom(bottom) {}

	template<typename U>
	RectT(const RectT<U>& other)
		: Left(other.Left), Top(other.Top), Right(other.Right), Bottom(other.Bottom) {}

	T Left;
	T Top;
	T Right;
	T Bottom;

	T GetWidth() const { return Right - Left; }
	T GetHeight() const { return Bottom - Top; }
	T GetAspect() const
	{
		return GetWidth() / GetHeight();
	}

	RectT Scale(const float scale) const
	{
		return RectT(Left * scale, Top * scale, Right * scale, Bottom * scale);
	}

	RectT Scale(const float scaleX, const float scaleY) const
	{
		return RectT(Left * scaleX, Top * scaleY, Right * scaleX, Bottom * scaleY);
	}

	bool operator==(const RectT& other) const
	{
		return Left == other.Left && Top == other.Top &&
			Right == other.Right && Bottom == other.Bottom;
	}

	bool operator!=(const RectT& other) const
	{
		return !(*this == other);
	}

	static RectT ZERO()
	{
		return RectT(0, 0, 0, 0);
	}
};

using FloatRect = RectT<float>;
using IntRect = RectT<int>;
