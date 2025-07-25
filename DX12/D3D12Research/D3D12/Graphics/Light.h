#pragma once

struct Light
{
	enum class Type : uint32_t
	{
		Directional,
		Point,
		Spot,
		MAX
	};

	Vector3 Position;
	int Enabled{true};
	Vector3 Direction;
	Type LightType{};
	Vector4 Color;
	float Range{1.f};
	float CosHalfAngle{0.f};
	float Attenuation{1.f};
	int32_t ShadowIndex{-1};

	static Light Directional(const Vector3& position, const Vector3& direction, float intensity = 1.0f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light light;
		light.Enabled = true;
		light.Position = position;
		light.Direction = direction;
		light.Color = Vector4(color.x, color.y, color.z, intensity);
		light.LightType = Type::Directional;
		return light;
	}

	static Light Point(const Vector3& position, float radius, float intensity = 1.0f, float attenuation = 0.5f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light light;
		light.Enabled = true;
		light.Position = position;
		light.Range = radius;
		light.Color = Vector4(color.x, color.y, color.z, intensity);
		light.LightType = Type::Point;
		light.Attenuation = attenuation;
		return light;
	}

	static Light Spot(const Vector3& position, float range, const Vector3& direction, float angleInDegrees = 60, float intensity = 1.0f, float attenuation = 0.5f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light light;
		light.Enabled = true;
		light.Position = position;
		light.Range = range;
		light.Direction = direction;
		light.CosHalfAngle = cos(angleInDegrees * 0.5f * Math::PI / 180.f);
		light.Color = Vector4(color.x, color.y, color.z, intensity);
		light.Attenuation = attenuation;
		light.LightType = Type::Spot;
		return light;
	}
};