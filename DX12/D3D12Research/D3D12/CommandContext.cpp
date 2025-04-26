#include "stdafx.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "CommandQueue.h"
#include "DynamicResourceAllocator.h"
#include "GraphicsResource.h"

#if _DEBUG
#include <pix3.h>
#endif

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

uint64_t CommandContext::ExecuteAndReset(bool wait)
{
	FlushResourceBarriers();
	CommandQueue* pQueue = m_pGraphics->GetCommandQueue(m_Type);
	uint64_t fenceValue = pQueue->ExecuteCommandList(m_pCommandList);
	if (wait)
	{
		pQueue->WaitForFence(fenceValue);
	}
	
	m_pCommandList->Reset(m_pAllocator, nullptr);

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

DynamicAllocation CommandContext::AllocateUploadMemory(uint32_t size)
{
	return m_pGraphics->GetCpuVisibleAllocator()->Allocate(size);
}

void CommandContext::InitializeBuffer(GraphicsBuffer* pResource, void* pData, uint32_t dataSize)
{
	DynamicAllocation allocation = AllocateUploadMemory(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	InsertResourceBarrier(pResource, D3D12_RESOURCE_STATE_COPY_DEST, true);
	m_pCommandList->CopyBufferRegion(pResource->GetResource(), 0, allocation.pBackingResource, allocation.Offset, dataSize);
	InsertResourceBarrier(pResource, D3D12_RESOURCE_STATE_GENERIC_READ, true);
}

void CommandContext::SetViewport(const FloatRect& rect, float minDepth, float maxDepth)
{
	D3D12_VIEWPORT viewport;
	viewport.Height = (float)rect.GetHeight();
	viewport.Width = (float)rect.GetWidth();
	viewport.MinDepth = minDepth;
	viewport.MaxDepth = maxDepth;
	viewport.TopLeftX = (float)rect.Left;
	viewport.TopLeftY = (float)rect.Top;

	m_pCommandList->RSSetViewports(1, &viewport);
}

void CommandContext::SetScissorRect(const FloatRect& rect)
{
	D3D12_RECT r;
	r.left = rect.Left;
	r.top = rect.Top;
	r.right = rect.Right;
	r.bottom = rect.Bottom;

	m_pCommandList->RSSetScissorRects(1, &r);
}

void CommandContext::InsertResourceBarrier(GraphicsResource* pBuffer, D3D12_RESOURCE_STATES state, bool executeImmediate)
{
	if (pBuffer->GetResourceState() != state)
	{
		m_QueueBarriers[m_NumQueueBarriers] = CD3DX12_RESOURCE_BARRIER::Transition(
			pBuffer->GetResource(),
			pBuffer->GetResourceState(),
			state);

		++m_NumQueueBarriers;
		if (executeImmediate || m_NumQueueBarriers >= m_QueueBarriers.size())
		{
			FlushResourceBarriers();
		}

		pBuffer->SetResourceState(state);
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
	DynamicAllocation allocation = AllocateUploadMemory(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	m_pCommandList->SetGraphicsRootConstantBufferView(slot, allocation.GpuHandle);
}

void CommandContext::SetDynamicVertexBuffer(int slot, int elementCount, int elementSize, void* pData)
{
	int bufferSize = elementCount * elementSize;
	DynamicAllocation allocation = AllocateUploadMemory(bufferSize);
	memcpy(allocation.pMappedMemory, pData, bufferSize);
	D3D12_VERTEX_BUFFER_VIEW view{};
	view.BufferLocation = allocation.GpuHandle;
	view.SizeInBytes = bufferSize;
	view.StrideInBytes = elementSize;
	m_pCommandList->IASetVertexBuffers(slot, 1, &view);
}

void CommandContext::SetDynamicIndexBuffer(int elementCount, void* pData)
{
	int bufferSize = elementCount * sizeof(uint32_t);
	DynamicAllocation allocation = AllocateUploadMemory(bufferSize);
	memcpy(allocation.pMappedMemory, pData, bufferSize);
	D3D12_INDEX_BUFFER_VIEW view{};
	view.BufferLocation = allocation.GpuHandle;
	view.SizeInBytes = bufferSize;
	view.Format = DXGI_FORMAT_R32_UINT;
	m_pCommandList->IASetIndexBuffer(&view);
}

void CommandContext::MarkBegin(const wchar_t* pName)
{
#ifdef _DEBUG
	::PIXBeginEvent(m_pCommandList, 0, pName);
#endif
}

void CommandContext::MarkEvent(const wchar_t* pName)
{
#ifdef _DEBUG
	::PIXSetMarker(m_pCommandList, 0, pName);
#endif
}

void CommandContext::MarkEnd()
{
#ifdef _DEBUG
	::PIXEndEvent(m_pCommandList);
#endif
}

void CommandContext::PrepareDraw()
{
	m_pCommandList->OMSetRenderTargets(1, m_pRenderTarget, false, m_pDepthStencilView);
}
