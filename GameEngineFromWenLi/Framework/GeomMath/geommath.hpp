#pragma once
#include <cmath>
#include <iostream>
#include "include/CrossProduct.h"
#include "include/DotProduct.h"
#include "include/MulByElement.h"
#include "include/Normalize.h"
#include "include/Transform.h"
#include "include/Transpose.h"
#include "include/AddByElement.h"
#include "include/SubByElement.h"

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#ifndef TWO_PI
#define TWO_PI 3.14159265358979323846f * 2.0f
#endif 

// 列矩阵 按行存储

namespace My
{
	template<typename T, size_t SizeOfArray>
	constexpr size_t countof(T (&array)[SizeOfArray]) { return SizeOfArray; }

	template<typename T, size_t RowSize, size_t ColSize>
	constexpr size_t countof(T (&array)[RowSize][ColSize]) { return RowSize * ColSize; }


	//https://cplusplus.com/forum/general/72912/
    template<typename T, int ... Indexes>    
    class swizzle
    {
        float v[sizeof...(Indexes)];

	public:
		T& operator=(const T& rhs)
		{
			int indexes[] = { Indexes... };
			for (int i = 0; i < sizeof...(Indexes); i++) {
				v[indexes[i]] = rhs[i];
			}
			return *(T*)this;
		}

		operator T () const
		{
			return T( v[Indexes]... );
		}
    };

	template <typename T>
	struct Vector2Type
	{
		union {
			T data[2];
			struct { T x, y; };
			struct { T r, g; };
			struct { T u, v; };
			swizzle<Vector2Type<T>, 0, 1> xy;
			swizzle<Vector2Type<T>, 1, 0> yx;
		};

		Vector2Type<T>() { };
		Vector2Type<T>(const T& _v) : x(_v), y(_v) { };
		Vector2Type<T>(const T& _x, const T& _y) : x(_x), y(_y) { };

		operator T*() { return data; }
		operator const T*() const { return static_cast<const T*>(data); }
	};

	// using for template aliases only, both using and typedef can use for basic type/function pointer/complex type
	using Vector2f = Vector2Type<float>;

	template <typename T>
	struct Vector3Type
	{
		union {
			T data[3];
			struct { T x, y, z; };
			struct { T r, g, b; };
			swizzle<Vector2Type<T>, 0, 1> xy;
			swizzle<Vector2Type<T>, 1, 0> yx;
			swizzle<Vector2Type<T>, 0, 2> xz;
			swizzle<Vector2Type<T>, 2, 0> zx;
			swizzle<Vector2Type<T>, 1, 2> yz;
			swizzle<Vector2Type<T>, 2, 1> zy;
			swizzle<Vector3Type<T>, 0, 1, 2> xyz;
			swizzle<Vector3Type<T>, 1, 0, 2> yxz;
			swizzle<Vector3Type<T>, 0, 2, 1> xzy;
			swizzle<Vector3Type<T>, 2, 0, 1> zxy;
			swizzle<Vector3Type<T>, 1, 2, 0> yzx;
			swizzle<Vector3Type<T>, 2, 1, 0> zyx;
		};

		Vector3Type() {};
		Vector3Type(const T& _v) : x(_v), y(_v), z(_v) {};
		Vector3Type(const T& _x, const T& _y, const T& _z) : x(_x), y(_y), z(_z) {};

		operator T*() { return data; }
		operator const T*() const { return static_cast<const T*>(data); }
	};

	using Vector3f = Vector3Type<float>;

	template <typename T>
	struct Vector4Type
	{
		union {
			T data[4];
			struct { T x, y, z, w; };
			struct { T r, g, b, a; };
			Vector3Type<T> xyz;
			swizzle<Vector3Type<T>, 0, 2, 1> xzy;
			swizzle<Vector3Type<T>, 1, 0, 2> yxz;
			swizzle<Vector3Type<T>, 1, 2, 0> yzx;
			swizzle<Vector3Type<T>, 2, 0, 1> zxy;
			swizzle<Vector3Type<T>, 2, 1, 0> zyx;
			swizzle<Vector4Type<T>, 2, 1, 0, 3> bgra;
		};

