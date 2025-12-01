#pragma once

class GraphicsDevice;
class ResourceView;

constexpr D3D12_RESOURCE_STATES D3D12_RESOURCE_STATE_UNKNOWN = (D3D12_RESOURCE_STATES) - 1;

class ResourceState
{
public:
	ResourceState(D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_UNKNOWN) : m_CommonState(initialState), m_AllSameState(true) {}

	void Set(D3D12_RESOURCE_STATES state, uint32_t subResource)
	{
		if (subResource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			check(subResource < m_ResourceStates.size());
			if (m_AllSameState)
			{
				for (D3D12_RESOURCE_STATES& s : m_ResourceStates)
				{
					s = m_CommonState;
				}
				m_AllSameState = false;
			}
			m_ResourceStates[subResource] = state;
		}
		else
		{
			m_AllSameState = true;
			m_CommonState = state;
		}
	}

	D3D12_RESOURCE_STATES Get(uint32_t subResource) const
	{
		check(m_AllSameState || subResource < (uint32_t)m_ResourceStates.size());
		return m_AllSameState ? m_CommonState : m_ResourceStates[subResource];
	}

private:
	bool m_AllSameState;
	D3D12_RESOURCE_STATES m_CommonState;
	constexpr static uint32_t MAX_SUBRESOURCES = 12;
	std::array<D3D12_RESOURCE_STATES, MAX_SUBRESOURCES> m_ResourceStates{};
};

class GraphicsObject
{
public:
	GraphicsObject(GraphicsDevice* pParent = nullptr) : m_pGraphics(pParent) {}

	inline GraphicsDevice* GetGraphics() const { return m_pGraphics; }

private:
	GraphicsDevice* m_pGraphics;
};

class GraphicsResource : public GraphicsObject
{
	friend class CommandContext;

public:
	GraphicsResource(GraphicsDevice* pParent);
	GraphicsResource(GraphicsDevice* pParent, ID3D12Resource* pResource, D3D12_RESOURCE_STATES state);
	virtual ~GraphicsResource();

	void* Map(uint32_t subResource = 0, uint64_t readFrom = 0, uint64_t readTo = 0);
	void UnMap(uint32_t subResource = 0, uint64_t writeFrom = 0, uint64_t writeTo = 0);
	void* GetMappedData() const { return m_pMappedData; }

	void Release();
	void SetName(const char* pName);
	const std::string& GetName() { return m_Name;};

	inline ID3D12Resource* GetResource() const { return m_pResource; }
	inline D3D12_GPU_VIRTUAL_ADDRESS GetGpuHandle() const { return m_pResource->GetGPUVirtualAddress(); }

	void SetResourceState(D3D12_RESOURCE_STATES state, uint32_t subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
	{
		m_ResourceState.Set(state, subResource);
	}
	inline D3D12_RESOURCE_STATES GetResourceState(uint32_t subResource = 0) const 
	{ 
		return m_ResourceState.Get(subResource); 
	}

	void PrintRefCount(std::string_view prefix);

protected:
	std::string m_Name;
	ID3D12Resource* m_pResource{nullptr};	// when use ComPtr, the render target created from swap chain, reference counter is confusing
	void* m_pMappedData{nullptr};
	std::vector<std::unique_ptr<ResourceView>> m_Descriptors;	
	ResourceState m_ResourceState;
};
