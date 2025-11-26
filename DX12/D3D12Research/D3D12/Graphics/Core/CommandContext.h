#pragma once
#include "GraphicsResource.h"
#include "OnlineDescriptorAllocator.h"
#include "DynamicResourceAllocator.h"
#include "ResourceViews.h"

class GraphicsResource;
class GraphicsTexture;
class Buffer;
class OnlineDescriptorAllocator;
class RootSignature;
class PipelineState;
class StateObject;
class DynamicResourceAllocator;
class GraphicsCommandContext;
class ComputeCommandContext;
class CopyCommandContext;
class UnorderedAccessView;
class ShaderResourceView;
class CommandSignature;
class ShaderBindingTable;
class CommandQueue;
class DynamicAllocationManager;
struct BufferView;

enum class CommandListContext
{
	Graphics,
	Compute
};

enum class RenderTargetLoadAction : uint8_t
{
	DontCare,
	Load,
	Clear,
	NoAccess
};
DEFINE_ENUM_FLAG_OPERATORS(RenderTargetLoadAction)

enum class RenderTargetStoreAction : uint8_t
{
	DontCare,
	Store,
	Resolve,
	NoAccess
};
DEFINE_ENUM_FLAG_OPERATORS(RenderTargetStoreAction)

enum class RenderPassAccess : uint8_t
{
#define COMBINE_ACTIONS(load, store) (uint8_t)RenderTargetLoadAction::load << 4 | (uint8_t)RenderTargetStoreAction::store
	DontCare_DontCare = COMBINE_ACTIONS(DontCare, DontCare),
	DontCare_Store = COMBINE_ACTIONS(DontCare, Store),
	Clear_Store = COMBINE_ACTIONS(Clear, Store),
	Load_Store = COMBINE_ACTIONS(Load, Store),
	Clear_DontCare = COMBINE_ACTIONS(Clear, DontCare),
	Load_DontCare = COMBINE_ACTIONS(Load, DontCare),
	Clear_Resolve = COMBINE_ACTIONS(Clear, Resolve),
	Load_Resolve = COMBINE_ACTIONS(Load, Resolve),
	DontCare_Resolve = COMBINE_ACTIONS(DontCare, Resolve),
	NoAccess = COMBINE_ACTIONS(NoAccess, NoAccess),
#undef COMBINE_ACTIONS
};

struct ClearValues
{
	bool ClearColor = false;
	bool ClearStencil = false;
	bool ClearDepth = false;
};

struct RenderPassInfo
{
	struct RenderTargetInfo
	{
		RenderPassAccess Access{RenderPassAccess::DontCare_DontCare};
		GraphicsTexture* Target{nullptr};
		GraphicsTexture* ResolveTarget{nullptr};
		int MipLevel{0};
		int ArrayIndex{0};
	};

	struct DepthTargetInfo
	{
		RenderPassAccess Access{RenderPassAccess::DontCare_DontCare};
		RenderPassAccess StencilAccess{RenderPassAccess::DontCare_DontCare};
		GraphicsTexture* Target{nullptr};
		bool Write{true};
	};

	RenderPassInfo() {}

	RenderPassInfo(GraphicsTexture* pDepthBuffer, RenderPassAccess access, bool uavWrites = false)
		: RenderTargetCount(0)
	{
		DepthStencilTarget.Access = access;
		DepthStencilTarget.Target = pDepthBuffer;
		DepthStencilTarget.StencilAccess = RenderPassAccess::NoAccess;
		DepthStencilTarget.Write = true;
		WriteUAVs = uavWrites;
	}

	RenderPassInfo(GraphicsTexture* pRenderTarget, RenderPassAccess renderTargetAccess, GraphicsTexture* pDepthBuffer, RenderPassAccess depthAccess, bool depthWrite, bool uavWritrs = false, RenderPassAccess stencilAccess = RenderPassAccess::NoAccess)
		: RenderTargetCount(1)
	{
		RenderTargets[0].Access = renderTargetAccess;
		RenderTargets[0].Target = pRenderTarget;
		DepthStencilTarget.Access = depthAccess;
		DepthStencilTarget.Target = pDepthBuffer;
		DepthStencilTarget.StencilAccess = stencilAccess;
		DepthStencilTarget.Write = depthWrite;
		WriteUAVs = uavWritrs;
	}

