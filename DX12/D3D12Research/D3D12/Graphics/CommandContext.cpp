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


constexpr int VALID_COMPUTE_QUEUE_RESOURCE_STATES = D3D12_RESOURCE_STATE_COMMON |
													D3D12_RESOURCE_STATE_UNORDERED_ACCESS | 
													D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | 
													D3D12_RESOURCE_STATE_COPY_DEST | 
													D3D12_RESOURCE_STATE_COPY_SOURCE |
													D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
constexpr int VALID_COPY_QUEUE_RESOURCE_STATES = D3D12_RESOURCE_STATE_COMMON |
												 D3D12_RESOURCE_STATE_COPY_DEST | 
												 D3D12_RESOURCE_STATE_COPY_SOURCE;													

#define USE_RENDERPASSES 1
#ifndef USE_RENDERPASSES
#define USE_RENDERPASSES 0
#endif

#pragma region BASE

CommandContext::CommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, ID3D12CommandAllocator* pAllocator)
	: m_pGraphics(pGraphics), m_pCommandList(pCommandList), m_pAllocator(pAllocator)
{
	m_DynamicAllocator = std::make_unique<DynamicResourceAllocator>(pGraphics->GetAllocationManager());
}

CommandContext::~CommandContext()
{
}

void CommandContext::Reset()
{
	assert(m_pCommandList);
	if (m_pAllocator == nullptr)
	{
		m_pAllocator = m_pGraphics->GetCommandQueue(m_Type)->RequestAllocator();
		m_pCommandList->Reset(m_pAllocator, nullptr);
	}

	m_NumQueueBarriers = 0;
}

