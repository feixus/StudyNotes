#include "stdafx.h"
#include "Shader.h"
#include "Core/Paths.h"
#include "Core/CommandLine.h"
#include "Core/FileWatcher.h"
#include "Dxc/dxcapi.h"

namespace ShaderCompiler
{
	constexpr const char* pCompilerPath = "dxcompiler.dll";
	constexpr const char* pShaderSymbolsPath = "_Temp/ShaderSymbols/";

	static ComPtr<IDxcUtils> pUtils;
	static ComPtr<IDxcCompiler3> pCompiler3;
	static ComPtr<IDxcValidator> pValidator;
	static ComPtr<IDxcIncludeHandler> pDefaultIncludeHandler;

	struct CompileJob
	{
		std::string FilePath;
		std::string EntryPoint;
		std::string Target;
		std::vector<ShaderDefine> Defines;
		std::vector<std::string> IncludeDirs;
		uint8_t MajVersion;
		uint8_t MinVersion;
	};

	struct CompileResult
	{
		std::string ErrorMsg;
		ShaderBlob pBlob;
		ComPtr<IUnknown> pReflection;
		std::vector<std::string> Includes;

		bool Success() const{ return pBlob.Get() && ErrorMsg.length() == 0;};
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

	void LoadDXC()
	{
		FN_PROC(DxcCreateInstance);

		HMODULE lib = LoadLibraryA(pCompilerPath);
		DxcCreateInstanceFn.Load(lib);
		
		VERIFY_HR(DxcCreateInstanceFn(CLSID_DxcUtils, IID_PPV_ARGS(pUtils.GetAddressOf())));
		VERIFY_HR(DxcCreateInstanceFn(CLSID_DxcCompiler, IID_PPV_ARGS(pCompiler3.GetAddressOf())));
		VERIFY_HR(DxcCreateInstanceFn(CLSID_DxcValidator, IID_PPV_ARGS(pValidator.GetAddressOf())));
		VERIFY_HR(pUtils->CreateDefaultIncludeHandler(pDefaultIncludeHandler.GetAddressOf()));
		E_LOG(Info, "Loaded %s", pCompilerPath);
	}

	bool TryLoadFile(const char* pFilePath, const std::vector<std::string>& includeDirs, 
					 ComPtr<IDxcBlobEncoding>* pFile, std::string* pFullPath)
	{
		for (const std::string& includeDir : includeDirs)
		{
			std::string path = Paths::Combine(includeDir, pFilePath);
			if (Paths::FileExists(path))
			{
				if (SUCCEEDED(pUtils->LoadFile(MULTIBYTE_TO_UNICODE(path.c_str()), nullptr, pFile->GetAddressOf())))
				{
					*pFullPath = path;
					break;					
				}
			}
		}
		return *pFile;
	}

