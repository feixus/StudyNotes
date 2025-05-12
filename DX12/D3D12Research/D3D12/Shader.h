#pragma once

struct ShaderParameter
{
	std::string Name;
	uint32_t Offset;
	uint32_t Size;
};

class Shader
{
public:

	enum class Type : uint8_t
	{
		VertexShader,
		PixelShader,
		ComputeShader,
		MAX
	};

	bool Load(const char* pFilePath, Type shaderType, const char* pEntryPoint, const std::vector<std::string> defines = {});

	inline Type GetType() const { return m_Type; }
	inline void* GetByteCode() const { return m_pByteCode->GetBufferPointer(); }
	inline uint32_t GetByteCodeSize() const { return (uint32_t)m_pByteCode->GetBufferSize(); }

	const ShaderParameter& GetShaderParameter(const std::string& name) const { return m_Parameters.at(name); }

private:
	std::map<std::string, ShaderParameter> m_Parameters;

	ComPtr<ID3DBlob> m_pByteCode;
	Type m_Type{ Type::MAX };
};
