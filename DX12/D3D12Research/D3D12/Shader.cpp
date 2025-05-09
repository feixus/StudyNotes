#include "stdafx.h"
#include "Shader.h"

class D3DInclude : public ID3DInclude
{
public:
	D3DInclude(const std::string& basePath) : m_BasePath(basePath) {}

	HRESULT Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID* ppData, UINT* pBytes) override
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
	D3DCompile2(data.data(), data.size(), nullptr, nullptr, &extraInclude, pEntryPoint, shaderModel.c_str(), compileFlags, 0, 0, nullptr, 0, m_pByteCode.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMessage((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << pFilePath << " --> " << errorMessage << std::endl;
		return false;
	}

	pErrorBlob.Reset();

	//ShaderReflection();

	return true;
}

void Shader::ShaderReflection()
{
	ComPtr<ID3D12ShaderReflection> pShaderReflection;
	D3D12_SHADER_DESC shaderDesc;

	HR(D3DReflect(m_pByteCode->GetBufferPointer(), m_pByteCode->GetBufferSize(), IID_PPV_ARGS(pShaderReflection.GetAddressOf())));
	pShaderReflection->GetDesc(&shaderDesc);

	std::map<std::string, uint32_t> cbRegisterMap;

	for (unsigned int i = 0; i < shaderDesc.BoundResources; ++i)
	{
		D3D12_SHADER_INPUT_BIND_DESC resourceDesc{};
		pShaderReflection->GetResourceBindingDesc(i, &resourceDesc);

		switch (resourceDesc.Type)
		{
		case D3D_SIT_CBUFFER:
		case D3D_SIT_TBUFFER:
			cbRegisterMap[resourceDesc.Name] = resourceDesc.BindPoint;
			break;
		case D3D_SIT_TEXTURE:
		case D3D_SIT_SAMPLER:
		case D3D_SIT_STRUCTURED:
		case D3D_SIT_BYTEADDRESS:
		case D3D_SIT_UAV_RWTYPED:
		case D3D_SIT_UAV_RWSTRUCTURED:
		case D3D_SIT_UAV_RWBYTEADDRESS:
		case D3D_SIT_UAV_APPEND_STRUCTURED:
		case D3D_SIT_UAV_CONSUME_STRUCTURED:
		case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
		default:
			break;
		}
	}

	for (unsigned int i = 0; i < shaderDesc.BoundResources; ++i)
	{
		ID3D12ShaderReflectionConstantBuffer* pConstantBuffer = pShaderReflection->GetConstantBufferByIndex(i);
		D3D12_SHADER_BUFFER_DESC bufferDesc{};
		pConstantBuffer->GetDesc(&bufferDesc);
		uint32_t cbRegister = cbRegisterMap[bufferDesc.Name];

		for (unsigned int j = 0; j < bufferDesc.Variables; ++j)
		{
			ID3D12ShaderReflectionVariable* pVariable = pConstantBuffer->GetVariableByIndex(j);
			D3D12_SHADER_VARIABLE_DESC variableDesc{};
			pVariable->GetDesc(&variableDesc);
			
			ShaderParameter parameter{};
			parameter.Name = variableDesc.Name;
			parameter.Offset = variableDesc.StartOffset;
			parameter.Size = variableDesc.Size;
			m_Parameters[variableDesc.Name] = parameter;
		}
	}
}
