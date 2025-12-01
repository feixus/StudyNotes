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
	enum Flags
	{
		None = 0,
		Enabled = 1 << 0,
		Shadows = 1 << 1,
		Volumetric = 1 << 2,
		PointAttenuation = 1 << 3,
		DirectionalAttenuation = 1 << 4,

		PointLight = PointAttenuation,
		SpotLight = PointAttenuation | DirectionalAttenuation,
		DirectionalLight = None,
	};

	struct RenderData
	{
		Vector3 Position{Vector3::Zero};
		uint32_t Flags{0};
		Vector3 Direction{Vector3::Forward};
		uint32_t Colour{0xFFFFFFFF};
		Vector2 SpotlightAngles{Vector2::Zero};
		float Intensity{1.0f};
		float Range{1.0f};
		int32_t ShadowIndex{-1};
		float InvShadowSize{0};
		int LightTexture{-1};
	};

	Vector3 Position{Vector3::Zero};
	Vector3 Direction{Vector3::Forward};
	LightType Type{LightType::MAX};
	float UmbraAngleDegrees{0};
	float PenumbraAngleDegrees{0};
	Color Colour = Colors::White;
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
			.Flags = 0,
			.Direction = Direction,
			.Colour = Math::EncodeColor(Colour),
			.SpotlightAngles = Vector2(cos(PenumbraAngleDegrees * 0.5f * Math::DegreesToRadians), cos(UmbraAngleDegrees * 0.5f * Math::DegreesToRadians)),
			.Intensity = Intensity,
			.Range = Range,
			.ShadowIndex = CastShadows ? ShadowIndex : -1,
			.InvShadowSize = 1.0f / ShadowMapSize,
			.LightTexture = LightTexture,
		};

		if (VolumetricLighting)
		{
			data.Flags |= Flags::Volumetric;
		}
		if (Intensity > 0.0f)
		{
			data.Flags |= Flags::Enabled;
		}
		if (CastShadows)
		{
			data.Flags |= Flags::Shadows;
		}
		if (Type == LightType::Point)
		{
			data.Flags |= Flags::PointLight;
		}
		else if (Type == LightType::Spot)
		{
			data.Flags |= Flags::SpotLight;
		}
		else if (Type == LightType::Directional)
		{
			data.Flags |= Flags::DirectionalLight;
		}

		return data;
	}

	static Light Directional(const Vector3& position, const Vector3& direction, float intensity = 1.0f, const Vector4& color = Colors::White)
	{
		Light light{};
		light.Position = position;
		light.Direction = direction;
		light.Type = LightType::Directional;
		light.Intensity = intensity;
		light.Colour = color;
		return light;
	}

	static Light Point(const Vector3& position, float radius, float intensity = 1.0f, const Vector4& color = Colors::White)
	{
		Light light{};
		light.Position = position;
		light.Range = radius;
		light.Type = LightType::Point;
		light.Intensity = intensity;
		light.Colour = color;
		return light;
	}

	static Light Spot(const Vector3& position, float range, const Vector3& direction, float umbraAngleInDegrees = 60, float penumbraAngleInDegrees = 40, float intensity = 1.0f, const Vector4& color = Colors::White)
	{
		Light light{};
		light.Position = position;
		light.Range = range;
		light.Direction = direction;
		light.PenumbraAngleDegrees = penumbraAngleInDegrees;
		light.UmbraAngleDegrees = umbraAngleInDegrees;
		light.Type = LightType::Spot;
		light.Intensity = intensity;
		light.Colour = color;
		return light;
	}
};