		Vector4Type() {};
		Vector4Type(const T& _v) : x(_v), y(_v), z(_v), w(_v) {};
		Vector4Type(const T& _x, const T& _y, const T& _z, const T& _w) : x(_x), y(_y), z(_z), w(_w) {};

		operator T*() { return data; }
		operator const T*() const { return static_cast<const T*>(data); }
	};

	using Vector4f = Vector4Type<float>;
	// unsigned normalized 8-bit integer
	using R8G8B8A8Unorm = Vector4Type<uint8_t>;

	template <template <typename> class TT, typename T>
	std::ostream& operator<<(std::ostream& out, TT<T> vector)
	{
		out << "(";
		for (int i = 0; i < countof(vector.data); i++) {
			out << vector.data[i] << ((i == countof(vector.data) - 1) ? ' ' : ',');
		}
		out << ")\n";

		return out;
	}

	template <template <typename> class TT, typename T>
	void VectorAdd(TT<T>& result, const TT<T>& vec1, const TT<T>& vec2)
	{
		ispc::AddByElement(vec1, vec2, result, countof(result.data));
	}

	template <template <typename> class TT, typename T>
	TT<T> operator+(const TT<T>& vec1, const TT<T>& vec2)
	{
		TT<T> result;
		VectorAdd(result, vec1, vec2);

		return result;
	}

	template <template <typename> class TT, typename T>
	void VectorSub(TT<T>& result, const TT<T>& vec1, const TT<T>& vec2)
	{
		ispc::SubByElement(vec1, vec2, result, countof(result.data));
	}

	template <template <typename> class TT, typename T>
	TT<T> operator-(const TT<T>& vec1, const TT<T>& vec2)
	{
		TT<T> result;
		VectorSub(result, vec1, vec2);

		return result;
	}

	template <template <typename> class TT, typename T>
	inline void CrossProduct(TT<T>& result, const TT<T>& vec1, const TT<T>& vec2)
	{
		ispc::CrossProduct(vec1, vec2, result);
	}

	template <template <typename> class TT, typename T>
	inline void DotProduct(T& result, const TT<T>& vec1, const TT<T>& vec2)
	{
		ispc::DotProduct(vec1, vec2, &result, countof(vec1.data));
	}

	template <typename T>
	inline void MulByElement(T& result, const T& a, const T& b)
	{
		ispc::MulByElement(a, b, result, countof(result.data));
	}

	// matrix

	template <typename T, int ROWS, int COLS>
	struct Matrix
	{
		union {
			T data[ROWS][COLS];
		};

		auto operator[](int row_index) {
			return data[row_index];
		}

		const auto operator[](int row_index) const {
			return data[row_index];
		}

		operator float*() { return &data[0][0]; }
		operator const float*() const { return static_cast<const float*>(&data[0][0]); }
	};

	using Matrix4X4f = Matrix<float, 4, 4>;

	template <typename T, int ROWS, int COLS>
	std::ostream& operator<<(std::ostream& out, Matrix<T, ROWS, COLS> matrix)
	{
		out << std::endl;
		for (int i = 0; i < ROWS; i++) {
			for (int j = 0; j < COLS; j++) {
				out << matrix.data[i][j] << ((j == COLS - 1) ? '\n' : ',');
			}
		}
		out << std::endl;

		return out;
	}

	template <typename T, int ROWS, int COLS>
	void MatrixAdd(Matrix<T, ROWS, COLS>& result, const Matrix<T, ROWS, COLS>& matrix1, const Matrix<T, ROWS, COLS>& matrix2)
	{
		ispc::AddByElement(matrix1, matrix2, result, countof(result.data));
	}

