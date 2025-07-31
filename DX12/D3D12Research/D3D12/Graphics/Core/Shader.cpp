#include "stdafx.h"
#include "Shader.h"
#include "Core/Paths.h"

#ifndef USE_SHADER_LINE_DIRECTIVE
#define USE_SHADER_LINE_DIRECTIVE 1
#endif

bool ShaderCompiler::CompileDxc(const char* pIdentifier, const char* pShaderSource,
					uint32_t shaderSourceSize, IDxcBlob** pOutput, const char* pEntryPoint,
					const char* pTarget, const std::vector<std::string>& defines)
{
	static ComPtr<IDxcUtils> pUtils;
	static ComPtr<IDxcCompiler3> pCompiler;
	static ComPtr<IDxcValidator> pValidator;

	if (!pCompiler)
	{
		HR(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&pUtils)));
		HR(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pCompiler)));
		HR(DxcCreateInstance(CLSID_DxcValidator, IID_PPV_ARGS(&pValidator)));
	}

	ComPtr<IDxcBlobEncoding> pSource;
	HR(pUtils->CreateBlob(pShaderSource, shaderSourceSize, CP_UTF8, pSource.GetAddressOf()));

	wchar_t target[256], fileName[256], entryPoint[256];
	ToWidechar(pTarget, target, 256);
	ToWidechar(pEntryPoint, entryPoint, 256);
	ToWidechar(pIdentifier, fileName, 256);

	const constexpr char* pShaderSymbolsPath = "_Temp/ShaderSymbols/";
	bool debugShaders = CommandLine::GetBool("DebugShaders");
#ifdef _DEBUG
	debugShaders = true;
#endif

	std::vector<std::wstring> dDefineNames;
	std::vector<std::wstring> wDefines;
	for (const auto& define : defines)
	{
		std::wstringstream str;
		wchar_t intermediate[256];
		ToWidechar(define.c_str(), intermediate, 256);
		str << intermediate << "=1";
		wDefines.push_back(str.str());
	}

	wDefines.push_back(L"_DXC=1");

	std::vector<LPCWSTR> arguments;
	arguments.reserve(20);

	arguments.push_back(L"-E");
	arguments.push_back(entryPoint);

	arguments.push_back(L"-T");
	arguments.push_back(target);

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
	}

	arguments.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);
	arguments.push_back(DXC_ARG_DEBUG);
	arguments.push_back(DXC_ARG_PACK_MATRIX_ROW_MAJOR);

	arguments.push_back(L"-Qstrip_reflect");

	for (size_t i = 0; i < wDefines.size(); i++)
	{
		arguments.push_back(L"-D");
		arguments.push_back(wDefines[i].c_str());
	}

	DxcBuffer sourceBuffer;
	sourceBuffer.Ptr = pSource->GetBufferPointer();
	sourceBuffer.Size = pSource->GetBufferSize();
	sourceBuffer.Encoding = 0;

	ComPtr<IDxcResult> pCompileResult;
	HR(pCompiler->Compile(&sourceBuffer, arguments.data(), (uint32_t)arguments.size(), nullptr, IID_PPV_ARGS(pCompileResult.GetAddressOf())));

	ComPtr<IDxcBlobUtf8> pErrors;
	pCompileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(pErrors.GetAddressOf()), nullptr);
	if (pErrors && pErrors->GetStringLength() > 0)
	{
		E_LOG(LogType::Warning, "%s", (uint8_t*)pErrors->GetBufferPointer());
		return false;
	}

	// shader object
	{
		HR(pCompileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(pOutput), nullptr));
	}

	// validation
	{
		ComPtr<IDxcOperationResult> pResult;
		HR(pValidator->Validate(*pOutput, DxcValidatorFlags_InPlaceEdit, pResult.GetAddressOf()));
		HRESULT validationResult;
		pResult->GetStatus(&validationResult);
		if (validationResult != S_OK)
		{
			ComPtr<IDxcBlobEncoding> pPrintBlob;
			ComPtr<IDxcBlobUtf8> pPrintBlobUtf8;
			pResult->GetErrorBuffer(pPrintBlob.GetAddressOf());
			pUtils->GetBlobAsUtf8(pPrintBlob.Get(), pPrintBlobUtf8.GetAddressOf());
			E_LOG(LogType::Warning, "%s", pPrintBlobUtf8->GetBufferPointer());
		}
	}