	static RenderTargetLoadAction GetBeginAccess(RenderPassAccess access)
	{
		return (RenderTargetLoadAction)((uint8_t)access >> 4);
	}

	static RenderTargetStoreAction GetEndingAccess(RenderPassAccess access)
	{
		return (RenderTargetStoreAction)((uint8_t)access & 0b1111);
	}

	bool WriteUAVs = false;
	uint32_t RenderTargetCount{0};
	std::array<RenderTargetInfo, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> RenderTargets{};
	DepthTargetInfo DepthStencilTarget{};
};

class ResourceBarrierBatcher
{
public:
	void AddTransition(ID3D12Resource* pResource, D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState, int subResource);
	void AddUAV(ID3D12Resource* pResource);
	void Flush(ID3D12GraphicsCommandList* pCmdList);
	void Reset();
	bool HasWork() const { return m_QueueBarriers.size() > 0; }

private:
	std::vector<D3D12_RESOURCE_BARRIER> m_QueueBarriers;
};

namespace ComputeUtils
{
	inline IntVector3 GetNumThreadGroups(uint32_t threadsX = 1, uint32_t groupSizeX = 1, uint32_t threadsY = 1, uint32_t groupSizeY = 1, uint32_t threadsZ = 1, uint32_t groupSizeZ = 1)
	{
		IntVector3 groups;
		groups.x = Math::DivideAndRoundUp(threadsX, groupSizeX);
		groups.y = Math::DivideAndRoundUp(threadsY, groupSizeY);
		groups.z = Math::DivideAndRoundUp(threadsZ, groupSizeZ);
		return groups;
	}
}

class CommandContext : public GraphicsObject
{
public:
	CommandContext(GraphicsDevice* pGraphics, ID3D12GraphicsCommandList* pCommandList,
					D3D12_COMMAND_LIST_TYPE type, GlobalOnlineDescriptorHeap* pDescriptorHeap,
					DynamicAllocationManager* pDynamicMemoryManager, ID3D12CommandAllocator* pAllocator);
	~CommandContext() = default;

	void Reset();
	uint64_t Execute(bool wait);
	static uint64_t Execute(CommandContext** pContexts, uint32_t numContexts, bool wait);
	void Free(uint64_t fenceValue);
	
	void InsertResourceBarrier(GraphicsResource* pBuffer, D3D12_RESOURCE_STATES state, uint32_t subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	void InsertUavBarrier(GraphicsResource* pBuffer = nullptr);
	void FlushResourceBarriers();

	void CopyTexture(GraphicsResource* pSource, GraphicsResource* pDest);
	void CopyTexture(GraphicsTexture* pSource, Buffer* pDestination, const D3D12_BOX& sourceRegion, int sourceSubregion = 0, int destinationOffset = 0);
	void CopyTexture(GraphicsTexture* pSource, GraphicsTexture* pDestination, const D3D12_BOX& sourceRegion, const D3D12_BOX& destinationRegion, int sourceSubregion = 0, int destinationSubregion = 0);
	void CopyBuffer(Buffer* pSource, Buffer* pDestination, uint32_t size, uint32_t sourceOffset = 0, uint32_t destinationOffset = 0);
	void InitializeBuffer(GraphicsResource* pResource, const void* pData, uint64_t dataSize, uint32_t offset = 0);
	void InitializeTexture(GraphicsTexture* pResource, D3D12_SUBRESOURCE_DATA* pSubresources, int firstSubresource, int subresourceCount);

	void Dispatch(const IntVector3& groupCounts);
	void Dispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1);
	void DispatchMesh(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1);

	void ExecuteIndirect(CommandSignature* pCommandSignature, uint32_t maxCount, Buffer* pIndirectArguments, Buffer* pCountBuffer, uint32_t argumentsOffset = 0, uint32_t countOffset = 0);
	void Draw(int vertexStart, int vertexCount);
	void DrawIndexed(int indexCount, int indexStart, int minVertex = 0);
	void DrawIndexedInstanced(int indexCount, int indexStart, int instanceCount, int minVertex = 0, int instanceStart = 0);
	
	void DispatchRays(ShaderBindingTable& table, uint32_t width = 1, uint32_t height = 1, uint32_t depth = 1);