	template <typename T, int ROWS, int COLS>
	Matrix<T, ROWS, COLS> operator+(const Matrix<T, ROWS, COLS>& matrix1, const Matrix<T, ROWS, COLS>& matrix2)
	{
		Matrix<T, ROWS, COLS> result;
		MatrixAdd(result, matrix1, matrix2);

		return result;
	}

	template <typename T, int ROWS, int COLS>
	void MatrixSub(Matrix<T, ROWS, COLS>& result, const Matrix<T, ROWS, COLS>& matrix1, const Matrix<T, ROWS, COLS>& matrix2)
	{
		ispc::SubByElement(matrix1, matrix2, result, countof(result.data));
	}

	template <typename T, int ROWS, int COLS>
	Matrix<T, ROWS, COLS> operator-(const Matrix<T, ROWS, COLS>& matrix1, const Matrix<T, ROWS, COLS>& matrix2)
	{
		Matrix<T, ROWS, COLS> result;
		MatrixSub(result, matrix1, matrix2);

		return result;
	}

	template <typename T, int Da, int Db, int Dc>
	void MatrixMultiply(Matrix<T, Da, Dc>& result, const Matrix<T, Da, Db>& matrix1, const Matrix<T, Dc, Db>& matrix2)
	{
		Matrix<T, Dc, Db> matrix2_transpose;
		Transpose(matrix2_transpose, matrix2);
		for (int i = 0; i < Da; i++) {
			for (int j = 0; j < Dc; j++) {
				ispc::DotProduct(matrix1[i], matrix2_transpose[j], &result[i][j], Db);
			}
		}
	}

	template <typename T, int ROWS, int COLS>
	Matrix<T, ROWS, COLS> operator*(const Matrix<T, ROWS, COLS>& matrix1, const Matrix<T, ROWS, COLS>& matrix2)
	{
		Matrix<T, ROWS, COLS> result;
		MatrixMultiply(result, matrix1, matrix2);

		return result;
	}

	template <template <typename, int, int> class TT, typename T, int ROWS, int COLS>
	inline void Transpose(TT<T, ROWS, COLS>& result, const TT<T, ROWS, COLS>& matrix1)
	{
		ispc::Transpose(matrix1, result, ROWS, COLS);
	}

	template <typename T>
	inline void Normalize(T& result)
	{
		ispc::Normalize(result, countof(result.data));
	}

	void MatrixRotationYawPitchRoll(Matrix4X4f& matrix, const float yaw, const float pitch, const float roll)
	{
		float cYaw, cPitch, cRoll, sYaw, sPitch, sRoll;

		cYaw = cosf(yaw);
		cPitch = cosf(pitch);
		cRoll = cosf(roll);

		sYaw = sinf(yaw);
		sPitch = sinf(pitch);
		sRoll = sinf(roll);

		Matrix4X4f tmp = {{{
			{ (cRoll * cYaw) + (sRoll * sPitch * sYaw), (sRoll * cPitch), (cRoll * -sYaw) + (sRoll * sPitch * cYaw), 0.0f },
			{ (-sRoll * cYaw) + (cRoll * sPitch * sYaw), (cRoll * cPitch), (sRoll * sYaw) + (cRoll * sPitch * cYaw), 0.0f },
			{ (cPitch * sYaw), -sPitch, (cPitch * cYaw), 0.0f },
			{ 0.0f, 0.0f, 0.0f, 1.0f }
		}}};
		
		matrix = tmp;
	}

	void TransformCoord(Vector3f& vector, const Matrix4X4f& matrix)
	{
		ispc::Transform(vector, matrix);
	}

	void Transform(Vector4f& vector, const Matrix4X4f& matrix)
	{
		ispc::Transform(vector, matrix);
	}

