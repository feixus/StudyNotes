#pragma once
#include "Graphics/Core/ShaderInterop.h"

enum class LightType
{
	Directional,
	Point,
	Spot,
	MAX
};

struct Light
{
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

	ShaderInterop::Light GetData() const
	{
		ShaderInterop::Light data = 
		{
			.Position = Position,
			.Color = Math::EncodeRGBA(Colour),
			.Direction = Direction,
			.Intensity = Intensity,
			.SpotlightAngles = Vector2(cos(PenumbraAngleDegrees * 0.5f * Math::DegreesToRadians), cos(UmbraAngleDegrees * 0.5f * Math::DegreesToRadians)),
			.Range = Range,
			.ShadowIndex = CastShadows ? ShadowIndex : -1,
			.InvShadowSize = 1.0f / ShadowMapSize,
			.LightTexture = LightTexture,
			.IsEnabled = Intensity > 0 ? static_cast<ShaderInterop::uint>(1) : 0,
			.IsSpot = Type == LightType::Spot,
			.IsPoint = Type == LightType::Point,
			.IsDirectional = Type == LightType::Directional,
			.IsVolumetric = VolumetricLighting,
			.CastShadows = CastShadows,
		};

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
