#include "stdafx.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "CommandQueue.h"
#include "DynamicResourceAllocator.h"

CommandContext::CommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, ID3D12CommandAllocator* pAllocator, D3D12_COMMAND_LIST_TYPE type)
	: m_pGraphics(pGraphics), m_pCommandList(pCommandList), m_pAllocator(pAllocator), m_Type(type)
{
}

CommandContext::~CommandContext()
{
}

void CommandContext::Reset()
{
	m_pAllocator = m_pGraphics->GetCommandQueue(m_Type)->RequestAllocator();
	m_pCommandList->Reset(m_pAllocator, nullptr);
}

uint64_t CommandContext::Execute(bool wait)
{
	FlushResourceBarriers();
	CommandQueue* pQueue = m_pGraphics->GetCommandQueue(m_Type);
	uint64_t fenceValue = pQueue->ExecuteCommandList(m_pCommandList);
	pQueue->FreeAllocator(fenceValue, m_pAllocator);

	if (wait)
	{
		pQueue->WaitForFence(fenceValue);
	}

	m_pGraphics->FreeCommandList(this);
	m_pGraphics->GetCpuVisibleAllocator()->Free(fenceValue);

	return fenceValue;
}

void CommandContext::Draw(int vertexStart, int vertexCount)
{
	PrepareDraw();
	m_pCommandList->DrawInstanced(vertexCount, 1, vertexStart, 0);
}

void CommandContext::DrawIndexed(int indexCount, int indexStart, int minVertex)
{
	PrepareDraw();
	m_pCommandList->DrawIndexedInstanced(indexCount, 1, indexStart, minVertex, 0);
}

void CommandContext::DrawIndexedInstanced(int indexCount, int indexStart, int instanceCount, int minVertex, int instanceStart)
{
	PrepareDraw();
	m_pCommandList->DrawIndexedInstanced(indexCount, instanceCount, indexStart, minVertex, instanceStart);
}

void CommandContext::ClearRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const DirectX::SimpleMath::Color& color /*= Color(0.2f, 0.2f, 0.2f, 1.0f)*/)
{
	m_pCommandList->ClearRenderTargetView(rtv, color, 0, nullptr);
}

void CommandContext::ClearDepth(D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS clearFlags /*= D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL*/, float depth /*= 1.0f*/, unsigned char stencil /*= 0*/)
{
	m_pCommandList->ClearDepthStencilView(dsv, clearFlags, depth, stencil, 0, nullptr);
}

void CommandContext::SetRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE* pRtv)
{
	m_pRenderTarget = pRtv;
}

void CommandContext::SetDepthStencil(D3D12_CPU_DESCRIPTOR_HANDLE* pDsv)
{
	m_pDepthStencilView = pDsv;
}

void CommandContext::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY type)
{
	m_pCommandList->IASetPrimitiveTopology(type);
}

void CommandContext::SetVertexBuffer(D3D12_VERTEX_BUFFER_VIEW pVertexBufferView)
{
	SetVertexBuffers(&pVertexBufferView, 1);
}

void CommandContext::SetVertexBuffers(D3D12_VERTEX_BUFFER_VIEW* pBuffers, int bufferCount)
{
	m_pCommandList->IASetVertexBuffers(0, bufferCount, pBuffers);
}

void CommandContext::SetIndexBuffer(D3D12_INDEX_BUFFER_VIEW indexBufferView)
{
	m_pCommandList->IASetIndexBuffer(&indexBufferView);
}

void CommandContext::SetViewport(const DirectX::SimpleMath::Rectangle& rect, float minDepth, float maxDepth)
{
	D3D12_VIEWPORT viewport;
	viewport.Height = (float)rect.width;
	viewport.Width = (float)rect.height;
	viewport.MinDepth = minDepth;
	viewport.MaxDepth = maxDepth;
	viewport.TopLeftX = (float)rect.x;
	viewport.TopLeftY = (float)rect.y;

	m_pCommandList->RSSetViewports(1, &viewport);
}

void CommandContext::SetScissorRect(const DirectX::SimpleMath::Rectangle& rect)
{
	D3D12_RECT r;
	r.left = rect.x;
	r.top = rect.y;
	r.right = rect.x + rect.width;
	r.bottom = rect.y + rect.height;

	m_pCommandList->RSSetScissorRects(1, &r);
}

void CommandContext::InsertResourceBarrier(D3D12_RESOURCE_BARRIER barrier, bool executeImmediate)
{
	if (m_NumQueueBarriers >= m_QueueBarriers.size())
	{
		FlushResourceBarriers();
	}

	m_QueueBarriers[m_NumQueueBarriers] = barrier;
	++m_NumQueueBarriers;
	if (executeImmediate)
	{
		FlushResourceBarriers();
	}
}

void CommandContext::FlushResourceBarriers()
{
	if (m_NumQueueBarriers > 0)
	{
		m_pCommandList->ResourceBarrier(m_NumQueueBarriers, m_QueueBarriers.data());
		m_NumQueueBarriers = 0;
	}
}

void CommandContext::SetDynamicConstantBufferView(int slot, void* pData, uint32_t dataSize)
{
	// alignment to a 256-byte boundary for constant buffers
	int bufferSize = (dataSize + 255) & ~255;
	DynamicAllocation allocation = m_pGraphics->GetCpuVisibleAllocator()->Allocate(bufferSize);
	memcpy(allocation.pMappedMemory, pData, bufferSize);
	m_pCommandList->SetGraphicsRootConstantBufferView(slot, allocation.GpuHandle);
}

void CommandContext::SetDynamicVertexBuffer(int slot, int elementCount, int elementSize, void* pData)
{
	int bufferSize = (elementCount * elementSize + 255) & ~255;
	DynamicAllocation allocation = m_pGraphics->GetCpuVisibleAllocator()->Allocate(bufferSize);
	memcpy(allocation.pMappedMemory, pData, elementCount * elementSize);
	D3D12_VERTEX_BUFFER_VIEW view{};
	view.BufferLocation = allocation.GpuHandle;
	view.SizeInBytes = elementCount * elementSize;
	view.StrideInBytes = elementSize;
	m_pCommandList->IASetVertexBuffers(slot, 1, &view);
}

void CommandContext::SetDynamicIndexBuffer(int elementCount, void* pData)
{
	int bufferSize = (elementCount * sizeof(uint32_t) + 255) & ~255;
	DynamicAllocation allocation = m_pGraphics->GetCpuVisibleAllocator()->Allocate(bufferSize);
	memcpy(allocation.pMappedMemory, pData, elementCount * sizeof(uint32_t));
	D3D12_INDEX_BUFFER_VIEW view{};
	view.BufferLocation = allocation.GpuHandle;
	view.SizeInBytes = elementCount * sizeof(uint32_t);
	view.Format = DXGI_FORMAT_R32_UINT;
	m_pCommandList->IASetIndexBuffer(&view);
}

void CommandContext::PrepareDraw()
{
	m_pCommandList->OMSetRenderTargets(1, m_pRenderTarget, false, m_pDepthStencilView);
}
