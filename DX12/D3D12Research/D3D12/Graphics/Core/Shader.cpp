#include "stdafx.h"
#include "Shader.h"
#include "Core/Paths.h"
#include "Core/CommandLine.h"
#include "Core/FileWatcher.h"
#include <D3Dcompiler.h>

#ifndef USE_SHADER_LINE_DIRECTIVE
#define USE_SHADER_LINE_DIRECTIVE 1
#endif

namespace ShaderCompiler
{
	constexpr const char* pShaderSymbolsPath = "_Temp/ShaderSymbols/";

	struct CompileResult
	{
		bool Success{false};
		std::string ErrorMsg;
		std::string DebugPath;
		ShaderBlob pBlob;
		ShaderBlob pSymbolsBlob;
		ComPtr<IUnknown> pReflection;
	};

	constexpr const char* GetShaderTarget(ShaderType t)
	{
		switch (t)
		{
		case ShaderType::Vertex:		return "vs";
		case ShaderType::Pixel:			return "ps";
		case ShaderType::Geometry:		return "gs";
		case ShaderType::Compute:		return "cs";
		case ShaderType::Hull:			return "hs";
		case ShaderType::Domain:		return "ds";
		case ShaderType::Mesh:			return "ms";
		case ShaderType::Amplification: return "as";
		default: noEntry();				return "";
		}
	}

