#pragma once
#include "GraphicsResource.h"
#include "OnlineDescriptorAllocator.h"
#include "DynamicResourceAllocator.h"

class Graphics;
class GraphicsResource;
class GraphicsTexture;
class Buffer;
class OnlineDescriptorAllocator;
class RootSignature;
class PipelineState;
class DynamicResourceAllocator;
class GraphicsCommandContext;
class ComputeCommandContext;
class CopyCommandContext;
class UnorderedAccessView;
class ShaderResourceView;
class CommandSignature;
class ShaderBindingTable;
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
	};

	RenderPassInfo() {}

	RenderPassInfo(GraphicsTexture* pDepthBuffer, RenderPassAccess access, bool uavWrites = false)
		: RenderTargetCount(0)
	{
		DepthStencilTarget.Access = access;
		DepthStencilTarget.Target = pDepthBuffer;
		DepthStencilTarget.StencilAccess = RenderPassAccess::NoAccess;
		WriteUAVs = uavWrites;
	}

	RenderPassInfo(GraphicsTexture* pRenderTarget, RenderPassAccess renderTargetAccess, GraphicsTexture* pDepthBuffer, RenderPassAccess depthAccess, bool uavWritrs = false, RenderPassAccess stencilAccess = RenderPassAccess::NoAccess)
		: RenderTargetCount(1)
	{
		RenderTargets[0].Access = renderTargetAccess;
		RenderTargets[0].Target = pRenderTarget;
		DepthStencilTarget.Access = depthAccess;
		DepthStencilTarget.Target = pDepthBuffer;
		DepthStencilTarget.StencilAccess = stencilAccess;
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

class CommandContext : public GraphicsObject
{
public:
	CommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, D3D12_COMMAND_LIST_TYPE type);
	virtual ~CommandContext();

	virtual void Reset();
	virtual uint64_t Execute(bool wait);
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

	ID3D12GraphicsCommandList* GetCommandList() const { return m_pCommandList; }
	ID3D12GraphicsCommandList4* GetRaytracingCommandList() const { return m_pRaytracingCommandList.Get(); }
	ID3D12GraphicsCommandList6* GetMeshShadingCommandList() const {	return m_pMeshShadingCommandList.Get(); }
	
	D3D12_COMMAND_LIST_TYPE GetType() const { return m_Type; }

	// commands
	void Dispatch(const IntVector3& groupCounts);
	void Dispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1);
	void DispatchMesh(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1);
	void ExecuteIndirect(CommandSignature* pCommandSignature, Buffer* pIndirectArguments, DescriptorTableType type = DescriptorTableType::Compute);
	void Draw(int vertexStart, int vertexCount);
	void DrawIndexed(int indexCount, int indexStart, int minVertex = 0);
	void DrawIndexedInstanced(int indexCount, int indexStart, int instanceCount, int minVertex = 0, int instanceStart = 0);
	
	void DispatchRays(ShaderBindingTable& table, uint32_t width = 1, uint32_t height = 1, uint32_t depth = 1);

	void ClearColor(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const Color& color = Color(0.f, 0.f, 0.f, 1.0f));
	void ClearDepth(D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, float depth = 1.0f, unsigned char stencil = 0);
	void ClearUavUInt(GraphicsResource* pBuffer, UnorderedAccessView* pUav, uint32_t* values = nullptr);
	void ClearUavFloat(GraphicsResource* pBuffer, UnorderedAccessView* pUav, float* values = nullptr);
	
	void ResolveResource(GraphicsTexture* pSource, uint32_t sourceSubResource, GraphicsTexture* pTarget, uint32_t targetSubResource, DXGI_FORMAT format);
	
	void PrepareDraw(DescriptorTableType type);

	// a more structured way for applications to decalare data dependencies and output targets for a set of rendering operations. 
	void BeginRenderPass(const RenderPassInfo& renderPassInfo);
	void EndRenderPass();

	// bindings
	void SetComputeRootSignature(RootSignature* pRootSignature);
	void SetPipelineState(PipelineState* pPipelineState);
	void SetPipelineState(ID3D12StateObject* pStateObject);
	void SetComputeRootConstants(int rootIndex, uint32_t count, const void* pConstants);
	void SetComputeDynamicConstantBufferView(int rootIndex, void* pData, uint32_t dataSize);

	void SetDynamicDescriptor(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE handle);
	void SetDynamicDescriptor(int rootIndex, int offset, ShaderResourceView* pSrv);
	void SetDynamicDescriptor(int rootIndex, int offset, UnorderedAccessView* pUav);
	void SetDynamicDescriptors(int rootIndex, int offset, const D3D12_CPU_DESCRIPTOR_HANDLE* handle, int count);
	void SetDynamicSamplerDescriptor(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE handle);
	void SetDynamicSamplerDescriptors(int rootIndex, int offset, const D3D12_CPU_DESCRIPTOR_HANDLE* handle, int count);

	void SetGraphicsRootSignature(RootSignature* pRootSignature);

	void SetGraphicsRootConstants(int rootIndex, uint32_t count, const void* pConstants);
	void SetDynamicConstantBufferView(int rootIndex, const void* pData, uint32_t dataSize);
	void SetDynamicVertexBuffer(int rootIndex, int elementCount, int elementSize, const void* pData);
	void SetDynamicIndexBuffer(int elementCount, const void* pData, bool smallIndices = false);
	void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY type);
	void SetVertexBuffer(BufferView pVertexBuffer);
	void SetVertexBuffers(BufferView* pVertexBuffers, int bufferCount);
	void SetIndexBuffer(BufferView pIndexBuffer);
	void SetViewport(const FloatRect& rect, float minDepth = 0.0f, float maxDepth = 1.0f);
	void SetScissorRect(const FloatRect& rect);

	void SetDescriptorHeap(ID3D12DescriptorHeap* pHeap, D3D12_DESCRIPTOR_HEAP_TYPE type);

	DynamicAllocation AllocateTransientMemory(uint64_t size);
	DescriptorHandle AllocateTransientDescriptor(uint32_t count, D3D12_DESCRIPTOR_HEAP_TYPE type);

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
	void BindDescriptorHeaps();

	std::unique_ptr<OnlineDescriptorAllocator> m_pShaderResourceDescriptorAllocator;
	std::unique_ptr<OnlineDescriptorAllocator> m_pSamplerDescriptorAllocator;

	std::array<ID3D12DescriptorHeap*, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_CurrentDescriptorHeaps{};

	ResourceBarrierBatcher m_BarrierBatcher;

	std::unique_ptr<DynamicResourceAllocator> m_DynamicAllocator;
	ID3D12GraphicsCommandList* m_pCommandList{};
	ComPtr<ID3D12GraphicsCommandList4> m_pRaytracingCommandList;
	ComPtr<ID3D12GraphicsCommandList6> m_pMeshShadingCommandList;
	ID3D12CommandAllocator* m_pAllocator{};
	D3D12_COMMAND_LIST_TYPE m_Type{};

	RenderPassInfo m_CurrentRenderPassInfo;
	bool m_InRenderPass{ false };
	std::array<D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> m_ResolveSubresourceParameters{};

	std::unordered_map<GraphicsResource*, ResourceState> m_ResourceStates;
	std::vector<PendingBarrier> m_PendingBarriers;
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


class CommandSignature
{
public:
    void Finalize(const char* pName, ID3D12Device* pDevice);

    void SetRootSignature(ID3D12RootSignature* pRootSignature) { m_pRootSignature = pRootSignature; };
    void AddDispatch();
    void AddDraw();
    void AddDrawIndexed();

    ID3D12CommandSignature* GetCommandSignature() const { return m_pCommandSignature.Get(); }

private:
    // describes the layout of data used in indirect draw or dispatch commamds(e.g. ExecuteIndirect)
    ComPtr<ID3D12CommandSignature> m_pCommandSignature;
    ID3D12RootSignature* m_pRootSignature{nullptr};
    uint32_t m_Stride{0};
    std::vector<D3D12_INDIRECT_ARGUMENT_DESC> m_ArgumentDescs;
};