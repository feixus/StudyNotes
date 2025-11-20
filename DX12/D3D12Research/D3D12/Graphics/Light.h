#pragma once

enum class LightType
{
	Directional,
	Point,
	Spot,
	MAX
};

struct Light
{
	struct RenderData
	{
		Vector3 Position{Vector3::Zero};
		int Enabled{1};
		Vector3 Direction{Vector3::Forward};
		LightType Type{LightType::MAX};
		Vector2 SpotlightAngles{Vector2::Zero};
		uint32_t Colour{0xFFFFFFFF};
		float Intensity{1.0f};
		float Range{1.0f};
		int32_t ShadowIndex{-1};
		float InvShadowSize{0};
		int VolumetricLight{1};
		int LightTexture{-1};
		int pad[3];
	};

	Vector3 Position{Vector3::Zero};
	Vector3 Direction{Vector3::Forward};
	LightType Type{LightType::MAX};
	float UmbraAngle{0};
	float PenumbraAngle{0};
	Color Colour = Color(1, 1, 1, 1);
	float Intensity{1};
	float Range{1};

	int ShadowIndex{-1};
	int ShadowMapSize{512};
	bool CastShadows{false};
	bool VolumetricLighting{false};
	int LightTexture{-1};

	RenderData GetData() const
	{
		RenderData data = {
			.Position = Position,
			.Enabled = Intensity > 0,
			.Direction = Direction,
			.Type = Type,
			.SpotlightAngles = Vector2(cos(PenumbraAngle * 0.5f * Math::ToRadians), cos(UmbraAngle * 0.5f * Math::ToRadians)),
			.Colour = Math::EncodeColor(Colour),
			.Intensity = Intensity,
			.Range = Range,
			.ShadowIndex = CastShadows ? ShadowIndex : -1,
			.InvShadowSize = 1.0f / ShadowMapSize,
			.VolumetricLight = VolumetricLighting,
			.LightTexture = LightTexture,
		};
		return data;
	}

	static Light Directional(const Vector3& position, const Vector3& direction, float intensity = 1.0f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light light{};
		light.Position = position;
		light.Direction = direction;
		light.Type = LightType::Directional;
		light.Intensity = intensity;
		light.Colour = color;
		return light;
	}

	static Light Point(const Vector3& position, float radius, float intensity = 1.0f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light light{};
		light.Position = position;
		light.Range = radius;
		light.Type = LightType::Point;
		light.Intensity = intensity;
		light.Colour = color;
		return light;
	}

	static Light Spot(const Vector3& position, float range, const Vector3& direction, float umbraAngleInDegrees = 60, float penumbraAngleInDegrees = 40, float intensity = 1.0f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light light{};
		light.Position = position;
		light.Range = range;
		light.Direction = direction;
		light.PenumbraAngle = penumbraAngleInDegrees;
		light.UmbraAngle = umbraAngleInDegrees;
		light.Type = LightType::Spot;
		light.Intensity = intensity;
		light.Colour = color;
		return light;
	}
};
