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
	~Shader();

	inline Type GetType() const { return m_Type; }
	void* GetByteCode() const;
	uint32_t GetByteCodeSize() const;
	const std::vector<std::string>& GetDependencies() const { return m_Dependencies; }

	static void AddGlobalShaderDefine(const std::string& name, const std::string& value = "1");

private:
	bool ProcessSource(const std::string& filePath, std::stringstream& output, std::vector<StringHash>& processedIncludes, std::vector<std::string>& dependencies);

	bool Compile(const char* pFilePath, Type shaderType, const char* pEntryPoint, char shaderModelMajor, char shaderModelMinor, const std::vector<std::string>& defines = {});
	bool CompileDxc(const std::string& source, const char* pTarget, const char* pEntryPoint, const std::vector<std::string>& defines = {});
	bool CompileFxc(const std::string& source, const char* pTarget, const char* pEntryPoint, const std::vector<std::string>& defines = {});

	static std::string GetShaderTarget(Type shaderType, char shaderModelMajor, char shaderModelMinor);

	inline static std::vector<std::pair<std::string, std::string>> m_GlobalShaderDefines;

	std::vector<std::string> m_Dependencies;
	std::string m_Path;
	ComPtr<ID3DBlob> m_pByteCode;
	Type m_Type{ Type::MAX };
};