#if 0
	// symbols
	{
		ComPtr<IDxcBlob> pDebugData;
		ComPtr<IDxcBlobUtf16> pDebugDataPath;
		pCompileResult->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(pDebugData.GetAddressOf()), pDebugDataPath.GetAddressOf());
		
		Paths::CreateDirectoryTree(pShaderSymbolsPath);
		std::filesystem::path fullPath = std::filesystem::path(pShaderSymbolsPath) / 
			std::filesystem::path(reinterpret_cast<const wchar_t*>(pDebugDataPath->GetBufferPointer()));
		
		std::ofstream fileStream(fullPath, std::ios::binary);
		fileStream.write((char*)pDebugData->GetBufferPointer(), pDebugData->GetBufferSize());
	}

	// reflection
	{
		ComPtr<IDxcBlob> pReflectionData;
		pCompileResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(pReflectionData.GetAddressOf()), nullptr);
		DxcBuffer reflectionBuffer;
		reflectionBuffer.Ptr = pReflectionData->GetBufferPointer();
		reflectionBuffer.Size = pReflectionData->GetBufferSize();
		reflectionBuffer.Encoding = 0;
		if (strcmp(pEntryPoint, "") == 0)
		{
			ComPtr<ID3D12LibraryReflection> pLibraryReflection;
			HR(pUtils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(pLibraryReflection.GetAddressOf())));
			D3D12_LIBRARY_DESC libraryDesc;
			pLibraryReflection->GetDesc(&libraryDesc);
		}
		else
		{
			ComPtr<ID3D12ShaderReflection> pShaderReflection;
			HR(pUtils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(pShaderReflection.GetAddressOf())));
			D3D12_SHADER_DESC shaderDesc;
			pShaderReflection->GetDesc(&shaderDesc);
		}
	}
#endif	
	return true;
}

bool ShaderCompiler::CompileFxc(const char* pIdentifier, const char* pShaderSource,
						uint32_t shaderSourceSize, ID3DBlob** pOutput, const char* pEntryPoint,
						const char* pTarget, const std::vector<std::string>& defines)
{
	uint32_t compileFlags = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
#if defined(_DEBUG)
	// shader debugging
	compileFlags |= (D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_PREFER_FLOW_CONTROL);
#else
	compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

	std::vector<std::string> globalDefines;
	globalDefines.push_back("_FXC=1");

	std::vector<D3D_SHADER_MACRO> shaderDefines;
	for (const std::string& define : defines)
	{
		D3D_SHADER_MACRO m;
		m.Name = define.c_str();
		m.Definition = "1";
		shaderDefines.push_back(m);
	}

	for (const auto& define : globalDefines)
	{
		D3D_SHADER_MACRO m;
		m.Name = define.c_str();
		m.Definition = "1";
		shaderDefines.push_back(m);
	}

	D3D_SHADER_MACRO endMacro;
	endMacro.Name = nullptr;
	endMacro.Definition = nullptr;
	shaderDefines.push_back(endMacro);

	ComPtr<ID3DBlob> pErrorBlob;
	D3DCompile(pShaderSource, shaderSourceSize, pIdentifier, shaderDefines.data(), nullptr, pEntryPoint, pTarget, compileFlags, 0, pOutput, pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMessage((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << pIdentifier << " --> " << errorMessage << std::endl;
		return false;
	}

	pErrorBlob.Reset();
	return true;
}

bool ShaderBase::ProcessSource(const std::string& sourcePath, const std::string& filePath, std::stringstream& output, std::vector<StringHash>& processedIncludes, std::vector<std::string>& dependencies)
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
		E_LOG(LogType::Error, "Failed to open shader file: %s", filePath.c_str());
		return false;
	}

	while (std::getline(fileStream, line))
	{
		size_t start = line.find("#include");
		if (start != std::string::npos)
		{
			size_t start = line.find('"') + 1;
			size_t end = line.rfind('"');
			if (end == std::string::npos || start == std::string::npos || start == end)
			{
				E_LOG(LogType::Error, "Include syntax error: %s", line.c_str());
				return false;
			}

			std::string includeFilePath = line.substr(start, end - start);
			StringHash includeHash(includeFilePath);
			if (std::find(processedIncludes.begin(), processedIncludes.end(), includeHash) == processedIncludes.end())
			{
				processedIncludes.push_back(includeHash);
				std::string basePath = Paths::GetDirectoryPath(filePath);
				std::string filePath = basePath + includeFilePath;

				if (!ProcessSource(sourcePath, filePath, output, processedIncludes, dependencies))
				{
					return false;
				}
			}
			placedLineDirective = false;
		}
		else
		{
			if (!placedLineDirective)
			{
				placedLineDirective = true;
#if USE_SHADER_LINE_DIRECTIVE
				output << "#line " << linesProcessed + 1 << " \"" << filePath << "\"\n";
#endif
			}
			output << line << '\n';
		}

		linesProcessed++;
	}
	
	return true;
}

