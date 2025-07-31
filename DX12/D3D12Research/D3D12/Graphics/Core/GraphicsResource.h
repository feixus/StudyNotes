#pragma once

class Graphics;

class GraphicsObject
{
public:
	GraphicsObject(Graphics* pParent = nullptr) : m_pGraphics(pParent) {}

	Graphics* GetGraphics() const { return m_pGraphics; }

protected:
	Graphics* m_pGraphics;
};

class GraphicsResource : public GraphicsObject
{
	friend class CommandContext;

public:
	GraphicsResource(Graphics* pParent);
	GraphicsResource(Graphics* pParent, ID3D12Resource* pResource, D3D12_RESOURCE_STATES state);
	virtual ~GraphicsResource();

	void Release();
	void SetName(const char* pName);
	std::string GetName() const;

	inline ID3D12Resource* GetResource() const { return m_pResource; }
	inline ID3D12Resource** GetResourceAddressOf() { return &m_pResource; }
	inline D3D12_GPU_VIRTUAL_ADDRESS GetGpuHandle() const { return m_pResource->GetGPUVirtualAddress(); }
	inline D3D12_RESOURCE_STATES GetResourceState() const { return m_CurrentState; }

	void PrintRefCount(std::string_view prefix);

protected:
	ID3D12Resource* m_pResource{nullptr};	// when use ComPtr, the render target created from swap chain, reference counter is confusing
	D3D12_RESOURCE_STATES m_CurrentState;
};