#include "stdafx.h"
#include "Shader.h"

class D3DInclude : public ID3DInclude
{
public:
	D3DInclude(const std::string& basePath) : m_BasePath(basePath) {}

	HRESULT Open(D3D_INCLUDE_TYPE /*IncludeType*/, LPCSTR pFileName, LPCVOID /*pParentData*/, LPCVOID* ppData, UINT* pBytes) override
	{
		std::string fullPath = m_BasePath + "/" + std::string(pFileName);
		std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
		if (!file) return E_FAIL;

		std::streamsize size = file.tellg();
		file.seekg(0, std::ios::beg);

		auto buffer = std::make_unique<std::vector<char>>(size);
		if (!file.read(buffer->data(), size)) {
			return E_FAIL;
		}

		*ppData = buffer->data();
		*pBytes = static_cast<UINT>(size);

		// Store the buffer so we can free it later
		activeBuffers[*ppData] = std::move(buffer);

		return S_OK;
	}

	HRESULT Close(LPCVOID pData) override
	{
		auto it = activeBuffers.find(pData);
		if (it != activeBuffers.end()) {
			activeBuffers.erase(it);  // deletes the unique_ptr, frees memory
			return S_OK;
		}
		return E_FAIL;  // trying to close unknown pointer
	}

private:
	std::string m_BasePath;

	std::unordered_map<const void*, std::unique_ptr<std::vector<char>>> activeBuffers;
};

bool Shader::Load(const char* pFilePath, Type shaderType, const char* pEntryPoint, const std::vector<std::string> defines)
{
	uint32_t compileFlags = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
#if defined(_DEBUG)
	// shader debugging
	compileFlags |= (D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_PREFER_FLOW_CONTROL);
#else
	compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

	std::vector<std::byte> data = ReadFile(pFilePath);

	std::vector<D3D_SHADER_MACRO> shaderDefines;
	for (const std::string& define : defines)
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

	std::string shaderModel = "";
	switch (shaderType)
	{
	case Type::VertexShader:
		shaderModel = "vs_5_0";
		break;
	case Type::PixelShader:
		shaderModel = "ps_5_0";
		break;
	case Type::ComputeShader:
		shaderModel = "cs_5_0";
		break;
	case Type::MAX:
	default:
		return false;
	}
	m_Type = shaderType;

	std::string filePath = pFilePath;
	D3DInclude extraInclude(filePath.substr(0, filePath.rfind('/') + 1));

	ComPtr<ID3DBlob> pErrorBlob;
	D3DCompile2(data.data(), data.size(), nullptr, shaderDefines.data(), &extraInclude, pEntryPoint, shaderModel.c_str(), compileFlags, 0, 0, nullptr, 0, m_pByteCode.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMessage((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << pFilePath << " --> " << errorMessage << std::endl;
		return false;
	}

	pErrorBlob.Reset();

	return true;
}