void* ShaderBase::GetByteCode() const
{
	return m_pByteCode->GetBufferPointer();
}

uint32_t ShaderBase::GetByteCodeSize() const
{
	return (uint32_t)m_pByteCode->GetBufferSize();
}

Shader::Shader(const char* pFilePath, Type shaderType, const char* pEntryPoint, const std::vector<std::string> defines)
{
	m_Path = pFilePath;
	m_Type = shaderType;
	Compile(pFilePath, shaderType, pEntryPoint, 6, 0, defines);
}

bool Shader::Compile(const char* pFilePath, Type shaderType, const char* pEntryPoint, char shaderModelMajor, char shaderModelMinor, const std::vector<std::string>& defines)
{
	std::stringstream shadersource;
	std::vector<StringHash> includes;
	if (!ProcessSource(pFilePath, pFilePath, shadersource, includes, m_Dependencies))
	{
		return false;
	}

	std::string source = shadersource.str();
	std::string target = GetShaderTarget(shaderType, shaderModelMajor, shaderModelMinor);

	if (shaderModelMajor < 6)
	{
		ID3DBlob** pBlob = reinterpret_cast<ID3DBlob**>(m_pByteCode.GetAddressOf());
		return ShaderCompiler::CompileFxc(pFilePath, source.c_str(), (uint32_t)source.size(), pBlob, pEntryPoint, target.c_str(), defines);
	}

	IDxcBlob** pBlob = reinterpret_cast<IDxcBlob**>(m_pByteCode.GetAddressOf());
	return ShaderCompiler::CompileDxc(pFilePath, source.c_str(), (uint32_t)source.size(), pBlob, pEntryPoint, target.c_str(), defines);
}

std::string Shader::GetShaderTarget(Type shaderType, char shaderModelMajor, char shaderModelMinor)
{
	char out[16];
	switch (shaderType)
	{
	case Type::Vertex:
		sprintf_s(out, "vs_%d_%d", shaderModelMajor, shaderModelMinor);
		break;
	case Type::Pixel:
		sprintf_s(out, "ps_%d_%d", shaderModelMajor, shaderModelMinor);
		break;
	case Type::Geometry:
		sprintf_s(out, "gs_%d_%d", shaderModelMajor, shaderModelMinor);
		break;
	case Type::Compute:
		sprintf_s(out, "cs_%d_%d", shaderModelMajor, shaderModelMinor);
		break;
	case Type::MAX:
	default:
		return "";
	}

	return out;
}

ShaderLibrary::ShaderLibrary(const char* pFilePath, const std::vector<std::string> defines)
{
	m_Path = pFilePath;
	Compile(pFilePath, 6, 3, defines);
}

std::string ShaderLibrary::GetShaderTarget(char shaderModelMajor, char shaderModelMinor)
{
	char out[16];
	sprintf_s(out, "lib_%d_%d", shaderModelMajor, shaderModelMinor);
	return out;
}

bool ShaderLibrary::Compile(const char* pFilePath, char shaderModelMajor, char shaderModelMinor, const std::vector<std::string> defines)
{
	std::stringstream shaderSource;
	std::vector<StringHash> includes;
	if (!ProcessSource(pFilePath, pFilePath, shaderSource, includes, m_Dependencies))
	{
		return false;
	}
	std::string source = shaderSource.str();

	IDxcBlob** pBlob = reinterpret_cast<IDxcBlob**>(m_pByteCode.GetAddressOf());
	std::string target = GetShaderTarget(shaderModelMajor, shaderModelMinor);
	return ShaderCompiler::CompileDxc(pFilePath, source.c_str(), (uint32_t)source.size(), pBlob, "", target.c_str(), defines);
}
