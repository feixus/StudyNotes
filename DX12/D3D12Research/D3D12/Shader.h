#pragma once

class Shader
{
public:
	Shader() = default;
	~Shader() = default;

	enum class Type : uint8_t
	{
		VertexShader,
		PixelShader,
		MAX
	};

	bool Load(const char* pFilePath, Type shaderType, const char* pEntryPoint);

	inline Type GetType() const { return m_Type; }
	inline void* GetByteCode() const { return m_pByteCode->GetBufferPointer(); }
	inline uint32_t GetByteCodeSize() const { return m_pByteCode->GetBufferSize(); }

private:
	ComPtr<ID3DBlob> m_pByteCode;
	Type m_Type;
};