uint64_t CommandContext::Execute(bool wait)
{
	FlushResourceBarriers();
	CommandQueue* pQueue = m_pGraphics->GetCommandQueue(m_Type);
	uint64_t fenceValue = pQueue->ExecuteCommandList(m_pCommandList);
	
	if (wait)
	{
		pQueue->WaitForFence(fenceValue);
	}

	m_DynamicAllocator->Free(fenceValue);

	pQueue->FreeAllocator(fenceValue, m_pAllocator);
	m_pAllocator = nullptr;

	m_pGraphics->FreeCommandList(this);

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
	
	m_DynamicAllocator->Free(fenceValue);
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

void CommandContext::InsertUavBarrier(GraphicsBuffer* pBuffer, bool executeImmediate)
{
	m_QueueBarriers[m_NumQueueBarriers] = CD3DX12_RESOURCE_BARRIER::UAV(pBuffer ? pBuffer->GetResource() : nullptr);
	++m_NumQueueBarriers;

	if (executeImmediate || m_NumQueueBarriers >= m_QueueBarriers.size())
	{
		FlushResourceBarriers();
	}

	if (pBuffer)
	{
		pBuffer->m_CurrentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
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

void CommandContext::CopyResource(GraphicsBuffer* pSource, GraphicsBuffer* pDest)
{
	assert(pSource);
	assert(pDest);
	D3D12_RESOURCE_STATES sourceState = pSource->GetResourceState();
	D3D12_RESOURCE_STATES destState = pDest->GetResourceState();
	InsertResourceBarrier(pSource, D3D12_RESOURCE_STATE_COPY_SOURCE);
	InsertResourceBarrier(pDest, D3D12_RESOURCE_STATE_COPY_DEST);
	m_pCommandList->CopyResource(pDest->GetResource(), pSource->GetResource());
	InsertResourceBarrier(pSource, sourceState);
	InsertResourceBarrier(pDest, destState);
}

void CommandContext::InitializeBuffer(GraphicsBuffer* pResource, const void* pData, uint64_t dataSize, uint32_t offset)
{
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	D3D12_RESOURCE_STATES previousState = pResource->GetResourceState();
	InsertResourceBarrier(pResource, D3D12_RESOURCE_STATE_COPY_DEST, true);
	m_pCommandList->CopyBufferRegion(pResource->GetResource(), offset, allocation.pBackingResource->GetResource(), allocation.Offset, dataSize);
	InsertResourceBarrier(pResource, previousState, true);
}

void CommandContext::InitializeTexture(GraphicsTexture* pResource, D3D12_SUBRESOURCE_DATA* pSubresources, int firstSubresource, int subresourceCount)
{
	D3D12_RESOURCE_DESC desc = pResource->GetResource()->GetDesc();
	size_t requiredSize = 0;
	m_pGraphics->GetDevice()->GetCopyableFootprints(&desc, firstSubresource, subresourceCount, 0, nullptr, nullptr, nullptr, &requiredSize);
	/* D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT(512 bytes): can for the start address of each subresource's data
	*  D3D12_TEXTURE_DATA_PITCH_ALIGNMENT(256 bytes): can used to ensure each row's pitch is properly padded
	*  D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT(64 KB): can used to with CreatePlacedResource, for GPU page size
	*/
	DynamicAllocation allocation = m_DynamicAllocator->Allocate((uint32_t)requiredSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
	D3D12_RESOURCE_STATES previousState = pResource->GetResourceState();
	InsertResourceBarrier(pResource, D3D12_RESOURCE_STATE_COPY_DEST, true);
	UpdateSubresources(m_pCommandList, pResource->GetResource(), allocation.pBackingResource->GetResource(), allocation.Offset, firstSubresource, subresourceCount, pSubresources);
	InsertResourceBarrier(pResource, previousState, true);
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

void GraphicsCommandContext::BeginRenderPass(const RenderPassInfo& renderPassInfo)
{
	assert(!m_InRenderPass);

#if USE_RENDERPASSES
	ComPtr<ID3D12GraphicsCommandList4> pCmd;
	if (m_pGraphics->UseRenderPasses() && m_pCommandList->QueryInterface(IID_PPV_ARGS(pCmd.GetAddressOf())) == S_OK)
	{
		D3D12_RENDER_PASS_DEPTH_STENCIL_DESC renderPassDepthStencilDesc{};
		renderPassDepthStencilDesc.DepthBeginningAccess.Type = RenderPassInfo::ExtractBeginAccess(renderPassInfo.DepthStencilTarget.Access);
		if (renderPassDepthStencilDesc.DepthBeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
		{
			const ClearBinding& clearBinding = renderPassInfo.DepthStencilTarget.Target->GetClearBinding();
			assert(clearBinding.BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
			renderPassDepthStencilDesc.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth = clearBinding.DepthStencil.Depth;
			renderPassDepthStencilDesc.DepthBeginningAccess.Clear.ClearValue.Format = renderPassInfo.DepthStencilTarget.Target->GetFormat();
		}
		renderPassDepthStencilDesc.DepthEndingAccess.Type = RenderPassInfo::ExtractEndingAccess(renderPassInfo.DepthStencilTarget.Access);

		bool writeable = true;
		if (renderPassDepthStencilDesc.DepthEndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD)
		{
			writeable = false;
		}

		renderPassDepthStencilDesc.StencilBeginningAccess.Type = RenderPassInfo::ExtractBeginAccess(renderPassInfo.DepthStencilTarget.StencilAccess);
		if (renderPassDepthStencilDesc.StencilBeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
		{
			const ClearBinding& clearBinding = renderPassInfo.DepthStencilTarget.Target->GetClearBinding();
			assert(clearBinding.BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
			renderPassDepthStencilDesc.StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil = clearBinding.DepthStencil.Stencil;
			renderPassDepthStencilDesc.StencilBeginningAccess.Clear.ClearValue.Format = renderPassInfo.DepthStencilTarget.Target->GetFormat();
		}
		renderPassDepthStencilDesc.StencilEndingAccess.Type = RenderPassInfo::ExtractEndingAccess(renderPassInfo.DepthStencilTarget.StencilAccess);
		renderPassDepthStencilDesc.cpuDescriptor = renderPassInfo.DepthStencilTarget.Target->GetDSV(writeable);

		std::array<D3D12_RENDER_PASS_RENDER_TARGET_DESC, 4> renderTargetDescs{};
		for (uint32_t i = 0; i < renderPassInfo.RenderTargetCount; i++)
		{
			const RenderPassInfo::RenderTargetInfo& data = renderPassInfo.RenderTargets[i];

			renderTargetDescs[i].BeginningAccess.Type = RenderPassInfo::ExtractBeginAccess(data.Access);
			if (renderTargetDescs[i].BeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
			{
				assert(data.Target->GetClearBinding().BindingValue == ClearBinding::ClearBindingValue::Color);
				memcpy(renderTargetDescs[i].BeginningAccess.Clear.ClearValue.Color, &data.Target->GetClearBinding().Color, sizeof(Color));
				renderTargetDescs[i].BeginningAccess.Clear.ClearValue.Format = data.Target->GetFormat();
			}
			renderTargetDescs[i].EndingAccess.Type = RenderPassInfo::ExtractEndingAccess(data.Access);

			uint32_t subResource = D3D12CalcSubresource(data.MipLevel, data.ArrayIndex, 0, data.Target->GetMipLevels(), data.Target->GetArraySize());

			std::array<D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS, 4> subResourceParams{};
			if (renderTargetDescs[i].EndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE)
			{
				assert(data.ResolveTarget);
				InsertResourceBarrier(data.ResolveTarget, D3D12_RESOURCE_STATE_RESOLVE_DEST);
				renderTargetDescs[i].EndingAccess.Resolve.Format = data.Target->GetFormat();
				renderTargetDescs[i].EndingAccess.Resolve.pDstResource = data.ResolveTarget->GetResource();
				renderTargetDescs[i].EndingAccess.Resolve.pSrcResource = data.Target->GetResource();
				renderTargetDescs[i].EndingAccess.Resolve.PreserveResolveSource = false;
				renderTargetDescs[i].EndingAccess.Resolve.ResolveMode = D3D12_RESOLVE_MODE_AVERAGE; // Default resolve mode, can be changed if needed
				renderTargetDescs[i].EndingAccess.Resolve.SubresourceCount = 1;
				
				subResourceParams[i].DstSubresource = 0;
				subResourceParams[i].SrcSubresource = subResource;
				subResourceParams[i].DstX = 0;
				subResourceParams[i].DstY = 0;
				subResourceParams[i].SrcRect = CD3DX12_RECT(0, 0, data.Target->GetWidth(), data.Target->GetHeight());
				renderTargetDescs[i].EndingAccess.Resolve.pSubresourceParameters = subResourceParams.data();
			}

			renderTargetDescs[i].cpuDescriptor = data.Target->GetRTV(subResource);
		}

		D3D12_RENDER_PASS_FLAGS renderPassFlags = D3D12_RENDER_PASS_FLAG_NONE;
		if (renderPassInfo.WriteUAVs)
		{
			renderPassFlags |= D3D12_RENDER_PASS_FLAG_ALLOW_UAV_WRITES;
		}

		FlushResourceBarriers();
		pCmd->BeginRenderPass(renderPassInfo.RenderTargetCount, renderTargetDescs.data(), &renderPassDepthStencilDesc, renderPassFlags);
	}
	else
#endif
	{
		FlushResourceBarriers();

		bool writeable = true;
		if (RenderPassInfo::ExtractEndingAccess(renderPassInfo.DepthStencilTarget.Access) == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD)
		{
			writeable = false;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = renderPassInfo.DepthStencilTarget.Target->GetDSV(writeable);
		D3D12_CLEAR_FLAGS clearFlags = (D3D12_CLEAR_FLAGS)0;
		if (RenderPassInfo::ExtractBeginAccess(renderPassInfo.DepthStencilTarget.Access) == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
		{
			clearFlags |= D3D12_CLEAR_FLAG_DEPTH;
		}
		if (RenderPassInfo::ExtractBeginAccess(renderPassInfo.DepthStencilTarget.StencilAccess) == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
		{
			clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
		}
		if (clearFlags != (D3D12_CLEAR_FLAGS)0)
		{
			const ClearBinding& clearBinding = renderPassInfo.DepthStencilTarget.Target->GetClearBinding();
			assert(clearBinding.BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
			m_pCommandList->ClearDepthStencilView(dsvHandle, clearFlags, clearBinding.DepthStencil.Depth, clearBinding.DepthStencil.Stencil, 0, nullptr);
		}
		
		std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 4> rtvHandles{};
		for (uint32_t i = 0; i < renderPassInfo.RenderTargetCount; i++)
		{
			const RenderPassInfo::RenderTargetInfo& data = renderPassInfo.RenderTargets[i];
			uint32_t subResource = D3D12CalcSubresource(data.MipLevel, data.ArrayIndex, 0, data.Target->GetMipLevels(), data.Target->GetArraySize());
			if (RenderPassInfo::ExtractBeginAccess(data.Access) == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
			{
				assert(data.Target->GetClearBinding().BindingValue == ClearBinding::ClearBindingValue::Color);
				m_pCommandList->ClearRenderTargetView(data.Target->GetRTV(subResource), data.Target->GetClearBinding().Color, 0, nullptr);
			}
			rtvHandles[i] = data.Target->GetRTV(subResource);
		}
		m_pCommandList->OMSetRenderTargets(renderPassInfo.RenderTargetCount, rtvHandles.data(), false, &dsvHandle);	
	}

	m_InRenderPass = true;
	m_CurrentRenderPassInfo = renderPassInfo;
}

void GraphicsCommandContext::EndRenderPass()
{
	assert(m_InRenderPass);
#if USE_RENDERPASSES
	ComPtr<ID3D12GraphicsCommandList4> pCmd;
	if (m_pGraphics->UseRenderPasses() && m_pCommandList->QueryInterface(IID_PPV_ARGS(pCmd.GetAddressOf())) == S_OK)
	{
		pCmd->EndRenderPass();
	}
	else
#endif
	{
		for (uint32_t i = 0; i < m_CurrentRenderPassInfo.RenderTargetCount; i++)
		{
			const RenderPassInfo::RenderTargetInfo& data = m_CurrentRenderPassInfo.RenderTargets[i];
			if (RenderPassInfo::ExtractEndingAccess(data.Access) == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE)
			{
				InsertResourceBarrier(data.Target, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, false);
				InsertResourceBarrier(data.ResolveTarget, D3D12_RESOURCE_STATE_RESOLVE_DEST, true);
				uint32_t subResource = D3D12CalcSubresource(data.MipLevel, data.ArrayIndex, 0, data.Target->GetMipLevels(), data.Target->GetArraySize());
				m_pCommandList->ResolveSubresource(data.ResolveTarget->GetResource(), 0, data.Target->GetResource(), subResource, data.Target->GetFormat());
			}
		}
	}
	m_InRenderPass = false;
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
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	m_pCommandList->SetGraphicsRootConstantBufferView(rootIndex, allocation.GpuHandle);
}

void GraphicsCommandContext::SetDynamicVertexBuffer(int rootIndex, int elementCount, int elementSize, void* pData)
{
	assert(m_CurrentContext == CommandListContext::Graphics);
	int bufferSize = elementCount * elementSize;
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(bufferSize);
	memcpy(allocation.pMappedMemory, pData, bufferSize);

	D3D12_VERTEX_BUFFER_VIEW view = {};
	view.BufferLocation = allocation.GpuHandle;
	view.SizeInBytes = bufferSize;
	view.StrideInBytes = elementSize;
	m_pCommandList->IASetVertexBuffers(rootIndex, 1, &view);
}

void GraphicsCommandContext::SetDynamicIndexBuffer(int elementCount, void* pData, bool smallIndices)
{
	assert(m_CurrentContext == CommandListContext::Graphics);
	
	int stride = smallIndices ? sizeof(uint16_t) : sizeof(uint32_t);
	int bufferSize = elementCount * stride;

	DynamicAllocation allocation = m_DynamicAllocator->Allocate(bufferSize);
	memcpy(allocation.pMappedMemory, pData, bufferSize);

	D3D12_INDEX_BUFFER_VIEW view = {};
	view.BufferLocation = allocation.GpuHandle;
	view.SizeInBytes = bufferSize;
	view.Format = smallIndices ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
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

// GPU-driven rendering
void ComputeCommandContext::ExecuteIndirect(ID3D12CommandSignature* pCommandSignature, GraphicsBuffer* pIndirectArguments)
{
	FlushResourceBarriers();
	m_pShaderResourceDescriptorAllocator->UploadAndBindStagedDescriptors(m_CurrentContext == CommandListContext::Compute ? DescriptorTableType::Compute : DescriptorTableType::Graphics);
	m_pSamplerDescriptorAllocator->UploadAndBindStagedDescriptors(m_CurrentContext == CommandListContext::Compute ? DescriptorTableType::Compute : DescriptorTableType::Graphics);
	// pCommandSignature: command type (dispatch or draw...)
	// pArgumentBuffer: the parameters for command
	m_pCommandList->ExecuteIndirect(pCommandSignature, 1, pIndirectArguments->GetResource(), 0, nullptr, 0);
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

void ComputeCommandContext::ClearUavUInt(GraphicsBuffer* pBuffer, uint32_t values[4])
{
	DescriptorHandle gpuHandle = m_pShaderResourceDescriptorAllocator->AllocateTransientDescriptor(1);
	m_pGraphics->GetDevice()->CopyDescriptorsSimple(1, gpuHandle.GetCpuHandle(), pBuffer->GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_pCommandList->ClearUnorderedAccessViewUint(gpuHandle.GetGpuHandle(), pBuffer->GetUAV(), pBuffer->GetResource(), values, 0, nullptr);
}

void ComputeCommandContext::ClearUavUFloat(GraphicsBuffer* pBuffer, float values[4])
{
	DescriptorHandle gpuHandle = m_pShaderResourceDescriptorAllocator->AllocateTransientDescriptor(1);
	m_pGraphics->GetDevice()->CopyDescriptorsSimple(1, gpuHandle.GetCpuHandle(), pBuffer->GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_pCommandList->ClearUnorderedAccessViewFloat(gpuHandle.GetGpuHandle(), pBuffer->GetUAV(), pBuffer->GetResource(), values, 0, nullptr);
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
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(dataSize);
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

D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE RenderPassInfo::ExtractBeginAccess(RenderPassAccess access)
{
	RenderTargetLoadAction loadAction = (RenderTargetLoadAction)((uint8_t)access >> 2);
	switch (loadAction)
	{
	case RenderTargetLoadAction::DontCare: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
	case RenderTargetLoadAction::Load: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
	case RenderTargetLoadAction::Clear: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
	}
	return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
}

D3D12_RENDER_PASS_ENDING_ACCESS_TYPE RenderPassInfo::ExtractEndingAccess(RenderPassAccess access)
{
	RenderTargetStoreAction storeAction = (RenderTargetStoreAction)((uint8_t)access & 0b11);
	switch (storeAction)
	{
	case RenderTargetStoreAction::DontCare: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD;
	case RenderTargetStoreAction::Store: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
	case RenderTargetStoreAction::Resolve: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE;
	}
	return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
}