#pragma once

class ShaderCompiler
{
public:
	static bool CompileDxc(const char* pIdentifier, const char* pShaderSource, uint32_t shaderSourceSize,
					IDxcBlob** pOutput, const char* pEntryPoint = "", const char* pTarget = "",
					const std::vector<std::string>& defines = {});
	static bool CompileFxc(const char* pIdentifier, const char* pShaderSource, uint32_t shaderSourceSize,
					ID3DBlob** pOutput, const char* pEntryPoint = "", const char* pTarget = "",
					const std::vector<std::string>& defines = {});
};

class ShaderBase
{
public:
	void* GetByteCode() const;
	uint32_t GetByteCodeSize() const;
	const std::vector<std::string>& GetDependencies() const { return m_Dependencies; }

protected:
	static bool ProcessSource(const std::string& sourcePath, const std::string& filePath, std::stringstream& output, std::vector<StringHash>& processedIncludes, std::vector<std::string>& dependencies);

	std::vector<std::string> m_Dependencies;
	std::string m_Path;
	ComPtr<ID3DBlob> m_pByteCode;
};

class Shader : public ShaderBase
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

private:
	bool Compile(const char* pFilePath, Type shaderType, const char* pEntryPoint, char shaderModelMajor, char shaderModelMinor, const std::vector<std::string>& defines = {});
	
	static std::string GetShaderTarget(Type shaderType, char shaderModelMajor, char shaderModelMinor);

	Type m_Type{ Type::MAX };
};

class ShaderLibrary : public ShaderBase
{
public:
	ShaderLibrary(const char* pFilePath, const std::vector<std::string> defines = {});

private:
	static std::string GetShaderTarget(char shaderModelMajor, char shaderModelMinor);

	bool Compile(const char* pFilePath, char shaderModelMajor, char shaderModelMinor, const std::vector<std::string> defines = {});
};