#include "stdafx.h"
#include "Shader.h"
#include "Core/Paths.h"
#include "Core/CommandLine.h"

#include <D3Dcompiler.h>

#ifndef USE_SHADER_LINE_DIRECTIVE
#define USE_SHADER_LINE_DIRECTIVE 1
#endif

#define SM_MAJ 6
#define SM_MIN 5

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

		wchar_t target[256], fileName[256], entryPoint[256];
		ToWidechar(pIdentifier, fileName, 256);
		ToWidechar(pEntryPoint, entryPoint, 256);
		ToWidechar(pTarget, target, 256);

		bool debugShaders = CommandLine::GetBool("debugshaders");

		std::vector<std::wstring> wDefines;
		for (const std::string& define : defines)
		{
			wchar_t intermediate[256];
			ToWidechar(define.c_str(), intermediate, 256);
			wDefines.push_back(intermediate);
		}

		std::vector<LPCWSTR> arguments;
		arguments.reserve(20);

		arguments.push_back(L"-E");
		arguments.push_back(entryPoint);

		arguments.push_back(L"-T");
		arguments.push_back(target);
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
			wchar_t symbolsPath[256];
			ToWidechar(pShaderSymbolsPath, symbolsPath, 256);
			arguments.push_back(L"/Fd");
			arguments.push_back(symbolsPath);
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
			Paths::CreateDirectoryTree(pShaderSymbolsPath);
			std::stringstream pathStream;
			char path[256];
			ToMultibyte((wchar_t*)pDebugDataPath->GetBufferPointer(), path, 256);
			pathStream << pShaderSymbolsPath << path;
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

	CompileResult Compile(const char* pIdentifier, const char* pShaderSource, uint32_t shaderSourceSize, const char* pTarget, const char* pEntryPoint, uint32_t majVersion, uint32_t minVersion, const std::vector<std::string>& defines)
	{
		char target[16];
		size_t i = strlen(pTarget);
		memcpy(target, pTarget, i);
		target[i++] = '_';
		target[i++] = '0' + (char)majVersion;
		target[i++] = '_';
		target[i++] = '0' + (char)minVersion;
		target[i++] = 0;

		std::vector<std::string> definesActual(defines);
		for (std::string& define : definesActual)
		{
			if (define.find('=') == std::string::npos)
			{
				define += std::string("=1");
			}
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

bool ShaderManager::ProcessSource(const std::string& sourcePath, const std::string& filePath, std::stringstream& output, std::vector<StringHash>& processedIncludes, std::vector<std::string>& dependencies)
{
	if (sourcePath != filePath)
	{
		dependencies.push_back(filePath);
	}

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
			StringHash includeHash(includeFilePath);
			if (std::find(processedIncludes.begin(), processedIncludes.end(), includeHash) == processedIncludes.end())
			{
				processedIncludes.push_back(includeHash);
				std::string basePath = Paths::GetDirectoryPath(filePath);
				std::string fullFilePath = basePath + includeFilePath;

				if (!ProcessSource(sourcePath, fullFilePath, output, processedIncludes, dependencies))
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

Shader* ShaderManager::GetShader(const std::string& shaderPath, ShaderType shaderType, const char* pEntryPoint, const std::vector<std::string>& defines)
{
	std::stringstream shaderSource;
	std::vector<StringHash> includes;
	std::vector<std::string> dependencies;
	std::string filePath = m_ShaderSourcePath + shaderPath;
	if (!ProcessSource(filePath, filePath, shaderSource, includes, dependencies))
	{
		return nullptr;
	}

	std::string source = shaderSource.str();

	auto result = ShaderCompiler::Compile(shaderPath.c_str(), source.c_str(), (uint32_t)source.size(), 
										  ShaderCompiler::GetShaderTarget(shaderType), pEntryPoint, 
										  m_ShaderModelMajor, m_ShaderModelMinor, defines);
	if (!result.Success)
	{
		E_LOG(Error, "Failed to compile shader '%s': %s", shaderPath.c_str(), result.ErrorMsg.c_str());
		return nullptr;
	}

	ShaderPtr pNewShader = std::make_unique<Shader>(result.pBlob, shaderType, pEntryPoint);
	m_Shaders.push_back(std::move(pNewShader));
	return m_Shaders.back().get();
}

ShaderLibrary* ShaderManager::GetLibrary(const std::string& shaderPath, const std::vector<std::string>& defines)
{
	std::stringstream shaderSource;
	std::vector<StringHash> includes;
	std::vector<std::string> dependencies;
	std::string filePath = m_ShaderSourcePath + shaderPath;
	if (!ProcessSource(filePath, filePath, shaderSource, includes, dependencies))
	{
		return nullptr;
	}

	std::string source = shaderSource.str();
	auto result = ShaderCompiler::Compile(shaderPath.c_str(), source.c_str(), (uint32_t)source.size(), 
										  "lib", "", m_ShaderModelMajor, m_ShaderModelMinor, defines);
	if (!result.Success)
	{
		E_LOG(Error, "Failed to compile shader library '%s': %s", shaderPath.c_str(), result.ErrorMsg.c_str());
		return nullptr;
	}

	LibraryPtr pNewLibrary = std::make_unique<ShaderLibrary>(result.pBlob);
	m_Libraries.push_back(std::move(pNewLibrary));
	return m_Libraries.back().get();
}