	void ClearColor(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const Color& color = Color(0.f, 0.f, 0.f, 1.0f));
	void ClearDepth(D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, float depth = 1.0f, unsigned char stencil = 0);
	void ResolveResource(GraphicsTexture* pSource, uint32_t sourceSubResource, GraphicsTexture* pTarget, uint32_t targetSubResource, DXGI_FORMAT format);
	
	void PrepareDraw(CommandListContext type);

	void ClearUavUInt(GraphicsResource* pBuffer, UnorderedAccessView* pUav, uint32_t* values = nullptr);
	void ClearUavFloat(GraphicsResource* pBuffer, UnorderedAccessView* pUav, float* values = nullptr);

	// a more structured way for applications to decalare data dependencies and output targets for a set of rendering operations. 
	void BeginRenderPass(const RenderPassInfo& renderPassInfo);
	void EndRenderPass();

	void SetPipelineState(PipelineState* pPipelineState);
	void SetPipelineState(StateObject* pStateObject);

	void BindResource(int rootIndex, int offset, ResourceView* pView);
	void BindResources(int rootIndex, int offset, const D3D12_CPU_DESCRIPTOR_HANDLE* handle, int count = 1);
	void BindResourceTable(int rootIndex, D3D12_GPU_DESCRIPTOR_HANDLE handle, CommandListContext context);
	
	void SetDynamicVertexBuffer(int rootIndex, int elementCount, int elementSize, const void* pData);
	void SetDynamicIndexBuffer(int elementCount, const void* pData, bool smallIndices = false);
	void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY type);
	void SetVertexBuffer(const VertexBufferView& buffer);
	void SetVertexBuffers(const VertexBufferView* pBuffers, int bufferCount);
	void SetIndexBuffer(const IndexBufferView& indexBuffer);
	void SetViewport(const FloatRect& rect, float minDepth = 0.0f, float maxDepth = 1.0f);
	void SetScissorRect(const FloatRect& rect);

	void SetShadingRate(D3D12_SHADING_RATE shadingRate = D3D12_SHADING_RATE_1X1);
	void SetShadingRateImage(GraphicsTexture* pTexture);

	// compute
	void SetComputeRootSignature(RootSignature* pRootSignature);
	void SetComputeRootSRV(int rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address);
	void SetComputeRootUAV(int rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address);
	void SetComputeRootConstants(int rootIndex, uint32_t count, const void* pConstants);
	template<typename T>
	void SetComputeRootConstants(int rootIndex, const T& data)
	{
		SetComputeRootConstants(rootIndex, sizeof(T) / sizeof(int32_t), &data);
	}
	void SetComputeDynamicConstantBufferView(int rootIndex, const void* pData, uint32_t dataSize);
	template<typename T>
	void SetComputeDynamicConstantBufferView(int rootIndex, const T& data)
	{
		static_assert(!std::is_pointer<T>::value, "provided type is a pointer. this is probably unintentional.");
		SetComputeDynamicConstantBufferView(rootIndex, &data, sizeof(T));
	}

	// graphics
	void SetGraphicsRootSignature(RootSignature* pRootSignature);
	void SetGraphicsRootSRV(int rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address);
	void SetGraphicsRootUAV(int rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address);
	void SetGraphicsRootConstants(int rootIndex, uint32_t count, const void* pConstants);
	template<typename T>
	void SetGraphicsRootConstants(int rootIndex, const T& data)
	{
		SetGraphicsRootConstants(rootIndex, sizeof(T) / sizeof(int32_t), &data);
	}
	void SetGraphicsDynamicConstantBufferView(int rootIndex, const void* pData, uint32_t dataSize);
	template<typename T>
	void SetGraphicsDynamicConstantBufferView(int rootIndex, const T& data)
	{
		static_assert(!std::is_pointer<T>::value, "provided type is a pointer. this is probably unintentional.");
		SetGraphicsDynamicConstantBufferView(rootIndex, &data, sizeof(T));
	}

	DynamicAllocation AllocateTransientMemory(uint64_t size, uint32_t alignment = 256);

	ID3D12GraphicsCommandList* GetCommandList() const { return m_pCommandList; }
	ID3D12GraphicsCommandList4* GetRaytracingCommandList() const { return m_pRaytracingCommandList.Get(); }
	ID3D12GraphicsCommandList6* GetMeshShadingCommandList() const {	return m_pMeshShadingCommandList.Get(); }
	
	D3D12_COMMAND_LIST_TYPE GetType() const { return m_Type; }

	struct PendingBarrier
	{
		GraphicsResource* pResource;
		ResourceState State;
		uint32_t SubResource;
	};
	const std::vector<PendingBarrier>& GetPendingBarriers() const { return m_PendingBarriers; }

	D3D12_RESOURCE_STATES GetResourceState(GraphicsResource* pResource, uint32_t subResource) const
	{
		auto iter = m_ResourceStates.find(pResource);
		check(iter != m_ResourceStates.end());
		return iter->second.Get(subResource);
	}

	D3D12_RESOURCE_STATES GetResourceStateWithFallback(GraphicsResource* pResource, uint32_t subResource) const
	{
		 auto it = m_ResourceStates.find(pResource);
		 if (it == m_ResourceStates.end())
		 {
			return pResource->GetResourceState(subResource);
		 }
		 return it->second.Get(subResource);
	}

	static bool IsTransitionAllowed(D3D12_COMMAND_LIST_TYPE commandListType, D3D12_RESOURCE_STATES state);