	void BuildViewMatrix(Matrix4X4f& result, const Vector3f position, const Vector3f lookAt, const Vector3f up)
	{
		Vector3f zAxis, xAxis, yAxis;
		float result1, result2, result3;

		// zAxis = normal(lookAt - position)
		zAxis = lookAt - position;
		Normalize(zAxis);
		
		// xAxis = normal(cross(up, zAxis))
		CrossProduct(xAxis, up, zAxis);
		Normalize(xAxis);

		// yAxis = cross(zAxis, xAxis)
		CrossProduct(yAxis, zAxis, xAxis);

		// -dot(xAxis, position)
		DotProduct(result1, xAxis, position);
		result1 = -result1;

		// -dot(yaxis, eye)
		DotProduct(result2, yAxis, position);
		result2 = - result2;

		// -dot(zaxis, eye)
		DotProduct(result3, zAxis, position);
		result3 = -result3;
	
		// Set the computed values in the view matrix.
		Matrix4X4f tmp = {{{
			{ xAxis.x, yAxis.x, zAxis.x, 0.0f },
            { xAxis.y, yAxis.y, zAxis.y, 0.0f },
            { xAxis.z, yAxis.z, zAxis.z, 0.0f },
            { result1, result2, result3, 1.0f }
		}}};

		result = tmp;
	}

	void BuildIdentityMatrix(Matrix4X4f& matrix)
	{
		Matrix4X4f identity = {{{
            { 1.0f, 0.0f, 0.0f, 0.0f},
            { 0.0f, 1.0f, 0.0f, 0.0f},
            { 0.0f, 0.0f, 1.0f, 0.0f},
            { 0.0f, 0.0f, 0.0f, 1.0f}
        }}};

        matrix = identity;
	}

	void BuildPerspectiveFovLHMatrix(Matrix4X4f& matrix, const float fieldOfView, const float screenAspect, const float screenNear, const float screenDepth)
	{
		Matrix4X4f perspective = {{{
			{ 1.0f / (screenAspect * tan(fieldOfView * 0.5f)), 0.0f, 0.0f, 0.0f },
			{ 0.0f, 1.0f / tan(fieldOfView * 0.5f), 0.0f, 0.0f },
			{ 0.0f, 0.0f, screenDepth / (screenDepth - screenNear), 0.0f },
			{0.0f, 0.0f, (-screenNear * screenDepth) / (screenDepth - screenNear), 0.0f}
		}}};

		matrix = perspective;
	}

	void MatrixRotationX(Matrix4X4f& matrix, const float angle)
	{
		float c = cosf(angle), s = sinf(angle);
	
		Matrix4X4f rotationX = {{{
            { 1.0f, 0.0f, 0.0f, 0.0f},
            { 0.0f,    c,    s, 0.0f},
            { 0.0f,   -s,    c, 0.0f},
            { 0.0f, 0.0f, 0.0f, 1.0f}
        }}};

        matrix = rotationX;
	}

	void MatrixRotationY(Matrix4X4f& matrix, const float angle)
	{
		float c = cosf(angle), s = sinf(angle);
	
		Matrix4X4f rotationY = {{{
            {    c, 0.0f,   -s, 0.0f},
            { 0.0f, 1.0f, 0.0f, 0.0f},
            {    s, 0.0f,    c, 0.0f},
            { 0.0f, 0.0f, 0.0f, 1.0f}
        }}};

        matrix = rotationY;
	}

	void MatrixRotationZ(Matrix4X4f& matrix, const float angle)
	{
		float c = cosf(angle), s = sinf(angle);
	
		Matrix4X4f rotationZ = {{{
            {    c,   s,  0.0f, 0.0f},
            {   -s,   c,  0.0f, 0.0f},
            { 0.0f, 0.0f, 1.0f, 0.0f},
            { 0.0f, 0.0f, 0.0f, 1.0f}
        }}};

        matrix = rotationZ;
	}

	void MatrixTranslation(Matrix4X4f& matrix, const float x, const float y, const float z)
	{
		Matrix4X4f translation = {{{
            { 1.0f, 0.0f, 0.0f, 0.0f},
            { 0.0f, 1.0f, 0.0f, 0.0f},
            { 0.0f, 0.0f, 1.0f, 0.0f},
            {    x,    y,    z, 1.0f}
        }}};

        matrix = translation;
	}
}