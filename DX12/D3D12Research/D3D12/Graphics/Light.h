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
	Vector2 SpotlightAngles;
	uint32_t Colour;
	float Intensity;
	float Range;
	int32_t ShadowIndex{-1};

	void SetColor(const Color& c)
	{
		Colour = (uint32_t)(c.x * 255) << 24 | (uint32_t)(c.y * 255) << 16 | 
				 (uint32_t)(c.z * 255) << 8 | (uint32_t)(c.w * 255) << 0;
	}

	static Light Directional(const Vector3& position, const Vector3& direction, float intensity = 1.0f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light light;
		light.Enabled = true;
		light.Position = position;
		light.Direction = direction;
		light.LightType = Type::Directional;
		light.Intensity = intensity;
		light.SetColor(color);
		return light;
	}

	static Light Point(const Vector3& position, float radius, float intensity = 1.0f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light light;
		light.Enabled = true;
		light.Position = position;
		light.Range = radius;
		light.LightType = Type::Point;
		light.Intensity = intensity;
		light.SetColor(color);
		return light;
	}

	static Light Spot(const Vector3& position, float range, const Vector3& direction, float umbraAngleInDegrees = 60, float penumbraAngleInDegrees = 40, float intensity = 1.0f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light light;
		light.Enabled = true;
		light.Position = position;
		light.Range = range;
		light.Direction = direction;
		light.SpotlightAngles.x = cos(penumbraAngleInDegrees * 0.5f * Math::PI / 180.0f);
		light.SpotlightAngles.y = cos(umbraAngleInDegrees * 0.5f * Math::PI / 180.0f);
		light.Intensity = intensity;
		light.LightType = Type::Spot;
		light.SetColor(color);
		return light;
	}
};