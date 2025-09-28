#pragma once

enum class ShaderType
{
	Vertex,
	Pixel,
	Geometry,
	Hull,
	Domain,
	Mesh,
	Amplification,
	Compute,
	MAX,
};

class ShaderBase
{
public:
	void* GetByteCode() const;
	virtual ~ShaderBase();
	uint32_t GetByteCodeSize() const;
	const std::vector<std::string>& GetDependencies() const { return m_Dependencies; }

protected:
	static bool ProcessSource(const std::string& sourcePath, const std::string& filePath, std::stringstream& output, std::vector<StringHash>& processedIncludes, std::vector<std::string>& dependencies);
	std::vector<std::string> m_Dependencies;
	std::string m_Path;
	ComPtr<struct IDxcBlob> m_pByteCode = nullptr;
};

class Shader : public ShaderBase
{
public:
	Shader(const char* pFilePath, ShaderType shaderType, const char* pEntryPoint, const std::vector<std::string> defines = {});
	inline ShaderType GetType() const { return m_Type; }

private:
	bool Compile(const char* pFilePath, ShaderType shaderType, const char* pEntryPoint, uint32_t shaderModelMajor, uint32_t shaderModelMinor, const std::vector<std::string> defines = {});
	ShaderType m_Type;
};

class ShaderLibrary : public ShaderBase
{
public:
	ShaderLibrary(const char* pFilePath, const std::vector<std::string> defines = {});
private:
	bool Compile(const char* pFilePath, uint32_t shaderModelMajor, uint32_t shaderModelMinor, const std::vector<std::string> defines = {});
};