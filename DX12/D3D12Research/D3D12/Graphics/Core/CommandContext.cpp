#include "stdafx.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "CommandQueue.h"
#include "DynamicResourceAllocator.h"
#include "OnlineDescriptorAllocator.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "GraphicsTexture.h"
#include "GraphicsBuffer.h"
#include "GraphicsResource.h"
#include "ResourceViews.h"
#include "ShaderBindingTable.h"		
#include "StateObject.h"

CommandContext::CommandContext(GraphicsDevice* pParent, ID3D12GraphicsCommandList* pCommandList,
								D3D12_COMMAND_LIST_TYPE type, GlobalOnlineDescriptorHeap* pDescriptorHeap,
								DynamicAllocationManager* pDynamicMemoryManager, ID3D12CommandAllocator* pAllocator)
	: GraphicsObject(pParent), m_pCommandList(pCommandList), m_Type(type), m_pAllocator(pAllocator),
		m_ShaderResourceDescriptorAllocator(pDescriptorHeap)
{
	m_DynamicAllocator = std::make_unique<DynamicResourceAllocator>(pDynamicMemoryManager);

	m_pCommandList->QueryInterface(IID_PPV_ARGS(m_pRaytracingCommandList.GetAddressOf()));
	m_pCommandList->QueryInterface(IID_PPV_ARGS(m_pMeshShadingCommandList.GetAddressOf()));
}

void CommandContext::Reset()
{
	check(m_pCommandList);
	if (m_pAllocator == nullptr)
	{
		m_pAllocator = GetGraphics()->GetCommandQueue(m_Type)->RequestAllocator();
		m_pCommandList->Reset(m_pAllocator, nullptr);
	}

	m_BarrierBatcher.Reset();
	m_PendingBarriers.clear();
	m_ResourceStates.clear();

	// GPU needs to know which heap(s) to read from when you bind descroptor tables.
	ID3D12DescriptorHeap* pHeaps[] = {
		GetGraphics()->GetGlobalViewHeap()->GetHeap(),
	};
	m_pCommandList->SetDescriptorHeaps((UINT)std::size(pHeaps), pHeaps);
}

uint64_t CommandContext::Execute(bool wait)
{
	CommandContext* pContexts[] = {this};
	return Execute(pContexts, std::size(pContexts), wait);
}

uint64_t CommandContext::Execute(CommandContext** pContexts, uint32_t numContexts, bool wait)
{
	check(numContexts > 0);
	CommandQueue* pQueue = pContexts[0]->GetGraphics()->GetCommandQueue(pContexts[0]->GetType());
	for (uint32_t i = 0; i < numContexts; ++i)
	{
		checkf(pContexts[i]->GetType() == pQueue->GetType(), "All commandlist types must come from the same queue");
		pContexts[i]->FlushResourceBarriers();
	}
	uint64_t fenceValue = pQueue->ExecuteCommandList(pContexts, numContexts);
	if (wait)
	{
		pQueue->WaitForFence(fenceValue);
	}

	for (uint32_t i = 0; i < numContexts; ++i)
	{
		pContexts[i]->Free(fenceValue);
	}

	return fenceValue;
}

void CommandContext::Free(uint64_t fenceValue)
{
	m_DynamicAllocator->Free(fenceValue);
	GetGraphics()->GetCommandQueue(m_Type)->FreeAllocator(fenceValue, m_pAllocator);
	m_pAllocator = nullptr;
	GetGraphics()->FreeCommandList(this);

	if (m_Type != D3D12_COMMAND_LIST_TYPE_COPY)
	{
		m_ShaderResourceDescriptorAllocator.ReleaseUsedHeaps(fenceValue);
	}
}

bool NeedsTransition(D3D12_RESOURCE_STATES& before, D3D12_RESOURCE_STATES& after)
{
	// can read from 'write' DSV
	if (before == D3D12_RESOURCE_STATE_DEPTH_WRITE && after == D3D12_RESOURCE_STATE_DEPTH_READ)
	{
		return false;
	}

	if (after == D3D12_RESOURCE_STATE_COMMON)
	{
		return before != D3D12_RESOURCE_STATE_COMMON;
	}

	// combine already transitioned bits
	D3D12_RESOURCE_STATES combined = before | after;
	if ((combined & (D3D12_RESOURCE_STATE_GENERIC_READ | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)) == combined)
	{
		after = combined;
	}

	return before != after;
}

void CommandContext::InsertResourceBarrier(GraphicsResource* pBuffer, D3D12_RESOURCE_STATES state, uint32_t subResource)
{
	check(pBuffer && pBuffer->GetResource());
	checkf(IsTransitionAllowed(m_Type, state), "afterState (%s) is not valid on this commandlist type (%s)", D3D::ResourceStateToString(state).c_str(), D3D::CommandlistTypeToString(m_Type));

	ResourceState& resourceState = m_ResourceStates[pBuffer];
	D3D12_RESOURCE_STATES beforeState = resourceState.Get(subResource);
	if (beforeState == D3D12_RESOURCE_STATE_UNKNOWN)
	{
		PendingBarrier barrier;
		barrier.pResource = pBuffer;
		barrier.State = state;
		barrier.SubResource = subResource;
		m_PendingBarriers.push_back(barrier);
		resourceState.Set(state, subResource);
	}
	else
	{
		if (NeedsTransition(beforeState, state))
		{
			checkf(IsTransitionAllowed(m_Type, beforeState), "current resource state (%s) is not valid to transition from this commandlist type (%s)", D3D::ResourceStateToString(beforeState).c_str(), D3D::CommandlistTypeToString(m_Type));
			m_BarrierBatcher.AddTransition(pBuffer->GetResource(), beforeState, state, subResource);
			resourceState.Set(state, subResource);	
		}
	}
}

