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

	Vector3 Position{Vector3::Zero};
	int Enabled{1};
	Vector3 Direction{Vector3::Forward};
	Type LightType{Type::MAX};
	Vector2 SpotlightAngles{Vector2::Zero};
	uint32_t Colour{0xFFFFFFFF};
	float Intensity{1.0f};
	float Range{1.0f};
	int32_t ShadowIndex{-1};

	static Light Directional(const Vector3& position, const Vector3& direction, float intensity = 1.0f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light light{};
		light.Enabled = true;
		light.Position = position;
		light.Direction = direction;
		light.LightType = Type::Directional;
		light.Intensity = intensity;
		light.Colour = Math::EncodeColor(color);
		return light;
	}

	static Light Point(const Vector3& position, float radius, float intensity = 1.0f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light light{};
		light.Enabled = true;
		light.Position = position;
		light.Range = radius;
		light.LightType = Type::Point;
		light.Intensity = intensity;
		light.Colour = Math::EncodeColor(color);
		return light;
	}

	static Light Spot(const Vector3& position, float range, const Vector3& direction, float umbraAngleInDegrees = 60, float penumbraAngleInDegrees = 40, float intensity = 1.0f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light light{};
		light.Enabled = true;
		light.Position = position;
		light.Range = range;
		light.Direction = direction;
		light.SpotlightAngles.x = cos(penumbraAngleInDegrees * 0.5f * Math::PI / 180.0f);
		light.SpotlightAngles.y = cos(umbraAngleInDegrees * 0.5f * Math::PI / 180.0f);
		light.Intensity = intensity;
		light.LightType = Type::Spot;
		light.Colour = Math::EncodeColor(color);
		return light;
	}
};