#pragma once

#pragma pack(push)
#pragma pack(16)

struct Light
{
	Vector3 Position;
	int Enabled{true};
	Vector3 Direction;
	uint32_t Type{0};
	float Intensity{1.0f};
	float Range{1.0f};
	float SpotLightAngle{0.0f};
	float Attenuation{1.0f};
	Vector4 Color;

	static Light Directional(const Vector3& position, const Vector3& direction, float intensity = 1.0f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light light;
		light.Enabled = true;
		light.Position = position;
		light.Direction = direction;
		light.Intensity = intensity;
		light.Color = color;
		light.Type = 0;
		return light;
	}

	static Light Point(const Vector3& position, float radius, float intensity = 1.0f, float attenuation = 0.5f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light light;
		light.Enabled = true;
		light.Position = position;
		light.Range = radius;
		light.Intensity = intensity;
		light.Color = color;
		light.Type = 1;
		light.Attenuation = attenuation;
		return light;
	}

	static Light Cone(const Vector3& position, float range, const Vector3& direction, float angle = XM_PIDIV4, float intensity = 1.0f, float attenuation = 0.5f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light light;
		light.Enabled = true;
		light.Position = position;
		light.Range = range;
		light.Direction = direction;
		light.SpotLightAngle = angle;
		light.Intensity = intensity;
		light.Attenuation = attenuation;
		light.Color = color;
		light.Type = 2;
		return light;
	}
};

#pragma pack(pop)
