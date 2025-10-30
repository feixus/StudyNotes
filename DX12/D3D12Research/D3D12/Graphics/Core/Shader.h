#pragma once

class FileWatcher;

using ShaderBlob = ComPtr<IDxcBlob>;

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
	ShaderDefine(const char* pDefine) : Value(pDefine) {}
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
	Shader(const ShaderBlob& shaderBlob, ShaderType shaderType, const std::string& entryPoint, const std::vector<ShaderDefine>& defines)
		: ShaderBase(shaderBlob, defines), m_Type(shaderType), m_EntryPoint(entryPoint)
	{}

	inline ShaderType GetType() const { return m_Type; }
	inline const std::string& GetEntryPoint() const { return m_EntryPoint; }
	
private:
	ShaderType m_Type;
	std::string m_EntryPoint;
};

class ShaderLibrary : public ShaderBase
{
public:
	ShaderLibrary(const ShaderBlob& shaderBlob, const std::vector<ShaderDefine>& defines) : ShaderBase(shaderBlob, defines) {}
};

DECLARE_MULTICAST_DELEGATE(OnShaderRecompiled, const std::string&, Shader*, Shader*);
DECLARE_MULTICAST_DELEGATE(OnLibraryRecompiled, const std::string&, ShaderLibrary*, ShaderLibrary*);

class ShaderManager
{
public:
	ShaderManager(const std::string& shaderSourcePath, uint8_t shaderModelMajor, uint8_t shaderModelMinor);
	~ShaderManager();

	void ConditionallyReloadShaders();

	Shader* GetShader(const std::string& shaderPath, ShaderType shaderType, const std::string& entryPoint, const std::vector<ShaderDefine>& defines = {});
	ShaderLibrary* GetLibrary(const std::string& shaderPath, const std::vector<ShaderDefine>& defines = {});

	OnShaderRecompiled& OnShaderRecompiledEvent() { return m_OnShaderRecompiledEvent; }
	OnLibraryRecompiled& OnLibraryRecompiledEvent() { return m_OnLibraryRecompiledEvent; }

private:
	Shader* LoadShader(const std::string& shaderPath, ShaderType shaderType, const std::string& entryPoint, const std::vector<ShaderDefine>& defines = {});
	ShaderLibrary* LoadLibrary(const std::string& shaderPath, const std::vector<ShaderDefine>& defines = {});

#if SHADER_HASH_DEBUG
	using ShaderStringHash = std::string;
#else
	using ShaderStringHash = StringHash;
#endif

	void RecompileFromFileChange(const std::string& filePath);

	static bool ProcessSource(const std::string& sourcePath, const std::string& filePath, 
							  std::stringstream& output, std::vector<ShaderStringHash>& processedIncludes);

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

	std::string m_ShaderSourcePath;
	uint8_t m_ShaderModelMajor;
	uint8_t m_ShaderModelMinor;

	OnShaderRecompiled m_OnShaderRecompiledEvent;
	OnLibraryRecompiled m_OnLibraryRecompiledEvent;
};
