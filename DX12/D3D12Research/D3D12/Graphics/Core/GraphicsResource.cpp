#include "stdafx.h"
#include "GraphicsResource.h"
#include "ResourceViews.h"

GraphicsResource::GraphicsResource(Graphics* pParent)
	: GraphicsObject(pParent), m_pResource(nullptr), m_ResourceState(D3D12_RESOURCE_STATE_COMMON)
{
}

GraphicsResource::GraphicsResource(Graphics* pParent, ID3D12Resource* pResource, D3D12_RESOURCE_STATES state)
	: GraphicsObject(pParent), m_pResource(pResource), m_ResourceState(state)
{
}

GraphicsResource::~GraphicsResource()
{
	Release();
}

void GraphicsResource::Release()
{
	if (m_pResource)
	{
		m_pResource->Release();
		m_pResource = nullptr;
	}
}

void* GraphicsResource::Map(uint32_t subResource /*= 0*/, uint64_t readFrom /*= 0*/, uint64_t readTo /*= 0*/)
{
	check(m_pResource);
	check(m_pMappedData == nullptr);

	CD3DX12_RANGE range(readFrom, readTo);
	m_pResource->Map(subResource, &range, &m_pMappedData);
	return m_pMappedData;
}

void GraphicsResource::UnMap(uint32_t subResource /*= 0*/, uint64_t writeFrom /*= 0*/, uint64_t writeTo /*= 0*/)
{
	check(m_pResource);

	CD3DX12_RANGE range(writeFrom, writeTo);
	m_pResource->Unmap(subResource, &range);
	m_pMappedData = nullptr;
}

void GraphicsResource::SetName(const char* pName)
{
	D3D_SETNAME(m_pResource, pName);
}

std::string GraphicsResource::GetName() const
{
	if (m_pResource)
	{
		uint32_t size = 0;
		m_pResource->GetPrivateData(WKPDID_D3DDebugObjectName, &size, nullptr);
		std::string str(size, '\0');
		m_pResource->GetPrivateData(WKPDID_D3DDebugObjectName, &size, str.data());
		return str;
	}
	return "";
}

void GraphicsResource::PrintRefCount(std::string_view prefix)
{
	if (!m_pResource)
	{
		return;
	}

	ULONG refCount = m_pResource->AddRef();
	m_pResource->Release();

	std::cout << prefix << " Reference Count: " << refCount << std::endl;
}
