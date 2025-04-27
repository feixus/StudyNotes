#include "stdafx.h"
#include "Shader.h"

bool Shader::Load(const char* pFilePath, Type shaderType, const char* pEntryPoint)
{
#if defined(_DEBUG)
	// shader debugging
	uint32_t compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	uint32_t compileFlags = 0;
#endif

	compileFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

	std::vector<std::byte> data = ReadFile(pFilePath);

	std::string shaderModel = "";
	switch (shaderType)
	{
	case Type::VertexShader:
		shaderModel = "vs_5_0";
		break;
	case Type::PixelShader:
		shaderModel = "ps_5_0";
		break;
	case Type::MAX:
	default:
		return false;
	}
	m_Type = shaderType;

	ComPtr<ID3DBlob> pErrorBlob;
	D3DCompile2(data.data(), data.size(), nullptr, nullptr, nullptr, pEntryPoint, shaderModel.c_str(), compileFlags, 0, 0, nullptr, 0, m_pByteCode.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMessage((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << errorMessage << std::endl;
		return false;
	}

	pErrorBlob.Reset();
	return true;
}
