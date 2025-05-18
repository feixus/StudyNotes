#pragma once

class Shader
{
public:

	enum class Type : uint8_t
	{
		VertexShader,
		PixelShader,
		GeometryShader,
		ComputeShader,
		MAX
	};

	Shader(const char* pFilePath, Type shaderType, const char* pEntryPoint, const std::vector<std::string> defines = {});

	inline Type GetType() const { return m_Type; }
	inline void* GetByteCode() const { return m_pByteCode->GetBufferPointer(); }
	inline uint32_t GetByteCodeSize() const { return (uint32_t)m_pByteCode->GetBufferSize(); }

	static void AddGlobalShaderDefine(const std::string& name, const std::string& value = "1");

private:
	inline static std::vector<std::pair<std::string, std::string>> m_GlobalShaderDefines;

	ComPtr<ID3DBlob> m_pByteCode;
	Type m_Type{ Type::MAX };
};