	CompileResult Compile(const CompileJob& compileJob)
	{
		CompileResult result;

		ComPtr<IDxcBlobEncoding> pSource;
		std::string fullPath;
		if (!TryLoadFile(compileJob.FilePath.c_str(), compileJob.IncludeDirs, &pSource, &fullPath))
		{
			result.ErrorMsg = Sprintf("Failed to load shader file: %s", compileJob.FilePath.c_str());
			return result;
		}

		bool debugShaders = true;// CommandLine::GetBool("debugshaders");
		bool shaderSymbols = true;// CommandLine::GetBool("shadersymbols");

		std::string target = Sprintf("%s_%d_%d", compileJob.Target.c_str(), compileJob.MajVersion, compileJob.MinVersion);

		class CompileArguments
		{
		public:
			void AddArgument(const char* pArgument, const char* pValue = nullptr)
			{
				m_Arguments.push_back(MULTIBYTE_TO_UNICODE(pArgument));
				if (pValue)
				{
					m_Arguments.push_back(MULTIBYTE_TO_UNICODE(pValue));
				}
			}

			void AddArgument(const wchar_t* pArgument, const wchar_t* pValue = nullptr)
			{
				m_Arguments.push_back(pArgument);
				if (pValue)
				{
					m_Arguments.push_back(pValue);
				}
			}

			void AddDefine(const char* pDefine, const char* pValue = nullptr)
			{
				if (strstr(pDefine, "=") != nullptr)
				{
					AddArgument(Sprintf("-D%s", pDefine).c_str());
				}
				else
				{
					AddArgument(Sprintf("-D%s=%s", pDefine, pValue ? pValue : "1").c_str());
				}
			}

			size_t GetNumArguments() const { return m_Arguments.size(); }
			const wchar_t** GetArguments()
			{
				m_ArgumentArr.reserve(GetNumArguments());
				for (const auto& arg : m_Arguments)
				{
					m_ArgumentArr.push_back(arg.c_str());
				}
				return m_ArgumentArr.data();
			}

		private:
			std::vector<const wchar_t*> m_ArgumentArr;
			std::vector<std::wstring> m_Arguments;
		} arguments;

		arguments.AddArgument(Paths::GetFileNameWithoutExtension(compileJob.FilePath).c_str());
		arguments.AddArgument("-E", compileJob.EntryPoint.c_str());
		arguments.AddArgument("-T", target.c_str());
		arguments.AddArgument(DXC_ARG_ALL_RESOURCES_BOUND);
		arguments.AddArgument(DXC_ARG_WARNINGS_ARE_ERRORS);
		arguments.AddArgument(DXC_ARG_PACK_MATRIX_ROW_MAJOR);

		// payload access qualifiers
		// if (majVersion >= 6 && minVersion >= 6)
		// {
		// 	arguments.AddArgument("-enable-payload-qualifiers");
		// 	arguments.AddDefine("_PAYLOAD_QUALIFIERS = 1");
		// }
		// else
		{
			arguments.AddDefine("_PAYLOAD_QUALIFIERS", "0");
		}

		if (debugShaders || shaderSymbols)
		{
			arguments.AddArgument("-Qembed_debug");
			arguments.AddArgument(DXC_ARG_DEBUG);
		}
		else
		{
			arguments.AddArgument("-Qstrip_debug");
			arguments.AddArgument("-Fd", pShaderSymbolsPath);
			arguments.AddArgument("-Qstrip_reflect");
		}

		if (debugShaders)
		{
			arguments.AddArgument(DXC_ARG_SKIP_OPTIMIZATIONS);
		}
		else
		{
			arguments.AddArgument(DXC_ARG_OPTIMIZATION_LEVEL3);
		}

		for (const std::string& includeDir : compileJob.IncludeDirs)
		{
			arguments.AddArgument("-I", includeDir.c_str());
		}

		for (const ShaderDefine& define : compileJob.Defines)
		{
			arguments.AddDefine(define.Value.c_str());
		}

		// why these argument must at the last after pass defines, due to miss entry point for CBT::RenderGS and missing volumetric fog.
		arguments.AddDefine(std::format("_SM_MAJ={}", compileJob.MajVersion).c_str());
		arguments.AddDefine(std::format("_SM_MIN={}", compileJob.MinVersion).c_str());
		arguments.AddDefine("_DXC");

		DxcBuffer sourceBuffer;
		sourceBuffer.Ptr = pSource->GetBufferPointer();
		sourceBuffer.Size = pSource->GetBufferSize();
		sourceBuffer.Encoding = 0;

		class CustomIncludeHandler : public IDxcIncludeHandler
		{
		public:
			HRESULT STDMETHODCALLTYPE LoadSource(_In_z_ LPCWSTR pFilename, _COM_Outptr_result_maybenull_ IDxcBlob **ppIncludeSource) override
			{
				ComPtr<IDxcBlobEncoding> pEncoding;
				std::string path = Paths::Normalize(UNICODE_TO_MULTIBYTE(pFilename));
				if (!Paths::FileExists(path))
				{
					*ppIncludeSource = nullptr;
					return E_FAIL;
				}
				if (IncludedFiles.find(path) != IncludedFiles.end())
				{
					static const char nullStr[] = " ";
					pUtils->CreateBlob(nullStr, std::size(nullStr), CP_UTF8, pEncoding.GetAddressOf());
					*ppIncludeSource = pEncoding.Detach();
					return S_OK;
				}

				if (!IsValidIncludePath(path.c_str()))
				{
					E_LOG(Warning, "Include path '%s' does not have a valid extension.", path.c_str());
					return E_FAIL;
				}

				HRESULT hr = pUtils->LoadFile(pFilename, nullptr, pEncoding.GetAddressOf());
				if (SUCCEEDED(hr))
				{
					IncludedFiles.insert(path);
					*ppIncludeSource = pEncoding.Detach();
				}
				else
				{
					*ppIncludeSource = nullptr;
				}
				return hr;
			}

			bool IsValidIncludePath(const char* pFilePath) const
			{
				std::string extension = Paths::GetFileExtension(pFilePath);
				CString::ToLower(extension.c_str(), extension.data());
				constexpr const char* pValidExtensions[] = {".hlsli", ".h"};
				for (uint32_t i = 0; i < std::size(pValidExtensions); i++)
				{
					if (strcmp(pValidExtensions[i], extension.c_str()) == 0)
					{
						return true;
					}
				}
				return false;
			}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject) override
			{
				return pDefaultIncludeHandler->QueryInterface(riid, ppvObject);
			}

			void Reset() { IncludedFiles.clear(); }

			ULONG STDMETHODCALLTYPE AddRef() override { return 0; }
			ULONG STDMETHODCALLTYPE Release() override { return 0; }

			std::unordered_set<std::string> IncludedFiles;
		};

