#include "stdafx.h"
#include "GraphicsResource.h"

GraphicsResource::GraphicsResource()
	: m_pResource(nullptr), m_CurrentState(D3D12_RESOURCE_STATE_COMMON)
{
}

GraphicsResource::GraphicsResource(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state)
	: m_pResource(pResource), m_CurrentState(state)
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

void GraphicsResource::SetName(const char* pName)
{
	SetD3DObjectName(m_pResource, pName);
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
