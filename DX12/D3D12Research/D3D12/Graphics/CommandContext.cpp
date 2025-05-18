#include "stdafx.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "CommandQueue.h"
#include "DynamicResourceAllocator.h"
#include "DynamicDescriptorAllocator.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "GraphicsTexture.h"
#include "GraphicsBuffer.h"

#if _DEBUG
#include <pix3.h>
#endif

constexpr int VALID_COMPUTE_QUEUE_RESOURCE_STATES = D3D12_RESOURCE_STATE_COMMON |
													D3D12_RESOURCE_STATE_UNORDERED_ACCESS | 
													D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | 
													D3D12_RESOURCE_STATE_COPY_DEST | 
													D3D12_RESOURCE_STATE_COPY_SOURCE;
constexpr int VALID_COPY_QUEUE_RESOURCE_STATES = D3D12_RESOURCE_STATE_COMMON |
												 D3D12_RESOURCE_STATE_COPY_DEST | 
												 D3D12_RESOURCE_STATE_COPY_SOURCE;													

#pragma region BASE

CommandContext::CommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, ID3D12CommandAllocator* pAllocator)
	: m_pGraphics(pGraphics), m_pCommandList(pCommandList), m_pAllocator(pAllocator)
{
}

CommandContext::~CommandContext()
{
}

void CommandContext::Reset()
{
	assert(m_pCommandList && m_pAllocator == nullptr);

	m_pAllocator = m_pGraphics->GetCommandQueue(m_Type)->RequestAllocator();
	m_pCommandList->Reset(m_pAllocator, nullptr);
	m_NumQueueBarriers = 0;
}

uint64_t CommandContext::Execute(bool wait)
{
	FlushResourceBarriers();
	CommandQueue* pQueue = m_pGraphics->GetCommandQueue(m_Type);
	uint64_t fenceValue = pQueue->ExecuteCommandList(m_pCommandList);
	
	pQueue->FreeAllocator(fenceValue, m_pAllocator);
	m_pAllocator = nullptr;

	m_pGraphics->GetCpuVisibleAllocator()->Free(fenceValue);

	if (wait)
	{
		pQueue->WaitForFence(fenceValue);
	}

	m_pGraphics->FreeCommandList(this);

	return fenceValue;
}

uint64_t CommandContext::ExecuteAndReset(bool wait)
{
	FlushResourceBarriers();
	CommandQueue* pQueue = m_pGraphics->GetCommandQueue(m_Type);
	uint64_t fenceValue = pQueue->ExecuteCommandList(m_pCommandList);

	m_pGraphics->GetCpuVisibleAllocator()->Free(fenceValue);
	if (wait)
	{
		pQueue->WaitForFence(fenceValue);
	}
	
	m_pCommandList->Reset(m_pAllocator, nullptr);

	return fenceValue;
}

