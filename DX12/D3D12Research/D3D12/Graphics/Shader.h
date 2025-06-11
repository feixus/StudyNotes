#pragma once

#include <dxcapi.h>

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
	~Shader();

	inline Type GetType() const { return m_Type; }
	void* GetByteCode() const;
	uint32_t GetByteCodeSize() const;

	static void AddGlobalShaderDefine(const std::string& name, const std::string& value = "1");

private:
	bool ProcessSource(const std::string& filePath, std::stringstream& output, std::vector<StringHash>& processedIncludes, std::vector<std::string>& dependencies);

	inline static std::vector<std::pair<std::string, std::string>> m_GlobalShaderDefines;

	std::string m_Path;
	ComPtr<ID3DBlob> m_pByteCode;
	ComPtr<IDxcBlob> m_pByteCodeDxc;
	Type m_Type{ Type::MAX };
};