		// if (CommandLine::GetBool("dumpshaders"))
		{
			// preprocessed source
			ComPtr<IDxcResult> pPreprocessOutput;
			CompileArguments preprocessArgs = arguments;
			preprocessArgs.AddArgument("-P", ".");
			CustomIncludeHandler preprocessIncludeHandler;
			if (SUCCEEDED(pCompiler3->Compile(&sourceBuffer, preprocessArgs.GetArguments(), (uint32_t)preprocessArgs.GetNumArguments(), &preprocessIncludeHandler, IID_PPV_ARGS(pPreprocessOutput.GetAddressOf()))))
			{
				ComPtr<IDxcBlobUtf8> pHLSL;
				if (SUCCEEDED(pPreprocessOutput->GetOutput(DXC_OUT_HLSL, IID_PPV_ARGS(pHLSL.GetAddressOf()), nullptr)))
				{
					Paths::CreateDirectoryTree(pShaderSymbolsPath);
					std::ofstream str(Sprintf("%s%s_%s_%s.hlsl", pShaderSymbolsPath, Paths::GetFileNameWithoutExtension(compileJob.FilePath).c_str(), compileJob.EntryPoint.c_str(), compileJob.Target.c_str()));
					str.write(pHLSL->GetStringPointer(), pHLSL->GetStringLength());
				}
			}
		}

		CustomIncludeHandler includeHandler;
		ComPtr<IDxcResult> pCompileResult;
		VERIFY_HR(pCompiler3->Compile(&sourceBuffer, arguments.GetArguments(), (uint32_t)arguments.GetNumArguments(), &includeHandler, IID_PPV_ARGS(pCompileResult.GetAddressOf())));

		ComPtr<IDxcBlobUtf8> pErrors;
		pCompileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(pErrors.GetAddressOf()), nullptr);
		if (pErrors && pErrors->GetStringLength() > 0)
		{
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
			VERIFY_HR(pValidator->Validate((IDxcBlob*)result.pBlob.Get(), DxcValidatorFlags_InPlaceEdit, pResult.GetAddressOf()));
			HRESULT validationResult;
			pResult->GetStatus(&validationResult);
			if (validationResult != S_OK)
			{
				ComPtr<IDxcBlobEncoding> pPrintBlob;
				ComPtr<IDxcBlobUtf8> pPrintBlobUtf8;
				pResult->GetErrorBuffer(pPrintBlob.GetAddressOf());
				pUtils->GetBlobAsUtf8(pPrintBlob.Get(), pPrintBlobUtf8.GetAddressOf());

				result.ErrorMsg = pPrintBlobUtf8->GetStringPointer();
				return result;
			}
		}