void CommandContext::InsertUavBarrier(GraphicsResource* pBuffer)
{
	m_BarrierBatcher.AddUAV(pBuffer ? pBuffer->GetResource() : nullptr);	
}

void CommandContext::FlushResourceBarriers()
{
	m_BarrierBatcher.Flush(m_pCommandList);
}

void CommandContext::CopyTexture(GraphicsResource* pSource, GraphicsResource* pDest)
{
	checkf(pSource && pSource->GetResource(), "Source is invalid");
	checkf(pDest && pDest->GetResource(), "Target is invalid");

	InsertResourceBarrier(pSource, D3D12_RESOURCE_STATE_COPY_SOURCE);
	InsertResourceBarrier(pDest, D3D12_RESOURCE_STATE_COPY_DEST);
	FlushResourceBarriers();
	m_pCommandList->CopyResource(pDest->GetResource(), pSource->GetResource());
}

void CommandContext::CopyTexture(GraphicsTexture* pSource, Buffer* pDestination, const D3D12_BOX& sourceRegion, int sourceSubregion, int destinationOffset)
{
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint = {};
	D3D12_RESOURCE_DESC desc = pSource->GetResource()->GetDesc();
	GetGraphics()->GetDevice()->GetCopyableFootprints(&desc, 0, 1, 0, &textureFootprint, nullptr, nullptr, nullptr);

	CD3DX12_TEXTURE_COPY_LOCATION srcLocation(pSource->GetResource(), sourceSubregion);
	CD3DX12_TEXTURE_COPY_LOCATION dstLocation(pDestination->GetResource(), textureFootprint);
	m_pCommandList->CopyTextureRegion(&dstLocation, destinationOffset, 0, 0, &srcLocation, &sourceRegion);
}

void CommandContext::CopyTexture(GraphicsTexture* pSource, GraphicsTexture* pDestination, const D3D12_BOX& sourceRegion, const D3D12_BOX& destinationRegion, int sourceSubregion, int destinationSubregion)
{
	CD3DX12_TEXTURE_COPY_LOCATION srcLocation(pSource->GetResource(), sourceSubregion);
	CD3DX12_TEXTURE_COPY_LOCATION dstLocation(pDestination->GetResource(), destinationSubregion);
	m_pCommandList->CopyTextureRegion(&dstLocation, destinationRegion.left, destinationRegion.top, destinationRegion.front, &srcLocation, &sourceRegion);
}

void CommandContext::CopyBuffer(Buffer* pSource, Buffer* pDestination, uint32_t size, uint32_t sourceOffset, uint32_t destinationOffset)
{
	m_pCommandList->CopyBufferRegion(pDestination->GetResource(), destinationOffset, pSource->GetResource(), sourceOffset, size);
}

void CommandContext::InitializeBuffer(GraphicsResource* pResource, const void* pData, uint64_t dataSize, uint32_t offset)
{
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);

	bool resetState = false;
	D3D12_RESOURCE_STATES previousState = GetResourceStateWithFallback(pResource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	if (previousState != D3D12_RESOURCE_STATE_COPY_DEST)
	{
		resetState = true;
		InsertResourceBarrier(pResource, D3D12_RESOURCE_STATE_COPY_DEST);
		FlushResourceBarriers();
	}
	m_pCommandList->CopyBufferRegion(pResource->GetResource(), offset, allocation.pBackingResource->GetResource(), allocation.Offset, dataSize);
	if (resetState)
	{
		InsertResourceBarrier(pResource, previousState);
	}
}

