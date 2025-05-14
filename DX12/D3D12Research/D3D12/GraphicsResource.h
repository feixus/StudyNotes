#pragma once
class CommandContext;
class Graphics;

class GraphicsResource
{
public:
	GraphicsResource() {}
	GraphicsResource(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state)
		: m_pResource(pResource), m_CurrentState(state) {}

	virtual ~GraphicsResource()
	{
		Release();
	}

	void Release()
	{
		if (m_pResource)
		{
			m_pResource->Release();
			m_pResource = nullptr;
		}
	}

	void SetName(const std::string& name);

	ID3D12Resource* GetResource() const { return m_pResource; }
	ID3D12Resource** GetResourceAddressOf() { return &m_pResource; }
	D3D12_GPU_VIRTUAL_ADDRESS GetGpuHandle() const { return m_pResource->GetGPUVirtualAddress(); }
	D3D12_RESOURCE_STATES GetResourceState() const { return m_CurrentState; }
	void SetResourceState(D3D12_RESOURCE_STATES state) { m_CurrentState = state; }

protected:
	ID3D12Resource* m_pResource {};
	D3D12_RESOURCE_STATES m_CurrentState {};
};

class GraphicsBuffer : public GraphicsResource
{
public:
	void Create(ID3D12Device* pDevice, uint32_t size, bool cpuVisible = false, bool unorderedAccess = false);
	void SetData(CommandContext* pContext, void* pData, uint32_t dataSize, uint32_t offset = 0);

	uint32_t GetSize() const { return m_Size; }

protected:
	uint32_t m_Size{0};
};

class StructuredBuffer : public GraphicsBuffer
{
public:
	void Create(Graphics* pGraphics, uint32_t elementStride, uint32_t elementCount, bool cpuVisible = false);

	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const { return m_Srv; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV() const { return m_Uav; }

private:
	D3D12_CPU_DESCRIPTOR_HANDLE m_Srv{};
	D3D12_CPU_DESCRIPTOR_HANDLE m_Uav{};
};

enum class TextureUsage
{
	UnorderedAccess				= 1 << 1,
	ShaderResource				= 1 << 2,
	RenderTarget				= 1 << 3,
	DepthStencil				= 1 << 4,
};
DEFINE_ENUM_FLAG_OPERATORS(TextureUsage)

class GraphicsTexture : public GraphicsResource
{
public:
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() { return m_Srv; }
	int GetRowDataSize(unsigned int width) const;

	int GetWidth() const { return m_Width; }
	int GetHeight() const { return m_Height; }
	int GetMipLevels() const { return m_MipLevels; }

protected:
	static DXGI_FORMAT GetDepthFormat(DXGI_FORMAT format);

	int m_SampleCount{1};
	int m_Width{0};
	int m_Height{0};
	DXGI_FORMAT m_Format{};
	int m_MipLevels{1};
	D3D12_CPU_DESCRIPTOR_HANDLE m_Srv{};
};

class GraphicsTexture2D : public GraphicsTexture
{
public:
	void Create(Graphics* pGraphics, CommandContext* pContext, const char* filePath, TextureUsage usage);
	void Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage, int sampleCount);
	void SetData(CommandContext* pContext, const void* pData);
	void CreateForSwapChain(Graphics* pGraphics, ID3D12Resource* pTexture);

	D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() { return m_Rtv; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() { return m_Rtv; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV() { return m_Uav; }

private:
	D3D12_CPU_DESCRIPTOR_HANDLE m_Rtv{};
	D3D12_CPU_DESCRIPTOR_HANDLE m_Uav{};
};

enum class CubeMapFace
{
	POSITIVE_X = 0,
	NEGATIVE_X,
	POSITIVE_Y,
	NEGATIVE_Y,
	POSITIVE_Z,
	NEGATIVE_Z,
	MAX
};

class GraphicsTextureCube : public GraphicsTexture
{
public:
	void Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage, int sampleCount);
	D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(CubeMapFace face) { return m_Rtv[(int)face]; }
	
private:
	D3D12_CPU_DESCRIPTOR_HANDLE m_Rtv[8]{};
};