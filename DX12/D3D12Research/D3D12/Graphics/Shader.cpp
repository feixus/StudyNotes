#include "stdafx.h"
#include "Shader.h"
#include "Core/Paths.h"

#define USE_SHADER_LINE_DIRECTIVE 1
#define DXC_COMPILER 1

#if DXC_COMPILER
#pragma comment(lib, "dxcompiler.lib")
#endif


Shader::Shader(const char* pFilePath, Type shaderType, const char* pEntryPoint, const std::vector<std::string> defines)
{
	m_Path = pFilePath;
	m_Type = shaderType;
	Compile(pFilePath, shaderType, pEntryPoint, 6, 0, defines);
}

Shader::~Shader()
{
}

bool Shader::ProcessSource(const std::string& filePath, std::stringstream& output, std::vector<StringHash>& processedIncludes, std::vector<std::string>& dependencies)
{
	if (m_Path != filePath)
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

				if (!ProcessSource(filePath, output, processedIncludes, dependencies))
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

bool Shader::Compile(const char* pFilePath, Type shaderType, const char* pEntryPoint, char shaderModelMajor, char shaderModelMinor, const std::vector<std::string>& defines)
{
	std::stringstream shadersource;
	std::vector<StringHash> includes;
	std::vector<std::string> dependencies;
	if (!ProcessSource(pFilePath, shadersource, includes, dependencies))
	{
		return false;
	}

	std::string source = shadersource.str();
	std::string target = GetShaderTarget(shaderType, shaderModelMajor, shaderModelMinor);

	if (shaderModelMajor < 6)
	{
		return CompileFxc(source, target.c_str(), pEntryPoint, defines);
	}

	return CompileDxc(source, target.c_str(), pEntryPoint, defines);
}

bool Shader::CompileDxc(const std::string& source, const char* pTarget, const char* pEntryPoint, const std::vector<std::string>& defines)
{

#if DXC_COMPILER
	IDxcLibrary* pLibrary;
	DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&pLibrary));
	IDxcCompiler* pCompiler;
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pCompiler));

	IDxcBlobEncoding* pSource;
	HR(pLibrary->CreateBlobWithEncodingFromPinned(source.data(), (uint32_t)source.size(), CP_UTF8, &pSource));

	size_t written = 0;
	wchar_t target[256];
	mbstowcs_s(&written, target, pTarget, 256);

	LPCWSTR pArgs[] = {
		L"/Zpr",
#ifdef _DEBUG
		L"-Zi",
#else
		L"-0d",
#endif	
	};

	std::vector<std::wstring> dDefineNames;
	std::vector<std::wstring> dDefineValues;
	for (const auto& define : defines)
	{
		dDefineNames.push_back(std::wstring(define.begin(), define.end()));
		dDefineValues.push_back(L"1");
	}

	for (const auto& define : m_GlobalShaderDefines)
	{
		dDefineNames.push_back(std::wstring(define.first.begin(), define.first.end()));
		dDefineValues.push_back(std::wstring(define.second.begin(), define.second.end()));
	}

	std::vector<DxcDefine> dxcDefines;
	for (size_t i = 0; i < dDefineNames.size(); i++)
	{
		DxcDefine define;
		define.Name = dDefineNames[i].c_str();
		define.Value = dDefineValues[i].c_str();
		dxcDefines.push_back(define);
	}

	wchar_t fileName[256], entryPoint[256];
	mbstowcs_s(&written, fileName, m_Path.c_str(), 256);
	mbstowcs_s(&written, entryPoint, pEntryPoint, 256);

	IDxcOperationResult* pCompileResult;
	HR(pCompiler->Compile(pSource, fileName, entryPoint, target, pArgs, sizeof(pArgs) / sizeof(pArgs[0]), dxcDefines.data(), (uint32_t)dxcDefines.size(), nullptr, &pCompileResult));

	HRESULT hrCompilation;
	pCompileResult->GetStatus(&hrCompilation);
	if (hrCompilation != S_OK)
	{
		IDxcBlobEncoding* pPrintBlob, * pRintBlob8;
		HR(pCompileResult->GetErrorBuffer(&pPrintBlob));
		pLibrary->GetBlobAsUtf8(pPrintBlob, &pRintBlob8);
		E_LOG(LogType::Error, "Shader compilation failed: %s", (char*)pRintBlob8->GetBufferPointer());
		pRintBlob8->Release();
		pPrintBlob->Release();
		return false;
	}

	pCompileResult->GetResult(m_pByteCodeDxc.GetAddressOf());

	pCompileResult->Release();
#else
	return false;
#endif
}

bool Shader::CompileFxc(const std::string& source, const char* pTarget, const char* pEntryPoint, const std::vector<std::string>& defines)
{
	uint32_t compileFlags = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
#if defined(_DEBUG)
	// shader debugging
	compileFlags |= (D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_PREFER_FLOW_CONTROL);
#else
	compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

	std::vector<D3D_SHADER_MACRO> shaderDefines;
	for (const std::string& define : defines)
	{
		D3D_SHADER_MACRO m;
		m.Name = define.c_str();
		m.Definition = "1";
		shaderDefines.push_back(m);
	}

	for (const auto& define : m_GlobalShaderDefines)
	{
		D3D_SHADER_MACRO m;
		m.Name = define.first.c_str();
		m.Definition = define.second.c_str();
		shaderDefines.push_back(m);
	}

	D3D_SHADER_MACRO endMacro;
	endMacro.Name = nullptr;
	endMacro.Definition = nullptr;
	shaderDefines.push_back(endMacro);

	ComPtr<ID3DBlob> pErrorBlob;
	D3DCompile(source.data(), source.size(), m_Path.c_str(), shaderDefines.data(), nullptr, pEntryPoint, pTarget, compileFlags, 0, m_pByteCode.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMessage((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << m_Path.c_str() << " --> " << errorMessage << std::endl;
		return false;
	}

	pErrorBlob.Reset();
	return true;
}

std::string Shader::GetShaderTarget(Type shaderType, char shaderModelMajor, char shaderModelMinor)
{
	char out[7];
	switch (shaderType)
	{
	case Type::VertexShader:
		sprintf_s(out, "vs_%d_%d", shaderModelMajor, shaderModelMinor);
		break;
	case Type::PixelShader:
		sprintf_s(out, "ps_%d_%d", shaderModelMajor, shaderModelMinor);
		break;
	case Type::GeometryShader:
		sprintf_s(out, "gs_%d_%d", shaderModelMajor, shaderModelMinor);
		break;
	case Type::ComputeShader:
		sprintf_s(out, "cs_%d_%d", shaderModelMajor, shaderModelMinor);
		break;
	case Type::MAX:
	default:
		return "";
	}

	return out;
}

void Shader::AddGlobalShaderDefine(const std::string& name, const std::string& value)
{
	m_GlobalShaderDefines.emplace_back(name, value);
}

void* Shader::GetByteCode() const
{
	if (m_pByteCodeDxc)
	{
		return m_pByteCodeDxc->GetBufferPointer();
	}

	return m_pByteCode->GetBufferPointer();
}

uint32_t Shader::GetByteCodeSize() const
{
	if (m_pByteCodeDxc)
	{
		return (uint32_t)m_pByteCodeDxc->GetBufferSize();
	}

	return (uint32_t)m_pByteCode->GetBufferSize();
}