void CommandContext::InsertResourceBarrier(GraphicsResource* pBuffer, D3D12_RESOURCE_STATES state, bool executeImmediate)
{
	if (pBuffer->GetResourceState() != state)
	{
		if (m_Type == D3D12_COMMAND_LIST_TYPE_COMPUTE)
		{
			D3D12_RESOURCE_STATES currentState = pBuffer->GetResourceState();
			assert((currentState & VALID_COMPUTE_QUEUE_RESOURCE_STATES) == currentState);
			assert((state & VALID_COMPUTE_QUEUE_RESOURCE_STATES) == state);
		}
		else if (m_Type == D3D12_COMMAND_LIST_TYPE_COPY)
		{
			D3D12_RESOURCE_STATES currentState = pBuffer->GetResourceState();
			assert((currentState & VALID_COPY_QUEUE_RESOURCE_STATES) == currentState);
			assert((state & VALID_COPY_QUEUE_RESOURCE_STATES) == state);
		}

		m_QueueBarriers[m_NumQueueBarriers] = CD3DX12_RESOURCE_BARRIER::Transition(
			pBuffer->GetResource(),
			pBuffer->GetResourceState(),
			state);

		++m_NumQueueBarriers;
		if (executeImmediate || m_NumQueueBarriers >= m_QueueBarriers.size())
		{
			FlushResourceBarriers();
		}

		pBuffer->m_CurrentState = state;
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

DynamicAllocation CommandContext::AllocateUploadMemory(uint32_t size)
{
	return m_pGraphics->GetCpuVisibleAllocator()->Allocate(size);
}

void CommandContext::InitializeBuffer(GraphicsBuffer* pResource, const void* pData, uint32_t dataSize, uint32_t offset)
{
	DynamicAllocation allocation = AllocateUploadMemory(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	D3D12_RESOURCE_STATES previousState = pResource->GetResourceState();
	InsertResourceBarrier(pResource, D3D12_RESOURCE_STATE_COPY_DEST, true);
	m_pCommandList->CopyBufferRegion(pResource->GetResource(), offset, allocation.pBackingResource, allocation.Offset, dataSize);
	InsertResourceBarrier(pResource, previousState, true);
}

void CommandContext::InitializeTexture(GraphicsTexture* pResource, D3D12_SUBRESOURCE_DATA* pSubresources, int firstSubresource, int subresourceCount)
{
	uint64_t allocationSize = (uint32_t)GetRequiredIntermediateSize(pResource->GetResource(), (UINT)firstSubresource, (UINT)subresourceCount);
	DynamicAllocation allocation = m_pGraphics->GetCpuVisibleAllocator()->Allocate((uint32_t)allocationSize, 512);
	D3D12_RESOURCE_STATES previousState = pResource->GetResourceState();
	InsertResourceBarrier(pResource, D3D12_RESOURCE_STATE_COPY_DEST, true);
	UpdateSubresources(m_pCommandList, pResource->GetResource(), allocation.pBackingResource, allocation.Offset, firstSubresource, subresourceCount, pSubresources);
	InsertResourceBarrier(pResource, previousState, true);
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

void CommandContext::SetName(const char* pName)
{
	SetD3DObjectName(m_pCommandList, pName);
}

#pragma endregion

#pragma region Graphics

GraphicsCommandContext::GraphicsCommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandlist, ID3D12CommandAllocator* pAllocator)
	: ComputeCommandContext(pGraphics, pCommandlist, pAllocator)
{
	m_CurrentContext = CommandListContext::Graphics;
	m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
}

void GraphicsCommandContext::SetGraphicsRootSignature(RootSignature* pRootSignature)
{
	assert(m_CurrentContext == CommandListContext::Graphics);
	m_pCommandList->SetGraphicsRootSignature(pRootSignature->GetRootSignature());
	m_pShaderResourceDescriptorAllocator->ParseRootSignature(pRootSignature);
	m_pSamplerDescriptorAllocator->ParseRootSignature(pRootSignature);
}

void GraphicsCommandContext::SetGraphicsPipelineState(GraphicsPipelineState* pPipelineState)
{
	m_pCommandList->SetPipelineState(pPipelineState->GetPipelineState());
	if (m_CurrentContext != CommandListContext::Graphics)
	{
		Reset();
		m_CurrentContext = CommandListContext::Graphics;
	}
}

void GraphicsCommandContext::SetDynamicConstantBufferView(int rootIndex, void* pData, uint32_t dataSize)
{
	DynamicAllocation allocation = AllocateUploadMemory(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	m_pCommandList->SetGraphicsRootConstantBufferView(rootIndex, allocation.GpuHandle);
}

void GraphicsCommandContext::SetDynamicVertexBuffer(int rootIndex, int elementCount, int elementSize, void* pData)
{
	assert(m_CurrentContext == CommandListContext::Graphics);
	int bufferSize = elementCount * elementSize;
	DynamicAllocation allocation = AllocateUploadMemory(bufferSize);
	memcpy(allocation.pMappedMemory, pData, bufferSize);

	D3D12_VERTEX_BUFFER_VIEW view = {};
	view.BufferLocation = allocation.GpuHandle;
	view.SizeInBytes = bufferSize;
	view.StrideInBytes = elementSize;
	m_pCommandList->IASetVertexBuffers(rootIndex, 1, &view);
}

void GraphicsCommandContext::SetDynamicIndexBuffer(int elementCount, void* pData)
{
	assert(m_CurrentContext == CommandListContext::Graphics);
	int bufferSize = elementCount * sizeof(uint32_t);
	DynamicAllocation allocation = AllocateUploadMemory(bufferSize);
	memcpy(allocation.pMappedMemory, pData, bufferSize);

	D3D12_INDEX_BUFFER_VIEW view = {};
	view.BufferLocation = allocation.GpuHandle;
	view.SizeInBytes = allocation.Size;
	view.Format = DXGI_FORMAT_R32_UINT;
	m_pCommandList->IASetIndexBuffer(&view);
}

void GraphicsCommandContext::SetGraphicsRootConstants(int rootIndex, uint32_t count, const void* pConstants)
{
	assert(m_CurrentContext == CommandListContext::Graphics);
	m_pCommandList->SetGraphicsRoot32BitConstants(rootIndex, count, pConstants, 0);
}

void GraphicsCommandContext::Draw(int vertexStart, int vertexCount)
{
	FlushResourceBarriers();
	m_pShaderResourceDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Graphics);
	m_pSamplerDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Graphics);
	m_pCommandList->DrawInstanced(vertexCount, 1, vertexStart, 0);
}

void GraphicsCommandContext::DrawIndexed(int indexCount, int indexStart, int minVertex)
{
	FlushResourceBarriers();
	m_pShaderResourceDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Graphics);
	m_pSamplerDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Graphics);
	m_pCommandList->DrawIndexedInstanced(indexCount, 1, indexStart, minVertex, 0);
}