void CommandContext::InitializeTexture(GraphicsTexture* pResource, D3D12_SUBRESOURCE_DATA* pSubresources, int firstSubresource, int subresourceCount)
{
	D3D12_RESOURCE_DESC desc = pResource->GetResource()->GetDesc();
	uint64_t requiredSize = 0;
	GetGraphics()->GetDevice()->GetCopyableFootprints(&desc, firstSubresource, subresourceCount, 0, nullptr, nullptr, nullptr, &requiredSize);
	/* D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT(512 bytes): can for the start address of each subresource's data
	*  D3D12_TEXTURE_DATA_PITCH_ALIGNMENT(256 bytes): can used to ensure each row's pitch is properly padded
	*  D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT(64 KB): can used to with CreatePlacedResource, for GPU page size
	*/
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(requiredSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
	
	bool resetState = false;
	D3D12_RESOURCE_STATES previousState = GetResourceStateWithFallback(pResource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	if (previousState != D3D12_RESOURCE_STATE_COPY_DEST)
	{
		resetState = true;
		InsertResourceBarrier(pResource, D3D12_RESOURCE_STATE_COPY_DEST);
		FlushResourceBarriers();
	}
	UpdateSubresources(m_pCommandList, pResource->GetResource(), allocation.pBackingResource->GetResource(), allocation.Offset, firstSubresource, subresourceCount, pSubresources);
	if (resetState)
	{
		InsertResourceBarrier(pResource, previousState);
	}
}

void CommandContext::Dispatch(const IntVector3& groupCounts)
{
	Dispatch(groupCounts.x, groupCounts.y, groupCounts.z);
}

void CommandContext::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
	PrepareDraw(CommandListContext::Compute);
	m_pCommandList->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void CommandContext::DispatchMesh(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
	check(m_pMeshShadingCommandList);
	PrepareDraw(CommandListContext::Graphics);
	m_pMeshShadingCommandList->DispatchMesh(groupCountX, groupCountY, groupCountZ);
}

void CommandContext::DispatchRays(ShaderBindingTable& table, uint32_t width, uint32_t height, uint32_t depth)
{
	check(m_pRaytracingCommandList);

	D3D12_DISPATCH_RAYS_DESC rayDesc = {};
	table.Commit(*this, rayDesc);
	rayDesc.Width = width;
	rayDesc.Height = height;
	rayDesc.Depth = depth;
	
	PrepareDraw(CommandListContext::Compute);
	m_pRaytracingCommandList->DispatchRays(&rayDesc);
}

// GPU-driven rendering
void CommandContext::ExecuteIndirect(CommandSignature* pCommandSignature, uint32_t maxCount, Buffer* pIndirectArguments, Buffer* pCountBuffer, uint32_t argumentsOffset, uint32_t countOffset)
{
	PrepareDraw(pCommandSignature->IsCompute() ? CommandListContext::Compute : CommandListContext::Graphics);
	m_pCommandList->ExecuteIndirect(pCommandSignature->GetCommandSignature(), maxCount, pIndirectArguments->GetResource(), argumentsOffset, pCountBuffer ? pCountBuffer->GetResource() : nullptr, countOffset);
}

void CommandContext::Draw(int vertexStart, int vertexCount)
{
	PrepareDraw(CommandListContext::Graphics);
	m_pCommandList->DrawInstanced(vertexCount, 1, vertexStart, 0);
}

void CommandContext::DrawIndexed(int indexCount, int indexStart, int minVertex)
{
	PrepareDraw(CommandListContext::Graphics);
	m_pCommandList->DrawIndexedInstanced(indexCount, 1, indexStart, minVertex, 0);
}

void CommandContext::DrawIndexedInstanced(int indexCount, int indexStart, int instanceCount, int minVertex, int instanceStart)
{
	PrepareDraw(CommandListContext::Graphics);
	m_pCommandList->DrawIndexedInstanced(indexCount, instanceCount, indexStart, minVertex, instanceStart);
}

void CommandContext::ClearColor(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const DirectX::SimpleMath::Color& color /*= Color(0.2f, 0.2f, 0.2f, 1.0f)*/)
{
	m_pCommandList->ClearRenderTargetView(rtv, color, 0, nullptr);
}

void CommandContext::ClearDepth(D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS clearFlags /*= D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL*/, float depth /*= 1.0f*/, unsigned char stencil /*= 0*/)
{
	m_pCommandList->ClearDepthStencilView(dsv, clearFlags, depth, stencil, 0, nullptr);
}

void CommandContext::BeginRenderPass(const RenderPassInfo& renderPassInfo)
{
	checkf(!m_InRenderPass, "already in RenderPass");
	checkf(renderPassInfo.DepthStencilTarget.Target || (renderPassInfo.DepthStencilTarget.Access == RenderPassAccess::NoAccess && renderPassInfo.DepthStencilTarget.StencilAccess == RenderPassAccess::NoAccess),
			"either a depth texture must be assigned or the access should be 'NoAccess'");

	auto ExtractBeginAccess = [](RenderPassAccess access)
	{
		switch (RenderPassInfo::GetBeginAccess(access))
		{
		case RenderTargetLoadAction::DontCare: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
		case RenderTargetLoadAction::Load: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
		case RenderTargetLoadAction::Clear: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
		case RenderTargetLoadAction::NoAccess: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
		}
		return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
	};

	auto ExtractEndingAccess = [](RenderPassAccess access)
	{
		switch (RenderPassInfo::GetEndingAccess(access))
		{
		case RenderTargetStoreAction::DontCare: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD;
		case RenderTargetStoreAction::Store: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
		case RenderTargetStoreAction::Resolve: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE;
		case RenderTargetStoreAction::NoAccess: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
		}
		return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
	};

#if D3D12_USE_RENDERPASSES
	if (GetGraphics()->GetCapabilities().SupportsRaytracing() && m_pRaytracingCommandList)
	{
		D3D12_RENDER_PASS_DEPTH_STENCIL_DESC renderPassDepthStencilDesc{};
		renderPassDepthStencilDesc.DepthBeginningAccess.Type = ExtractBeginAccess(renderPassInfo.DepthStencilTarget.Access);
		if (renderPassDepthStencilDesc.DepthBeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
		{
			const ClearBinding& clearBinding = renderPassInfo.DepthStencilTarget.Target->GetClearBinding();
			check(clearBinding.BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
			renderPassDepthStencilDesc.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth = clearBinding.DepthStencil.Depth;
			renderPassDepthStencilDesc.DepthBeginningAccess.Clear.ClearValue.Format = renderPassInfo.DepthStencilTarget.Target->GetFormat();
		}
		renderPassDepthStencilDesc.DepthEndingAccess.Type = ExtractEndingAccess(renderPassInfo.DepthStencilTarget.Access);

		if (renderPassDepthStencilDesc.DepthEndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD)
		{
			check(renderPassInfo.DepthStencilTarget.Write == false);
		}

		renderPassDepthStencilDesc.StencilBeginningAccess.Type = ExtractBeginAccess(renderPassInfo.DepthStencilTarget.StencilAccess);
		if (renderPassDepthStencilDesc.StencilBeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
		{
			const ClearBinding& clearBinding = renderPassInfo.DepthStencilTarget.Target->GetClearBinding();
			check(clearBinding.BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
			renderPassDepthStencilDesc.StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil = clearBinding.DepthStencil.Stencil;
			renderPassDepthStencilDesc.StencilBeginningAccess.Clear.ClearValue.Format = renderPassInfo.DepthStencilTarget.Target->GetFormat();
		}
		renderPassDepthStencilDesc.StencilEndingAccess.Type = ExtractEndingAccess(renderPassInfo.DepthStencilTarget.StencilAccess);
		if (renderPassInfo.DepthStencilTarget.Target)
		{
			renderPassDepthStencilDesc.cpuDescriptor = renderPassInfo.DepthStencilTarget.Target->GetDSV(renderPassInfo.DepthStencilTarget.Write);
		}

		std::array<D3D12_RENDER_PASS_RENDER_TARGET_DESC, 4> renderTargetDescs{};
		m_ResolveSubresourceParameters = {};
		for (uint32_t i = 0; i < renderPassInfo.RenderTargetCount; i++)
		{
			const RenderPassInfo::RenderTargetInfo& data = renderPassInfo.RenderTargets[i];

			renderTargetDescs[i].BeginningAccess.Type = ExtractBeginAccess(data.Access);
			if (renderTargetDescs[i].BeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
			{
				check(data.Target->GetClearBinding().BindingValue == ClearBinding::ClearBindingValue::Color);
				const Color& color = data.Target->GetClearBinding().Color;
				D3D12_CLEAR_VALUE& clearValue = renderTargetDescs[i].BeginningAccess.Clear.ClearValue;
				clearValue.Color[0] = color.x;
				clearValue.Color[1] = color.y;
				clearValue.Color[2] = color.z;
				clearValue.Color[3] = color.w;
				clearValue.Format = data.Target->GetFormat();
			}

			D3D12_RENDER_PASS_ENDING_ACCESS_TYPE endingAccess = ExtractEndingAccess(data.Access);
			if (data.Target->GetDesc().SampleCount <= 1 && endingAccess == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE)
			{
				validateOncef(data.Target == data.ResolveTarget, "render target %d is set to resolve but has a sample count of 1. this will just do a CopyTexture instead which is wasteful.", i);
				endingAccess = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
			}
			renderTargetDescs[i].EndingAccess.Type = endingAccess;

			uint32_t subResource = D3D12CalcSubresource(data.MipLevel, data.ArrayIndex, 0, data.Target->GetMipLevels(), data.Target->GetArraySize());
			if (renderTargetDescs[i].EndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE)
			{
				checkf(data.ResolveTarget, "expected ResolveTarget because ending access is 'Resolve'");
				InsertResourceBarrier(data.ResolveTarget, D3D12_RESOURCE_STATE_RESOLVE_DEST);
				renderTargetDescs[i].EndingAccess.Resolve.Format = data.Target->GetFormat();
				renderTargetDescs[i].EndingAccess.Resolve.pDstResource = data.ResolveTarget->GetResource();
				renderTargetDescs[i].EndingAccess.Resolve.pSrcResource = data.Target->GetResource();
				renderTargetDescs[i].EndingAccess.Resolve.PreserveResolveSource = false;
				renderTargetDescs[i].EndingAccess.Resolve.ResolveMode = D3D12_RESOLVE_MODE_AVERAGE; // Default resolve mode, can be changed if needed
				renderTargetDescs[i].EndingAccess.Resolve.SubresourceCount = 1;
				
				m_ResolveSubresourceParameters[i].DstSubresource = 0;
				m_ResolveSubresourceParameters[i].SrcSubresource = subResource;
				m_ResolveSubresourceParameters[i].DstX = 0;
				m_ResolveSubresourceParameters[i].DstY = 0;
				m_ResolveSubresourceParameters[i].SrcRect = CD3DX12_RECT(0, 0, data.Target->GetWidth(), data.Target->GetHeight());
				renderTargetDescs[i].EndingAccess.Resolve.pSubresourceParameters = m_ResolveSubresourceParameters.data();
			}

			renderTargetDescs[i].cpuDescriptor = data.Target->GetRTV();
		}

		D3D12_RENDER_PASS_FLAGS renderPassFlags = D3D12_RENDER_PASS_FLAG_NONE;
		if (renderPassInfo.WriteUAVs)
		{
			renderPassFlags |= D3D12_RENDER_PASS_FLAG_ALLOW_UAV_WRITES;
		}

		FlushResourceBarriers();
		m_pRaytracingCommandList->BeginRenderPass(renderPassInfo.RenderTargetCount, renderTargetDescs.data(), renderPassInfo.DepthStencilTarget.Target ? &renderPassDepthStencilDesc : nullptr, renderPassFlags);
	}
	else
#endif
	{
		FlushResourceBarriers();

		if (ExtractEndingAccess(renderPassInfo.DepthStencilTarget.Access) == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD)
		{
			check(renderPassInfo.DepthStencilTarget.Write == false);
		}

		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = renderPassInfo.DepthStencilTarget.Target ? renderPassInfo.DepthStencilTarget.Target->GetDSV(renderPassInfo.DepthStencilTarget.Write) : D3D12_CPU_DESCRIPTOR_HANDLE{};
		D3D12_CLEAR_FLAGS clearFlags = (D3D12_CLEAR_FLAGS)0;
		if (ExtractBeginAccess(renderPassInfo.DepthStencilTarget.Access) == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
		{
			clearFlags |= D3D12_CLEAR_FLAG_DEPTH;
		}
		if (ExtractBeginAccess(renderPassInfo.DepthStencilTarget.StencilAccess) == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
		{
			clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
		}
		if (clearFlags != (D3D12_CLEAR_FLAGS)0)
		{
			const ClearBinding& clearBinding = renderPassInfo.DepthStencilTarget.Target->GetClearBinding();
			check(clearBinding.BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
			m_pCommandList->ClearDepthStencilView(dsvHandle, clearFlags, clearBinding.DepthStencil.Depth, clearBinding.DepthStencil.Stencil, 0, nullptr);
		}
		
		std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 4> rtvHandles{};
		for (uint32_t i = 0; i < renderPassInfo.RenderTargetCount; i++)
		{
			const RenderPassInfo::RenderTargetInfo& data = renderPassInfo.RenderTargets[i];
			if (ExtractBeginAccess(data.Access) == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
			{
				check(data.Target->GetClearBinding().BindingValue == ClearBinding::ClearBindingValue::Color);
				m_pCommandList->ClearRenderTargetView(data.Target->GetRTV(), data.Target->GetClearBinding().Color, 0, nullptr);
			}
			rtvHandles[i] = data.Target->GetRTV();
		}
		m_pCommandList->OMSetRenderTargets(renderPassInfo.RenderTargetCount, rtvHandles.data(), false, dsvHandle.ptr != 0 ? &dsvHandle : nullptr);	
	}

	m_InRenderPass = true;
	m_CurrentRenderPassInfo = renderPassInfo;

	GraphicsTexture* pTargetTexture = renderPassInfo.DepthStencilTarget.Target ? renderPassInfo.DepthStencilTarget.Target : renderPassInfo.RenderTargets[0].Target;
	SetViewport(FloatRect(0, 0, (float)pTargetTexture->GetWidth(), (float)pTargetTexture->GetHeight()), 0, 1);
}

void CommandContext::EndRenderPass()
{
	check(m_InRenderPass);

	auto ExtractEndingAccess = [](RenderPassAccess access) -> D3D12_RENDER_PASS_ENDING_ACCESS_TYPE
	{
		switch (RenderPassInfo::GetEndingAccess(access))
		{
		case RenderTargetStoreAction::DontCare: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD;
		case RenderTargetStoreAction::Store: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
		case RenderTargetStoreAction::Resolve: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE;
		case RenderTargetStoreAction::NoAccess: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
		}
		noEntry();
		return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
	};

#if D3D12_USE_RENDERPASSES
	if (GetGraphics()->GetCapabilities().SupportsRaytracing() && m_pRaytracingCommandList)
	{
		m_pRaytracingCommandList->EndRenderPass();

		for (uint32_t i = 0; i < m_CurrentRenderPassInfo.RenderTargetCount; ++i)
		{
			const RenderPassInfo::RenderTargetInfo& data = m_CurrentRenderPassInfo.RenderTargets[i];
			if (ExtractEndingAccess(data.Access) == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE && data.Target->GetDesc().SampleCount <= 1 && data.Target != data.ResolveTarget)
			{
				FlushResourceBarriers();
				CopyTexture(data.Target, data.ResolveTarget);
			}
		}
	}
	else
#endif
	{
		for (uint32_t i = 0; i < m_CurrentRenderPassInfo.RenderTargetCount; i++)
		{
			const RenderPassInfo::RenderTargetInfo& data = m_CurrentRenderPassInfo.RenderTargets[i];
			if (ExtractEndingAccess(data.Access) == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE)
			{
				if (data.Target->GetDesc().SampleCount > 1)
				{
					InsertResourceBarrier(data.Target, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
					InsertResourceBarrier(data.ResolveTarget, D3D12_RESOURCE_STATE_RESOLVE_DEST);
					uint32_t subResource = D3D12CalcSubresource(data.MipLevel, data.ArrayIndex, 0, data.Target->GetMipLevels(), data.Target->GetArraySize());
					ResolveResource(data.Target, subResource, data.ResolveTarget, 0, data.Target->GetFormat());
				}
				else if (data.Target != data.ResolveTarget)
				{
					validateOncef(false, "render target %d is set to resolve but has a sample count of 1. this will just do a CopyTexture instead which is wasteful.", i);
					FlushResourceBarriers();
					CopyTexture(data.Target, data.ResolveTarget);
				}
			}
		}
	}
	m_InRenderPass = false;
}

void CommandContext::SetGraphicsRootSRV(int rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress)
{
	m_pCommandList->SetGraphicsRootShaderResourceView(rootIndex, gpuAddress);
}

void CommandContext::SetGraphicsRootUAV(int rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	m_pCommandList->SetGraphicsRootUnorderedAccessView(rootIndex, address);
}

void CommandContext::SetGraphicsRootSignature(RootSignature* pRootSignature)
{
	m_pCommandList->SetGraphicsRootSignature(pRootSignature->GetRootSignature());
	m_ShaderResourceDescriptorAllocator.ParseRootSignature(pRootSignature);
}

void CommandContext::SetGraphicsDynamicConstantBufferView(int rootIndex, const void* pData, uint32_t dataSize)
{
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	m_pCommandList->SetGraphicsRootConstantBufferView(rootIndex, allocation.GpuHandle);
}

void CommandContext::SetDynamicVertexBuffer(int rootIndex, int elementCount, int elementSize, const void* pData)
{
	int bufferSize = elementCount * elementSize;
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(bufferSize);
	memcpy(allocation.pMappedMemory, pData, bufferSize);

	D3D12_VERTEX_BUFFER_VIEW view = {};
	view.BufferLocation = allocation.GpuHandle;
	view.SizeInBytes = bufferSize;
	view.StrideInBytes = elementSize;
	m_pCommandList->IASetVertexBuffers(rootIndex, 1, &view);
}

void CommandContext::SetDynamicIndexBuffer(int elementCount, const void* pData, bool smallIndices)
{
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

void CommandContext::SetGraphicsRootConstants(int rootIndex, uint32_t count, const void* pConstants)
{
	m_pCommandList->SetGraphicsRoot32BitConstants(rootIndex, count, pConstants, 0);
}

void CommandContext::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY type)
{
	m_pCommandList->IASetPrimitiveTopology(type);
}

void CommandContext::SetVertexBuffer(const VertexBufferView& buffer)
{
	SetVertexBuffers(&buffer, 1);
}

void CommandContext::SetVertexBuffers(const VertexBufferView* pBuffer, int bufferCount)
{
	constexpr int MAX_VERTEX_BUFFERS = 4;
	checkf(bufferCount <= MAX_VERTEX_BUFFERS, "vertexBuffers count (%d) exceeds the maximum (%d)", bufferCount, MAX_VERTEX_BUFFERS);
	std::array<D3D12_VERTEX_BUFFER_VIEW, MAX_VERTEX_BUFFERS> views{};
	for (int i = 0; i < bufferCount; ++i)
	{
		views[i].BufferLocation = pBuffer[i].Location;
		views[i].SizeInBytes = (uint32_t)pBuffer[i].Elements * pBuffer[i].Stride;
		views[i].StrideInBytes = pBuffer[i].Stride;
	}
	m_pCommandList->IASetVertexBuffers(0, bufferCount, views.data());
}

void CommandContext::SetIndexBuffer(const IndexBufferView& indexBuffer)
{
	D3D12_INDEX_BUFFER_VIEW view;
	view.BufferLocation = indexBuffer.Location;
	view.SizeInBytes = (uint32_t)indexBuffer.Elements * (indexBuffer.SmallIndices ? 2 : 4);
	view.Format = indexBuffer.SmallIndices ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
	m_pCommandList->IASetIndexBuffer(&view);
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
	SetScissorRect(rect);
}

void CommandContext::SetScissorRect(const FloatRect& rect)
{
	D3D12_RECT r;
	r.left = (LONG)rect.Left;
	r.top = (LONG)rect.Top;
	r.right = (LONG)rect.Right;
	r.bottom = (LONG)rect.Bottom;

	m_pCommandList->RSSetScissorRects(1, &r);
}

void CommandContext::ClearUavUInt(GraphicsResource* pBuffer, UnorderedAccessView* pUav, uint32_t* values)
{
	FlushResourceBarriers();

	DescriptorHandle descriptorHandle = m_ShaderResourceDescriptorAllocator.Allocate(1);
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = pUav->GetDescriptor();
	GetGraphics()->GetDevice()->CopyDescriptorsSimple(1, descriptorHandle.CpuHandle, cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	uint32_t zeros[4] = {0, 0, 0, 0};
	m_pCommandList->ClearUnorderedAccessViewUint(descriptorHandle.GpuHandle, cpuHandle, pBuffer->GetResource(), values ? values : zeros, 0, nullptr);
}

void CommandContext::ClearUavFloat(GraphicsResource* pBuffer, UnorderedAccessView* pUav, float* values)
{
	FlushResourceBarriers();

	DescriptorHandle descriptorHandle = m_ShaderResourceDescriptorAllocator.Allocate(1);
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = pUav->GetDescriptor();
	GetGraphics()->GetDevice()->CopyDescriptorsSimple(1, descriptorHandle.CpuHandle, cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	float zeros[4] = {0, 0, 0, 0};
	m_pCommandList->ClearUnorderedAccessViewFloat(descriptorHandle.GpuHandle, cpuHandle, pBuffer->GetResource(), values ? values : zeros, 0, nullptr);
}

void CommandContext::ResolveResource(GraphicsTexture* pSource, uint32_t sourceSubResource, GraphicsTexture* pTarget, uint32_t targetSubResource, DXGI_FORMAT format)
{
	FlushResourceBarriers();
	m_pCommandList->ResolveSubresource(pTarget->GetResource(), targetSubResource, pSource->GetResource(), sourceSubResource, format);
}

void CommandContext::PrepareDraw(CommandListContext type)
{
	FlushResourceBarriers();
	m_ShaderResourceDescriptorAllocator.BindStagedDescriptors(m_pCommandList, type);
}

void CommandContext::SetPipelineState(PipelineState* pPipelineState)
{
	pPipelineState->ConditionallyReload();
	m_pCommandList->SetPipelineState(pPipelineState->GetPipelineState());
}

void CommandContext::SetPipelineState(StateObject* pStateObject)
{
	check(m_pRaytracingCommandList);
	m_pRaytracingCommandList->SetPipelineState1(pStateObject->GetStateObject());
}

void CommandContext::SetComputeRootSignature(RootSignature* pRootSignature)
{
	m_pCommandList->SetComputeRootSignature(pRootSignature->GetRootSignature());
	m_ShaderResourceDescriptorAllocator.ParseRootSignature(pRootSignature);
}

void CommandContext::SetComputeRootSRV(int rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	m_pCommandList->SetComputeRootShaderResourceView(rootIndex, address);
}

void CommandContext::SetComputeRootUAV(int rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	m_pCommandList->SetComputeRootUnorderedAccessView(rootIndex, address);
}

void CommandContext::SetComputeRootConstants(int rootIndex, uint32_t count, const void* pConstants)
{
	m_pCommandList->SetComputeRoot32BitConstants(rootIndex, count, pConstants, 0);
}

void CommandContext::SetComputeDynamicConstantBufferView(int rootIndex, const void* pData, uint32_t dataSize)
{
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	m_pCommandList->SetComputeRootConstantBufferView(rootIndex, allocation.GpuHandle);
}

void CommandContext::BindResource(int rootIndex, int offset, ResourceView* pView)
{
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = pView->GetDescriptor();
	m_ShaderResourceDescriptorAllocator.SetDescriptors(rootIndex, offset, 1, &cpuHandle);
}

void CommandContext::BindResources(int rootIndex, int offset, const D3D12_CPU_DESCRIPTOR_HANDLE* handles, int count)
{
	m_ShaderResourceDescriptorAllocator.SetDescriptors(rootIndex, offset, count, handles);
}

void CommandContext::BindResourceTable(int rootIndex, D3D12_GPU_DESCRIPTOR_HANDLE handle, CommandListContext context)
{
	if (context == CommandListContext::Graphics)
	{
		m_pCommandList->SetGraphicsRootDescriptorTable(rootIndex, handle);
	}
	else
	{
		m_pCommandList->SetComputeRootDescriptorTable(rootIndex, handle);
	}
}

void CommandContext::SetShadingRate(D3D12_SHADING_RATE shadingRate)
{
	check(m_pMeshShadingCommandList);
	m_pMeshShadingCommandList->RSSetShadingRate(shadingRate, nullptr);
}

void CommandContext::SetShadingRateImage(GraphicsTexture* pTexture)
{
	check(m_pMeshShadingCommandList);
	m_pMeshShadingCommandList->RSSetShadingRateImage(pTexture->GetResource());
}

DynamicAllocation CommandContext::AllocateTransientMemory(uint64_t size, uint32_t alignment)
{
	return m_DynamicAllocator->Allocate(size, alignment);
}

void ResourceBarrierBatcher::AddTransition(ID3D12Resource* pResource, D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState, int subResource)
{
	if (beforeState == afterState)
	{
		return;
	}

	if (m_QueueBarriers.size())
	{
		const D3D12_RESOURCE_BARRIER last = m_QueueBarriers.back();
		if (last.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION &&
			last.Transition.pResource == pResource &&
			last.Transition.StateBefore == beforeState &&
			last.Transition.StateAfter == afterState)
		{
			m_QueueBarriers.pop_back();
			return;
		}
	}

	m_QueueBarriers.emplace_back(
		CD3DX12_RESOURCE_BARRIER::Transition(pResource, beforeState, afterState, subResource, D3D12_RESOURCE_BARRIER_FLAG_NONE));
}

void ResourceBarrierBatcher::AddUAV(ID3D12Resource* pResource)
{
	m_QueueBarriers.emplace_back(CD3DX12_RESOURCE_BARRIER::UAV(pResource));
}

void ResourceBarrierBatcher::Flush(ID3D12GraphicsCommandList* pCmdList)
{
	if (m_QueueBarriers.size())
	{
		pCmdList->ResourceBarrier((uint32_t)m_QueueBarriers.size(), m_QueueBarriers.data());
		Reset();
	}
}

void ResourceBarrierBatcher::Reset()
{
	m_QueueBarriers.clear();
}

bool CommandContext::IsTransitionAllowed(D3D12_COMMAND_LIST_TYPE commandListType, D3D12_RESOURCE_STATES state)
{
	constexpr int VALID_COMPUTE_QUEUE_RESOURCE_STATES = D3D12_RESOURCE_STATE_COMMON |
													D3D12_RESOURCE_STATE_UNORDERED_ACCESS | 
													D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | 
													D3D12_RESOURCE_STATE_COPY_DEST | 
													D3D12_RESOURCE_STATE_COPY_SOURCE |
													D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
	constexpr int VALID_COPY_QUEUE_RESOURCE_STATES = D3D12_RESOURCE_STATE_COMMON |
												 D3D12_RESOURCE_STATE_COPY_DEST | 
												 D3D12_RESOURCE_STATE_COPY_SOURCE;	

	if (commandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
	{
		return (state & VALID_COMPUTE_QUEUE_RESOURCE_STATES) == state;
	}
	else if (commandListType == D3D12_COMMAND_LIST_TYPE_COPY)
	{
		return (state & VALID_COPY_QUEUE_RESOURCE_STATES) == state;
	}
	return true;
}

void CommandSignature::Finalize(const char* pName)
{
    D3D12_COMMAND_SIGNATURE_DESC desc = {};
    desc.ByteStride = m_Stride;
    desc.NumArgumentDescs = static_cast<uint32_t>(m_ArgumentDescs.size());
    desc.pArgumentDescs = m_ArgumentDescs.data();
    desc.NodeMask = 0;

    VERIFY_HR_EX(GetGraphics()->GetDevice()->CreateCommandSignature(&desc, m_pRootSignature, IID_PPV_ARGS(m_pCommandSignature.GetAddressOf())), GetGraphics()->GetDevice());
    D3D_SETNAME(m_pCommandSignature.Get(), pName);
}

void CommandSignature::AddDispatch()
{
    D3D12_INDIRECT_ARGUMENT_DESC desc = {};
    desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    m_ArgumentDescs.push_back(desc);
    m_Stride += sizeof(D3D12_DISPATCH_ARGUMENTS);
	m_IsCompute = true;
}

void CommandSignature::AddDraw()
{
    D3D12_INDIRECT_ARGUMENT_DESC desc = {};
    desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    m_ArgumentDescs.push_back(desc);
    m_Stride += sizeof(D3D12_DRAW_ARGUMENTS);
	m_IsCompute = false;
}

void CommandSignature::AddDrawIndexed()
{
    D3D12_INDIRECT_ARGUMENT_DESC desc;
    desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    m_ArgumentDescs.push_back(desc);
    m_Stride += sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
	m_IsCompute = false;
}

void CommandSignature::AddConstants(uint32_t numConstants, uint32_t rootIndex, uint32_t offset)
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
    desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
	desc.Constant.RootParameterIndex = rootIndex;
	desc.Constant.Num32BitValuesToSet = numConstants;
	desc.Constant.DestOffsetIn32BitValues = offset;
    m_ArgumentDescs.push_back(desc);
    m_Stride += numConstants * sizeof(uint32_t);
}

// the argument is one GPU virtual address (64-bit)
void CommandSignature::AddConstantBufferView(uint32_t rootIndex)
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
    desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
	desc.ConstantBufferView.RootParameterIndex = rootIndex;
	m_ArgumentDescs.push_back(desc);
	m_Stride += sizeof(uint64_t);
}

void CommandSignature::AddShaderResourceView(uint32_t rootIndex)
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
    desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW;
	desc.ShaderResourceView.RootParameterIndex = rootIndex;
	m_ArgumentDescs.push_back(desc);
	m_Stride += sizeof(uint64_t);
}

void CommandSignature::AddUnorderedAccessView(uint32_t rootIndex)
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW;
	desc.UnorderedAccessView.RootParameterIndex = rootIndex;
	m_ArgumentDescs.push_back(desc);
	m_Stride += sizeof(uint64_t);
}

void CommandSignature::AddVertexBuffer(uint32_t slot)
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
	desc.VertexBuffer.Slot = slot;
	m_ArgumentDescs.push_back(desc);
	m_Stride += sizeof(D3D12_VERTEX_BUFFER_VIEW);
}

void CommandSignature::AddIndexBuffer()
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;
	m_ArgumentDescs.push_back(desc);
	m_Stride += sizeof(D3D12_INDEX_BUFFER_VIEW);
}