		//Symbols
		{
			ComPtr<IDxcBlobUtf16> pSymbolsNameUtf16;
			ComPtr<IDxcBlob> pSymbolsBlob;
			if (SUCCEEDED(pCompileResult->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(pSymbolsBlob.GetAddressOf()), pSymbolsNameUtf16.GetAddressOf())))
			{
				Paths::CreateDirectoryTree(pShaderSymbolsPath);

				ComPtr<IDxcBlobUtf8> pSymbolsNameUtf8;
				pUtils->GetBlobAsUtf8(pSymbolsNameUtf16.Get(), pSymbolsNameUtf8.GetAddressOf());
				std::string debugPath = std::format("{}{}", pShaderSymbolsPath, pSymbolsNameUtf8->GetStringPointer());
				std::ofstream str(debugPath, std::ios::binary);
				str.write((const char*)pSymbolsBlob->GetBufferPointer(), pSymbolsBlob->GetBufferSize());
			}
		}

		//Reflection
		{
			ComPtr<IDxcBlob> pReflectionData;
			if (SUCCEEDED(pCompileResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(pReflectionData.GetAddressOf()), nullptr)))
			{
				DxcBuffer reflectionBuffer;
				reflectionBuffer.Ptr = pReflectionData->GetBufferPointer();
				reflectionBuffer.Size = pReflectionData->GetBufferSize();
				reflectionBuffer.Encoding = 0;
				VERIFY_HR(pUtils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(result.pReflection.GetAddressOf())));
			}
		}

		for (const std::string& includePath : includeHandler.IncludedFiles)
		{
			result.Includes.push_back(includePath);
		}

		return result;
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

ShaderManager::ShaderManager(uint8_t shaderModelMajor, uint8_t shaderModelMinor)
	: m_ShaderModelMajor(shaderModelMajor), m_ShaderModelMinor(shaderModelMinor)
{
	ShaderCompiler::LoadDXC();
}

ShaderManager::~ShaderManager()
{}

ShaderManager::ShaderStringHash ShaderManager::GetEntryPointHash(const char* pEntryPoint, const std::vector<ShaderDefine>& defines)
{
	ShaderStringHash hash(pEntryPoint);
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
			case FileWatcher::FileEvent::Type::Removed:
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
			Shader* pNewShader = LoadShader(dependency.c_str(), pOldShader->GetType(), pOldShader->GetEntryPoint(), pOldShader->GetDefines());
			if (pNewShader)
			{
				E_LOG(Info, "Reloaded shader: \"%s - %s\"", dependency.c_str(), pNewShader->GetEntryPoint());
				m_OnShaderRecompiledEvent.Broadcast(pOldShader, pNewShader);
				m_Shaders.remove_if([pOldShader](const ShaderPtr& ptr) { return ptr.get() == pOldShader; });
			}
			else
			{
				E_LOG(Warning, "Failed to reload shader: \"%s - %s\"", dependency.c_str(), pOldShader->GetEntryPoint());
			}
		}
		for (auto library : objectMap.Libraries)
		{
			ShaderLibrary* pOldLibrary = library.second;
			ShaderLibrary* pNewLibrary = LoadLibrary(dependency.c_str(), pOldLibrary->GetDefines());
			if (pNewLibrary)
			{
				E_LOG(Info, "Reloaded shader library: \"%s\"", dependency.c_str());
				m_OnLibraryRecompiledEvent.Broadcast(pOldLibrary, pNewLibrary);
				m_Libraries.remove_if([pOldLibrary](const LibraryPtr& ptr) { return ptr.get() == pOldLibrary; });
			}
			else
			{
				E_LOG(Warning, "Failed to reload shader library: \"%s\"", dependency.c_str());
			}
		}
	}
}