void GraphicsCommandContext::DrawIndexedInstanced(int indexCount, int indexStart, int instanceCount, int minVertex, int instanceStart)
{
	FlushResourceBarriers();
	m_pShaderResourceDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Graphics);
	m_pSamplerDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Graphics);
	m_pCommandList->DrawIndexedInstanced(indexCount, instanceCount, indexStart, minVertex, instanceStart);
}

void GraphicsCommandContext::ClearRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const DirectX::SimpleMath::Color& color /*= Color(0.2f, 0.2f, 0.2f, 1.0f)*/)
{
	m_pCommandList->ClearRenderTargetView(rtv, color, 0, nullptr);
}

void GraphicsCommandContext::ClearDepth(D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS clearFlags /*= D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL*/, float depth /*= 1.0f*/, unsigned char stencil /*= 0*/)
{
	m_pCommandList->ClearDepthStencilView(dsv, clearFlags, depth, stencil, 0, nullptr);
}

void GraphicsCommandContext::SetDepthOnlyTarget(D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
	SetRenderTargets(nullptr, dsv);
}

void GraphicsCommandContext::SetRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
	SetRenderTargets(&rtv, dsv);
}

void GraphicsCommandContext::SetRenderTargets(D3D12_CPU_DESCRIPTOR_HANDLE* pRtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
	assert(m_CurrentContext == CommandListContext::Graphics);
	if (pRtv)
	{
		m_pCommandList->OMSetRenderTargets(1, pRtv, false, &dsv);
	}
	else
	{
		m_pCommandList->OMSetRenderTargets(0, nullptr, false, &dsv);
	}
}

void GraphicsCommandContext::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY type)
{
	m_pCommandList->IASetPrimitiveTopology(type);
}

void GraphicsCommandContext::SetVertexBuffer(VertexBuffer* pVertexBuffer)
{
	SetVertexBuffers(pVertexBuffer, 1);
}

void GraphicsCommandContext::SetVertexBuffers(VertexBuffer* pVertexBuffers, int bufferCount)
{
	assert(bufferCount <= 4);
	std::array<D3D12_VERTEX_BUFFER_VIEW, 4> views{};
	for (int i = 0; i < bufferCount; ++i)
	{
		views[i] = pVertexBuffers->GetView();
	}
	m_pCommandList->IASetVertexBuffers(0, bufferCount, views.data());
}

void GraphicsCommandContext::SetIndexBuffer(IndexBuffer* pIndexBuffer)
{
	const D3D12_INDEX_BUFFER_VIEW view = pIndexBuffer->GetView();
	m_pCommandList->IASetIndexBuffer(&view);
}

void GraphicsCommandContext::SetViewport(const FloatRect& rect, float minDepth, float maxDepth)
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

void GraphicsCommandContext::SetScissorRect(const FloatRect& rect)
{
	D3D12_RECT r;
	r.left = (LONG)rect.Left;
	r.top = (LONG)rect.Top;
	r.right = (LONG)rect.Right;
	r.bottom = (LONG)rect.Bottom;

	m_pCommandList->RSSetScissorRects(1, &r);
}

#pragma endregion

#pragma region COMPUTE

ComputeCommandContext::ComputeCommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandlist, ID3D12CommandAllocator* pAllocator)
	: CommandContext(pGraphics, pCommandlist, pAllocator)
{
	m_CurrentContext = CommandListContext::Compute;
	m_Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
	m_pShaderResourceDescriptorAllocator = std::make_unique<DynamicDescriptorAllocator>(pGraphics, this, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_pSamplerDescriptorAllocator = std::make_unique<DynamicDescriptorAllocator>(pGraphics, this, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
}

void ComputeCommandContext::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
	assert(m_CurrentContext == CommandListContext::Compute);
	FlushResourceBarriers();
	m_pShaderResourceDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Compute);
	m_pSamplerDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Compute);
	m_pCommandList->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void ComputeCommandContext::Reset()
{
	CommandContext::Reset();
	BindDescriptorHeaps();
}