	CompileResult CompileDxc(const char* pIdentifier, const char* pShaderSource, uint32_t shaderSourceSize, const char* pEntryPoint, const char* pTarget, const std::vector<std::string>& defines)
	{
		static ComPtr<IDxcUtils> pUtils;
		static ComPtr<IDxcCompiler3> pCompiler;
		static ComPtr<IDxcValidator> pValidator;
		if (!pUtils)
		{
			VERIFY_HR(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(pUtils.GetAddressOf())));
			VERIFY_HR(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(pCompiler.GetAddressOf())));
			VERIFY_HR(DxcCreateInstance(CLSID_DxcValidator, IID_PPV_ARGS(pValidator.GetAddressOf())));
		}

		ComPtr<IDxcBlobEncoding> pSource;
		VERIFY_HR(pUtils->CreateBlob(pShaderSource, shaderSourceSize, CP_UTF8, pSource.GetAddressOf()));

		bool debugShaders = CommandLine::GetBool("debugshaders");

		std::vector<std::wstring> wDefines;
		for (const std::string& define : defines)
		{
			wDefines.push_back(MULTIBYTE_TO_UNICODE(define.c_str()));
		}

		std::vector<LPCWSTR> arguments;
		arguments.reserve(20);

		arguments.push_back(L"-E");
		arguments.push_back(MULTIBYTE_TO_UNICODE(pEntryPoint));

		arguments.push_back(L"-T");
		arguments.push_back(MULTIBYTE_TO_UNICODE(pTarget));
		arguments.push_back(L"-all_resources_bound");

		if (debugShaders)
		{
			arguments.push_back(DXC_ARG_SKIP_OPTIMIZATIONS);
			arguments.push_back(L"-Qembed_debug");
		}
		else
		{
			arguments.push_back(DXC_ARG_OPTIMIZATION_LEVEL3);
			arguments.push_back(L"-Qstrip_debug");
			arguments.push_back(L"/Fd");
			arguments.push_back(MULTIBYTE_TO_UNICODE(pShaderSymbolsPath));
			arguments.push_back(L"-Qstrip_reflect");
		}

		arguments.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);
		arguments.push_back(DXC_ARG_DEBUG);
		arguments.push_back(DXC_ARG_PACK_MATRIX_ROW_MAJOR);

		for (size_t i = 0; i < wDefines.size(); ++i)
		{
			arguments.push_back(L"-D");
			arguments.push_back(wDefines[i].c_str());
		}

		DxcBuffer sourceBuffer;
		sourceBuffer.Ptr = pSource->GetBufferPointer();
		sourceBuffer.Size = pSource->GetBufferSize();
		sourceBuffer.Encoding = 0;

		ComPtr<IDxcResult> pCompileResult;
		VERIFY_HR(pCompiler->Compile(&sourceBuffer, arguments.data(), (uint32_t)arguments.size(), nullptr, IID_PPV_ARGS(pCompileResult.GetAddressOf())));

		CompileResult result;

		ComPtr<IDxcBlobUtf8> pErrors;
		pCompileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(pErrors.GetAddressOf()), nullptr);
		if (pErrors && pErrors->GetStringLength() > 0)
		{
			result.Success = false;
			result.ErrorMsg = (char*)pErrors->GetBufferPointer();
			return result;
		}

		//Shader object
		{
			VERIFY_HR(pCompileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(result.pBlob.GetAddressOf()), nullptr));
		}

		//Validation
		{
			ComPtr<IDxcOperationResult> pResult;
			VERIFY_HR(pValidator->Validate(*result.pBlob.GetAddressOf(), DxcValidatorFlags_InPlaceEdit, pResult.GetAddressOf()));
			HRESULT validationResult;
			pResult->GetStatus(&validationResult);
			if (validationResult != S_OK)
			{
				ComPtr<IDxcBlobEncoding> pPrintBlob;
				ComPtr<IDxcBlobUtf8> pPrintBlobUtf8;
				pResult->GetErrorBuffer(pPrintBlob.GetAddressOf());
				pUtils->GetBlobAsUtf8(pPrintBlob.Get(), pPrintBlobUtf8.GetAddressOf());

				result.Success = false;
				result.ErrorMsg = (char*)pPrintBlobUtf8->GetBufferPointer();
				return result;
			}
		}

		result.Success = true;

		//Symbols
		{
			ComPtr<IDxcBlobUtf16> pDebugDataPath;
			pCompileResult->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(result.pSymbolsBlob.GetAddressOf()), pDebugDataPath.GetAddressOf());
			std::stringstream pathStream;
			pathStream << pShaderSymbolsPath << UNICODE_TO_MULTIBYTE((wchar_t*)pDebugDataPath->GetBufferPointer());
			result.DebugPath = pathStream.str();
		}

		//Reflection
		{
			ComPtr<IDxcBlob> pReflectionData;
			pCompileResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(pReflectionData.GetAddressOf()), nullptr);
			DxcBuffer reflectionBuffer;
			reflectionBuffer.Ptr = pReflectionData->GetBufferPointer();
			reflectionBuffer.Size = pReflectionData->GetBufferSize();
			reflectionBuffer.Encoding = 0;

			VERIFY_HR(pUtils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(result.pReflection.GetAddressOf())));
		}

		return result;
	}

	CompileResult CompileFxc(const char* pIdentifier, const char* pShaderSource, uint32_t shaderSourceSize, const char* pEntryPoint, const char* pTarget, const std::vector<std::string>& defines)
	{
		bool debugShaders = CommandLine::GetBool("debugshaders");

		uint32_t compileFlags = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

		if (debugShaders)
		{
			// Enable better shader debugging with the graphics debugging tools.
			compileFlags |= D3DCOMPILE_DEBUG;
			compileFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
			compileFlags |= D3DCOMPILE_PREFER_FLOW_CONTROL;
		}
		else
		{
			compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
		}

		std::vector<std::pair<std::string, std::string>> defineValues(defines.size());
		std::vector<D3D_SHADER_MACRO> shaderDefines;
		for (size_t i = 0; i < defines.size(); ++i)
		{
			D3D_SHADER_MACRO m;
			const std::string& define = defines[i];
			defineValues[i] = std::make_pair<std::string, std::string>(define.substr(0, define.find('=')), define.substr(define.find('=') + 1));
			m.Name = defineValues[i].first.c_str();
			m.Definition = defineValues[i].second.c_str();
			shaderDefines.push_back(m);
		}

		D3D_SHADER_MACRO endMacro;
		endMacro.Name = nullptr;
		endMacro.Definition = nullptr;
		shaderDefines.push_back(endMacro);

		CompileResult result;

		ComPtr<ID3DBlob> pErrorBlob;
		D3DCompile(pShaderSource, shaderSourceSize, pIdentifier, 
			       shaderDefines.data(), nullptr, pEntryPoint, 
				   pTarget, compileFlags, 0, 
				   (ID3DBlob**)result.pBlob.GetAddressOf(), pErrorBlob.GetAddressOf());
		if (pErrorBlob != nullptr)
		{
			result.Success = false;
			result.ErrorMsg = (char*)pErrorBlob->GetBufferPointer();
			return result;
		}

		result.Success = true;
		return result;
	}

	CompileResult Compile(const char* pIdentifier, const char* pShaderSource, 
						  uint32_t shaderSourceSize, const char* pTarget, 
						  const char* pEntryPoint, uint32_t majVersion, 
						  uint32_t minVersion, const std::vector<ShaderDefine>& defines)
	{
		char target[16];
		size_t i = strlen(pTarget);
		memcpy(target, pTarget, i);
		target[i++] = '_';
		target[i++] = '0' + (char)majVersion;
		target[i++] = '_';
		target[i++] = '0' + (char)minVersion;
		target[i++] = 0;

		std::vector<std::string> definesActual;
		for (const ShaderDefine& define : defines)
		{
			definesActual.push_back(define.Value);
		}

		std::stringstream str;
		str << "_SM_MAJ=" << majVersion;
		definesActual.push_back(str.str());
		str = std::stringstream();
		str << "_SM_MIN=" << minVersion;
		definesActual.push_back(str.str());

		if (majVersion < 6)
		{
			definesActual.emplace_back("_FXC=1");
			return CompileFxc(pIdentifier, pShaderSource, shaderSourceSize, pEntryPoint, target, definesActual);
		}

		definesActual.emplace_back("_DXC=1");
		return CompileDxc(pIdentifier, pShaderSource, shaderSourceSize, pEntryPoint, target, definesActual);
	}
}