Shader* ShaderManager::LoadShader(const char* pShaderPath, ShaderType shaderType, const char* pEntryPoint, const std::vector<ShaderDefine>& defines)
{
	ShaderCompiler::CompileJob compileJob;
	compileJob.FilePath = pShaderPath;
	compileJob.EntryPoint = pEntryPoint;
	compileJob.Defines = defines;
	compileJob.IncludeDirs = m_IncludeDirs;
	compileJob.MajVersion = m_ShaderModelMajor;
	compileJob.MinVersion = m_ShaderModelMinor;
	compileJob.Target = ShaderCompiler::GetShaderTarget(shaderType);

	auto result = ShaderCompiler::Compile(compileJob);
	if (!result.Success())
	{
		E_LOG(Warning, "Failed to compile shader '%s:%s': %s", pShaderPath, pEntryPoint, result.ErrorMsg.c_str());
		return nullptr;
	}

	ShaderPtr pNewShader = std::make_unique<Shader>(result.pBlob, shaderType, pEntryPoint, defines);
	m_Shaders.push_back(std::move(pNewShader));
	Shader* pShader = m_Shaders.back().get();

	for (const std::string& include : result.Includes)
	{
		m_IncludeDependencyMap[ShaderStringHash(Paths::GetFileName(include))].insert(pShaderPath);
	}
	m_IncludeDependencyMap[ShaderStringHash(pShaderPath)].insert(pShaderPath);

	StringHash hash = GetEntryPointHash(pEntryPoint, defines);
	m_FilepathToObjectMap[ShaderStringHash(pShaderPath)].Shaders[hash] = pShader;

	return pShader;
}

ShaderLibrary* ShaderManager::LoadLibrary(const char* pShaderPath, const std::vector<ShaderDefine>& defines)
{
	ShaderCompiler::CompileJob compileJob;
	compileJob.FilePath = pShaderPath;
	compileJob.Defines = defines;
	compileJob.IncludeDirs = m_IncludeDirs;
	compileJob.MajVersion = m_ShaderModelMajor;
	compileJob.MinVersion = m_ShaderModelMinor;
	compileJob.Target = "lib";

	auto result = ShaderCompiler::Compile(compileJob);
	if (!result.Success())
	{
		E_LOG(Warning, "Failed to compile shader library '%s': %s", pShaderPath, result.ErrorMsg.c_str());
		return nullptr;
	}

	LibraryPtr pNewLibrary = std::make_unique<ShaderLibrary>(result.pBlob, defines);
	m_Libraries.push_back(std::move(pNewLibrary));
	ShaderLibrary* pLibrary = m_Libraries.back().get();

	for (const std::string& include : result.Includes)
	{
		m_IncludeDependencyMap[ShaderStringHash(Paths::GetFileName(include))].insert(pShaderPath);
	}
	m_IncludeDependencyMap[ShaderStringHash(pShaderPath)].insert(pShaderPath);

	StringHash hash = GetEntryPointHash("", defines);
	m_FilepathToObjectMap[ShaderStringHash(pShaderPath)].Libraries[hash] = pLibrary;

	return pLibrary;
}

Shader* ShaderManager::GetShader(const char* pShaderPath, ShaderType shaderType, const char* pEntryPoint, const std::vector<ShaderDefine>& defines)
{
	auto& shaderMap = m_FilepathToObjectMap[(ShaderStringHash)pShaderPath].Shaders;
	StringHash hash = GetEntryPointHash(pEntryPoint, defines);
	auto it = shaderMap.find(hash);
	if (it != shaderMap.end())
	{
		return it->second;
	}

	return LoadShader(pShaderPath, shaderType, pEntryPoint, defines);
}

ShaderLibrary* ShaderManager::GetLibrary(const char* pShaderPath, const std::vector<ShaderDefine>& defines)
{
	auto& libraryMap = m_FilepathToObjectMap[(ShaderStringHash)pShaderPath].Libraries;
	StringHash hash = GetEntryPointHash("", defines);
	auto it = libraryMap.find(hash);
	if (it != libraryMap.end())
	{
		return it->second;
	}

	return LoadLibrary(pShaderPath, defines);
}

void ShaderManager::AddIncludeDir(const std::string& includeDir, bool watch)
{
	m_IncludeDirs.push_back(includeDir);

	if (watch)
	{
		checkf(!m_pFileWatcher, "Can only have a single watch include directory");
		m_pFileWatcher = std::make_unique<FileWatcher>();
		m_pFileWatcher->StartWatching(includeDir.c_str(), true);
		E_LOG(Info, "Shader Hot-Reload enabled: \"%s\"", includeDir.c_str());
	}
}