uint64_t ComputeCommandContext::Execute(bool wait)
{
	uint64_t fenceValue = CommandContext::Execute(wait);
	if (m_pShaderResourceDescriptorAllocator)
	{
		m_pShaderResourceDescriptorAllocator->ReleaseUsedHeaps(fenceValue);
	}
	if (m_pSamplerDescriptorAllocator)
	{
		m_pSamplerDescriptorAllocator->ReleaseUsedHeaps(fenceValue);
	}
	return fenceValue;
}

uint64_t ComputeCommandContext::ExecuteAndReset(bool wait)
{
	uint64_t fenceValue = CommandContext::ExecuteAndReset(wait);
	m_CurrentDescriptorHeaps = {};
	return fenceValue;
}

void ComputeCommandContext::SetComputePipelineState(ComputePipelineState* pPipelineState)
{
	m_pCommandList->SetPipelineState(pPipelineState->GetPipelineState());
	if (m_CurrentContext != CommandListContext::Compute)
	{
		Reset();
		m_CurrentContext = CommandListContext::Compute;
	}
}

void ComputeCommandContext::SetComputeRootSignature(RootSignature* pRootSignature)
{
	assert(m_CurrentContext == CommandListContext::Compute);
	m_pCommandList->SetComputeRootSignature(pRootSignature->GetRootSignature());
	m_pShaderResourceDescriptorAllocator->ParseRootSignature(pRootSignature);
	m_pSamplerDescriptorAllocator->ParseRootSignature(pRootSignature);
}

void ComputeCommandContext::SetComputeRootConstants(int rootIndex, uint32_t count, const void* pConstants)
{
	assert(m_CurrentContext == CommandListContext::Compute);
	m_pCommandList->SetComputeRoot32BitConstants(rootIndex, count, pConstants, 0);
}

void ComputeCommandContext::SetComputeDynamicConstantBufferView(int rootIndex, void* pData, uint32_t dataSize)
{
	assert(m_CurrentContext == CommandListContext::Compute);
	DynamicAllocation allocation = AllocateUploadMemory(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	m_pCommandList->SetComputeRootConstantBufferView(rootIndex, allocation.GpuHandle);
}

void ComputeCommandContext::SetDynamicDescriptor(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
	m_pShaderResourceDescriptorAllocator->SetDescriptors(rootIndex, offset, 1, &handle);
}

void ComputeCommandContext::SetDynamicDescriptors(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE* handles, int count)
{
	m_pShaderResourceDescriptorAllocator->SetDescriptors(rootIndex, offset, count, handles);
}

void ComputeCommandContext::SetDynamicSamplerDescriptor(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
	m_pSamplerDescriptorAllocator->SetDescriptors(rootIndex, offset, 1, &handle);
}

void ComputeCommandContext::SetDynamicSamplerDescriptors(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE* handles, int count)
{
	m_pSamplerDescriptorAllocator->SetDescriptors(rootIndex, offset, 1, handles);
}

void ComputeCommandContext::SetDescriptorHeap(ID3D12DescriptorHeap* pHeap, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
	if (m_CurrentDescriptorHeaps[(int)type] != pHeap)
	{
		m_CurrentDescriptorHeaps[(int)type] = pHeap;
		BindDescriptorHeaps();
	}
}

void ComputeCommandContext::BindDescriptorHeaps()
{
	std::array<ID3D12DescriptorHeap*, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> heapsToBind{};
	int heapCount = 0;
	for (size_t i = 0; i < heapsToBind.size(); ++i)
	{
		if (m_CurrentDescriptorHeaps[i] != nullptr)
		{
			heapsToBind[heapCount++] = m_CurrentDescriptorHeaps[i];
		}
	}

	if (heapCount > 0)
	{
		m_pCommandList->SetDescriptorHeaps(heapCount, heapsToBind.data());
	}
}

#pragma endregion

#pragma region COPY

CopyCommandContext::CopyCommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandlist, ID3D12CommandAllocator* pAllocator)
	: CommandContext(pGraphics, pCommandlist, pAllocator)
{
	m_Type = D3D12_COMMAND_LIST_TYPE_COPY;
}

#pragma endregion