void* ShaderBase::GetByteCode() const
{
	return m_pByteCode->GetBufferPointer();
}

uint32_t ShaderBase::GetByteCodeSize() const
{
	return (uint32_t)m_pByteCode->GetBufferSize();
}

ShaderManager::ShaderManager(const std::string& shaderSourcePath, uint8_t shaderModelMajor, uint8_t shaderModelMinor)
	: m_ShaderSourcePath(shaderSourcePath), m_ShaderModelMajor(shaderModelMajor), m_ShaderModelMinor(shaderModelMinor)
{
	//if (CommandLine::GetBool("shaderhotreload"))
	{
		m_pFileWatcher = std::make_unique<FileWatcher>();
		m_pFileWatcher->StartWatching(shaderSourcePath, true);
		E_LOG(Info, "Shader Hot-Reload enabled: \"%s\"", shaderSourcePath.c_str());
	}
}

ShaderManager::~ShaderManager()
{}

ShaderManager::ShaderStringHash ShaderManager::GetEntryPointHash(const std::string& entryPoint, const std::vector<ShaderDefine>& defines)
{
	ShaderStringHash hash(entryPoint);
	for (const ShaderDefine& define : defines)
	{
		hash.Combine(StringHash(define.Value));
	}
	return hash;
}

void ShaderManager::ConditionallyReloadShaders()
{
	if (!m_pFileWatcher)
	{
		return;
	}

	FileWatcher::FileEvent fileEvent;
	while(m_pFileWatcher->GetNextChange(fileEvent))
	{
		switch (fileEvent.EventType)
		{
			case FileWatcher::FileEvent::Type::Added:
			case FileWatcher::FileEvent::Type::Modified:
			case FileWatcher::FileEvent::Type::Renamed:
				RecompileFromFileChange(fileEvent.Path);
				break;
		}
	}
}

