#pragma once

enum class ShaderType : uint8_t
{
	Vertex,
	Pixel,
	Hull,
	Domain,
	Geometry,
	Compute,
	Mesh,
	Amplification,
	MAX
};

class ShaderBase
{
public:
	virtual ~ShaderBase() = default;

	void* GetByteCode() const;
	uint32_t GetByteCodeSize() const;
	const std::vector<std::string>& GetDependencies() const { return m_Dependencies; }

protected:
	static bool ProcessSource(const std::string& sourcePath, const std::string& filePath, std::stringstream& output, std::vector<StringHash>& processedIncludes, std::vector<std::string>& dependencies);

	std::vector<std::string> m_Dependencies;
	std::string m_Path;
	ComPtr<IDxcBlob> m_pByteCode;
};

class Shader : public ShaderBase
{
public:
	Shader(const char* pFilePath, ShaderType shaderType, const char* pEntryPoint, const std::vector<std::string> defines = {});

	inline ShaderType GetType() const { return m_Type; }

private:
	bool Compile(const char* pFilePath, ShaderType shaderType, const char* pEntryPoint, char shaderModelMajor, char shaderModelMinor, const std::vector<std::string>& defines = {});
	
	ShaderType m_Type{ ShaderType::MAX };
};

class ShaderLibrary : public ShaderBase
{
public:
	ShaderLibrary(const char* pFilePath, const std::vector<std::string> defines = {});

private:
	bool Compile(const char* pFilePath, char shaderModelMajor, char shaderModelMinor, const std::vector<std::string> defines = {});
};