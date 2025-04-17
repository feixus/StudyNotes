#include "LinearAllocator.h"

LinearAllocator::LinearAllocator(Graphics* pGraphics)
{
	m_PagesManagers.reserve(2);
	m_PagesManagers[0] = make_unique<LinearAllocatorPageManager>(pGraphics, LinearAllocationType::GpuExclusive);
	m_PagesManagers[1] = make_unique<LinearAllocatorPageManager>(pGraphics, LinearAllocationType::CpuWrite);
}

LinearAllocator::~LinearAllocator()
{
}

DynamicAllocation LinearAllocator::Allocate(const LinearAllocationType type, size_t size, const size_t alignment)
{
	if (alignment)
	{
		size = (size + alignment - 1) - (size + alignment - 1) % alignment;
	}

	if (m_pCurrentPage == nullptr || m_CurrentOffset + size > m_pCurrentPage->GetSize())
	{
		m_pCurrentPage = m_PagesManagers[(int)type]->RequestPage();
		m_CurrentOffset = 0;
	}

	DynamicAllocation allocation(m_pCurrentPage->GetResource(), m_CurrentOffset, size);
	allocation.GpuAddress = m_pCurrentPage->m_pGpuAddress + m_CurrentOffset;
	allocation.pCpuAddress = (char*)m_pCurrentPage->m_pCpuAddress + m_CurrentOffset;

	m_CurrentOffset += size;

	return allocation;
}

LinearAllocationPage* LinearAllocatorPageManager::RequestPage()
{
	while (!m_RetiredPages.empty())
	{

	}

	return nullptr;
}

LinearAllocationPage* LinearAllocatorPageManager::CreateNewPage(const size_t size)
{
	return nullptr;
}