void ShaderManager::RecompileFromFileChange(const std::string& filePath)
{
	auto it = m_IncludeDependencyMap.find(ShaderStringHash(filePath));
	if (it == m_IncludeDependencyMap.end())
	{
		return;
	}

	E_LOG(Info, "Modified \"%s\". Recompiling dependencies...", filePath.c_str());
	const std::unordered_set<std::string>& dependencies = it->second;
	for (const std::string& dependency : dependencies)
	{
		auto objectMapIt = m_FilepathToObjectMap.find(ShaderStringHash(dependency));
		if (objectMapIt == m_FilepathToObjectMap.end())
		{
			continue;
		}

		ShadersInFileMap objectMap = objectMapIt->second;
		for (auto shader : objectMap.Shaders)
		{
			Shader* pOldShader = shader.second;
			Shader* pNewShader = LoadShader(dependency, pOldShader->GetType(), pOldShader->GetEntryPoint().c_str(), pOldShader->GetDefines());
			if (pNewShader)
			{
				E_LOG(Info, "Reloaded shader: \"%s - %s\"", dependency.c_str(), pNewShader->GetEntryPoint().c_str());
				m_OnShaderRecompiledEvent.Broadcast(pOldShader, pNewShader);
				m_Shaders.remove_if([pOldShader](const ShaderPtr& ptr) { return ptr.get() == pOldShader; });
			}
		}
		for (auto library : objectMap.Libraries)
		{
			ShaderLibrary* pOldLibrary = library.second;
			ShaderLibrary* pNewLibrary = LoadLibrary(dependency, pOldLibrary->GetDefines());
			if (pNewLibrary)
			{
				E_LOG(Info, "Reloaded shader library: \"%s\"", dependency.c_str());
				m_OnLibraryRecompiledEvent.Broadcast(pOldLibrary, pNewLibrary);
				m_Libraries.remove_if([pOldLibrary](const LibraryPtr& ptr) { return ptr.get() == pOldLibrary; });
			}
		}
	}
}

bool ShaderManager::ProcessSource(const std::string& sourcePath, const std::string& filePath, std::stringstream& output, std::vector<ShaderStringHash>& processedIncludes)
{
	std::string line;

	int linesProcessed = 0;
	bool placedLineDirective = false;

	std::ifstream fileStream(filePath, std::ios::binary);

	if (fileStream.fail())
	{
		E_LOG(Error, "Failed to open file '%s'", filePath.c_str());
		return false;
	}

	while (getline(fileStream, line))
	{
		size_t includeStart = line.find("#include");
		if (includeStart != std::string::npos)
		{
			size_t start = line.find('"') + 1;
			size_t end = line.rfind('"');
			if (end == std::string::npos || start == std::string::npos || start == end)
			{
				E_LOG(Error, "Include syntax errror: %s", line.c_str());
				return false;
			}
			std::string includeFilePath = line.substr(start, end - start);
			ShaderStringHash includeHash(includeFilePath);
			if (std::find(processedIncludes.begin(), processedIncludes.end(), includeHash) == processedIncludes.end())
			{
				processedIncludes.push_back(includeHash);
				std::string basePath = Paths::GetDirectoryPath(filePath);
				std::string fullFilePath = basePath + includeFilePath;

				if (!ProcessSource(sourcePath, fullFilePath, output, processedIncludes))
				{
					return false;
				}
			}
			placedLineDirective = false;
		}
		else
		{
			if (placedLineDirective == false)
			{
				placedLineDirective = true;
#if USE_SHADER_LINE_DIRECTIVE
				output << "#line " << linesProcessed + 1 << " \"" << filePath << "\"\n";
#endif
			}
			output << line << '\n';
		}
		++linesProcessed;
	}
	return true;
}

