#pragma once

class FileWatcher;

using ShaderBlob = ComPtr<ID3DBlob>;

#ifndef SHADER_HASH_DEBUG
#define SHADER_HASH_DEBUG 0
#endif

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

struct ShaderDefine
{
	ShaderDefine() = default;
	ShaderDefine(const std::string& define) : Value(define) {}
	ShaderDefine(const char* pDefine) : Value(pDefine) {}
	ShaderDefine(const char* pDefine, const char* pValue) : Value(Sprintf("%s=%s", pDefine, pValue)) {}
	ShaderDefine(const char* pDefine, const uint32_t value) : Value(Sprintf("%s=%d", pDefine, value)) {}
	std::string Value;
};

class ShaderBase
{
public:
	ShaderBase(const ShaderBlob& shaderBlob, const std::vector<ShaderDefine>& defines) 
		: m_pByteCode(shaderBlob), m_Defines(defines) {}

	void* GetByteCode() const;
	virtual ~ShaderBase() {};
	uint32_t GetByteCodeSize() const;
	const std::vector<ShaderDefine>& GetDefines() const { return m_Defines; }

protected:
	ShaderBlob m_pByteCode;
	std::vector<ShaderDefine> m_Defines;
};

class Shader : public ShaderBase
{
public:
	Shader(const ShaderBlob& shaderBlob, ShaderType shaderType, const char* pEntryPoint, const std::vector<ShaderDefine>& defines)
		: ShaderBase(shaderBlob, defines), m_Type(shaderType), m_pEntryPoint(pEntryPoint)
	{}

	inline ShaderType GetType() const { return m_Type; }
	inline const char* GetEntryPoint() const { return m_pEntryPoint; }
	
private:
	ShaderType m_Type;
	const char* m_pEntryPoint;
};

class ShaderLibrary : public ShaderBase
{
public:
	ShaderLibrary(const ShaderBlob& shaderBlob, const std::vector<ShaderDefine>& defines) : ShaderBase(shaderBlob, defines) {}
};

class ShaderManager
{
public:
	ShaderManager(uint8_t shaderModelMajor, uint8_t shaderModelMinor);
	~ShaderManager();

	void ConditionallyReloadShaders();
	void AddIncludeDir(const std::string& includeDir);

	Shader* GetShader(const char* pShaderPath, ShaderType shaderType, const char* pEntryPoint, const std::vector<ShaderDefine>& defines = {});
	ShaderLibrary* GetLibrary(const char* pShaderPath, const std::vector<ShaderDefine>& defines = {});

	DECLARE_MULTICAST_DELEGATE(OnShaderRecompiled, Shader*, Shader*);
	OnShaderRecompiled& OnShaderRecompiledEvent() { return m_OnShaderRecompiledEvent; }
	DECLARE_MULTICAST_DELEGATE(OnLibraryRecompiled, ShaderLibrary*, ShaderLibrary*);
	OnLibraryRecompiled& OnLibraryRecompiledEvent() { return m_OnLibraryRecompiledEvent; }

private:
#if SHADER_HASH_DEBUG
	using ShaderStringHash = std::string;
#else
	using ShaderStringHash = StringHash;
#endif

	ShaderStringHash GetEntryPointHash(const char* pEntryPoint, const std::vector<ShaderDefine>& defines);
	Shader* LoadShader(const char* pShaderPath, ShaderType shaderType, const char* pEntryPoint, const std::vector<ShaderDefine>& defines = {});
	ShaderLibrary* LoadLibrary(const char* pShaderPath, const std::vector<ShaderDefine>& defines = {});

	void RecompileFromFileChange(const std::string& filePath);

	std::vector<std::string> m_IncludeDirs;
	std::unique_ptr<FileWatcher> m_pFileWatcher;

	using ShaderPtr = std::unique_ptr<Shader>;
	using LibraryPtr = std::unique_ptr<ShaderLibrary>;

	std::list<ShaderPtr> m_Shaders;
	std::list<LibraryPtr> m_Libraries;

	std::unordered_map<ShaderStringHash, std::unordered_set<std::string>> m_IncludeDependencyMap;

	struct ShadersInFileMap
	{
		std::unordered_map<StringHash, Shader*> Shaders;
		std::unordered_map<StringHash, ShaderLibrary*> Libraries;
	};
	std::unordered_map<ShaderStringHash, ShadersInFileMap> m_FilepathToObjectMap;

	uint8_t m_ShaderModelMajor;
	uint8_t m_ShaderModelMinor;

	OnShaderRecompiled m_OnShaderRecompiledEvent;
	OnLibraryRecompiled m_OnLibraryRecompiledEvent;
};