private:
	OnlineDescriptorAllocator m_ShaderResourceDescriptorAllocator;

	ResourceBarrierBatcher m_BarrierBatcher;

	ID3D12GraphicsCommandList* m_pCommandList;
	ComPtr<ID3D12GraphicsCommandList4> m_pRaytracingCommandList;
	ComPtr<ID3D12GraphicsCommandList6> m_pMeshShadingCommandList;

	std::unique_ptr<DynamicResourceAllocator> m_DynamicAllocator;
	ID3D12CommandAllocator* m_pAllocator{};
	D3D12_COMMAND_LIST_TYPE m_Type;
	std::unordered_map<GraphicsResource*, ResourceState> m_ResourceStates;
	std::vector<PendingBarrier> m_PendingBarriers;

	RenderPassInfo m_CurrentRenderPassInfo;
	bool m_InRenderPass{ false };
	std::array<D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> m_ResolveSubresourceParameters{};
};

class ScopedBarrier
{
public:
	ScopedBarrier(CommandContext& context, GraphicsResource* pResource, D3D12_RESOURCE_STATES newState, uint32_t subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		: m_Context(context), m_pResource(pResource), m_SubResource(subResource)
	{
		m_BeforeState = context.GetResourceStateWithFallback(pResource, subResource);

		context.InsertResourceBarrier(pResource, newState, subResource);
	}

	~ScopedBarrier()
	{
		m_Context.InsertResourceBarrier(m_pResource, m_BeforeState, m_SubResource);
	}

private:
	CommandContext& m_Context;
	GraphicsResource* m_pResource;
	uint32_t m_SubResource;
	D3D12_RESOURCE_STATES m_BeforeState{D3D12_RESOURCE_STATE_UNKNOWN};
};


class CommandSignature : public GraphicsObject
{
public:
	CommandSignature(GraphicsDevice* pParent) : GraphicsObject(pParent) {}
    void Finalize(const char* pName);

    void SetRootSignature(ID3D12RootSignature* pRootSignature) { m_pRootSignature = pRootSignature; };
    void AddDispatch();
    void AddDraw();
    void AddDrawIndexed();
	void AddConstants(uint32_t numConstants, uint32_t rootIndex, uint32_t offset);
	void AddConstantBufferView(uint32_t rootIndex);
	void AddShaderResourceView(uint32_t rootIndex);
	void AddUnorderedAccessView(uint32_t rootIndex);
	void AddVertexBuffer(uint32_t slot);
	void AddIndexBuffer();

    ID3D12CommandSignature* GetCommandSignature() const { return m_pCommandSignature.Get(); }
	bool IsCompute() const { return m_IsCompute; }

private:
    // describes the layout of data used in indirect draw or dispatch commamds(e.g. ExecuteIndirect)
    ComPtr<ID3D12CommandSignature> m_pCommandSignature;
    ID3D12RootSignature* m_pRootSignature{nullptr};
    uint32_t m_Stride{0};
	bool m_IsCompute{true};
    std::vector<D3D12_INDIRECT_ARGUMENT_DESC> m_ArgumentDescs;
};