Shader* ShaderManager::LoadShader(const std::string& shaderPath, ShaderType shaderType, const std::string& entryPoint, const std::vector<ShaderDefine>& defines)
{
	std::stringstream shaderSource;
	std::vector<ShaderStringHash> includes;
	std::string filePath = m_ShaderSourcePath + shaderPath;
	if (!ProcessSource(filePath, filePath, shaderSource, includes))
	{
		return nullptr;
	}

	std::string source = shaderSource.str();
	auto result = ShaderCompiler::Compile(shaderPath.c_str(), 
										source.c_str(), 
										(uint32_t)source.size(), 
										ShaderCompiler::GetShaderTarget(shaderType),
										entryPoint.c_str(), 
										m_ShaderModelMajor,
										m_ShaderModelMinor,
										defines);
	if (!result.Success)
	{
		E_LOG(Warning, "Failed to compile shader '%s': %s", shaderPath.c_str(), result.ErrorMsg.c_str());
		return nullptr;
	}

	ShaderPtr pNewShader = std::make_unique<Shader>(result.pBlob, shaderType, entryPoint, defines);
	m_Shaders.push_back(std::move(pNewShader));
	Shader* pShader = m_Shaders.back().get();

	for (const ShaderStringHash& include : includes)
	{
		m_IncludeDependencyMap[include].insert(shaderPath);
	}
	m_IncludeDependencyMap[ShaderStringHash(shaderPath)].insert(shaderPath);

	StringHash hash = GetEntryPointHash(entryPoint, defines);
	m_FilepathToObjectMap[ShaderStringHash(shaderPath)].Shaders[hash] = pShader;

	return pShader;
}

ShaderLibrary* ShaderManager::LoadLibrary(const std::string& shaderPath, const std::vector<ShaderDefine>& defines)
{
	std::stringstream shaderSource;
	std::vector<ShaderStringHash> includes;
	std::string filePath = m_ShaderSourcePath + shaderPath;
	if (!ProcessSource(filePath, filePath, shaderSource, includes))
	{
		return nullptr;
	}

	std::string source = shaderSource.str();
	auto result = ShaderCompiler::Compile(shaderPath.c_str(), source.c_str(), (uint32_t)source.size(), 
										  "lib", "", m_ShaderModelMajor, m_ShaderModelMinor, defines);
	if (!result.Success)
	{
		E_LOG(Warning, "Failed to compile shader library '%s': %s", shaderPath.c_str(), result.ErrorMsg.c_str());
		return nullptr;
	}

	LibraryPtr pNewLibrary = std::make_unique<ShaderLibrary>(result.pBlob, defines);
	m_Libraries.push_back(std::move(pNewLibrary));
	ShaderLibrary* pLibrary = m_Libraries.back().get();

	for (const ShaderStringHash& include : includes)
	{
		m_IncludeDependencyMap[include].insert(shaderPath);
	}
	m_IncludeDependencyMap[ShaderStringHash(shaderPath)].insert(shaderPath);

	StringHash hash = GetEntryPointHash("", defines);
	m_FilepathToObjectMap[ShaderStringHash(shaderPath)].Libraries[hash] = pLibrary;

	return pLibrary;
}

Shader* ShaderManager::GetShader(const std::string& shaderPath, ShaderType shaderType, const std::string& entryPoint, const std::vector<ShaderDefine>& defines)
{
	auto& shaderMap = m_FilepathToObjectMap[(ShaderStringHash)shaderPath].Shaders;
	StringHash hash = GetEntryPointHash(entryPoint, defines);
	auto it = shaderMap.find(hash);
	if (it != shaderMap.end())
	{
		return it->second;
	}

	return LoadShader(shaderPath, shaderType, entryPoint, defines);
}

ShaderLibrary* ShaderManager::GetLibrary(const std::string& shaderPath, const std::vector<ShaderDefine>& defines)
{
	auto& libraryMap = m_FilepathToObjectMap[(ShaderStringHash)shaderPath].Libraries;
	StringHash hash = GetEntryPointHash("", defines);
	auto it = libraryMap.find(hash);
	if (it != libraryMap.end())
	{
		return it->second;
	}

	return LoadLibrary(shaderPath, defines);
}
