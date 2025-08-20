#pragma once
#include "GraphicsResource.h"
#include "OnlineDescriptorAllocator.h"
#include "DynamicResourceAllocator.h"

class Graphics;
class GraphicsResource;
class GraphicsTexture;
class Buffer;
class BufferUAV;
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
	std::array<RenderTargetInfo, 4> RenderTargets{};
	DepthTargetInfo DepthStencilTarget{};
};

class ResourceBarrierBatcher
{
public:
	void AddTransition(ID3D12Resource* pResource, D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState, int subResource);
	void AddUAV(ID3D12Resource* pResource);
	void Flush(ID3D12GraphicsCommandList* pCmdList);
	void Reset();

private:
	std::vector<D3D12_RESOURCE_BARRIER> m_QueueBarriers;
};

class CommandContext : public GraphicsObject
{
public:
	CommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, ID3D12CommandAllocator* pAllocator, D3D12_COMMAND_LIST_TYPE type);
	virtual ~CommandContext();

	virtual void Reset();
	virtual uint64_t Execute(bool wait);
	
	void InsertResourceBarrier(GraphicsResource* pBuffer, D3D12_RESOURCE_STATES state, bool executeImmediate = false, uint32_t subResource = 0xffffffff);
	void InsertUavBarrier(GraphicsResource* pBuffer = nullptr, bool executeImmediate = false);
	void FlushResourceBarriers();

	void CopyResource(GraphicsResource* pSource, GraphicsResource* pDest);
	void InitializeBuffer(GraphicsResource* pResource, const void* pData, uint64_t dataSize, uint32_t offset = 0);
	void InitializeTexture(GraphicsTexture* pResource, D3D12_SUBRESOURCE_DATA* pSubresources, int firstSubresource, int subresourceCount);

	ID3D12GraphicsCommandList* GetCommandList() const { return m_pCommandList; }
	ID3D12GraphicsCommandList4* GetRaytracingCommandList() const { return m_pRaytracingCommandList.Get(); }
	D3D12_COMMAND_LIST_TYPE GetType() const { return m_Type; }

	// commands
	void Dispatch(const IntVector3& groupCounts);
	void Dispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1);
	void ExecuteIndirect(CommandSignature* pCommandSignature, Buffer* pIndirectArguments, DescriptorTableType type = DescriptorTableType::Compute);
	void Draw(int vertexStart, int vertexCount);
	void DrawIndexed(int indexCount, int indexStart, int minVertex = 0);
	void DrawIndexedInstanced(int indexCount, int indexStart, int instanceCount, int minVertex = 0, int instanceStart = 0);
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
	void SetComputeRootConstants(int rootIndex, uint32_t count, const void* pConstants);
	void SetComputeDynamicConstantBufferView(int rootIndex, void* pData, uint32_t dataSize);

	void SetDynamicDescriptor(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE handle);
	void SetDynamicDescriptor(int rootIndex, int offset, ShaderResourceView* pSrv);
	void SetDynamicDescriptor(int rootIndex, int offset, UnorderedAccessView* pUav);
	void SetDynamicDescriptors(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE* handle, int count);
	void SetDynamicSamplerDescriptor(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE handle);
	void SetDynamicSamplerDescriptors(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE* handle, int count);

	void SetGraphicsRootSignature(RootSignature* pRootSignature);

	void SetGraphicsRootConstants(int rootIndex, uint32_t count, const void* pConstants);
	void SetDynamicConstantBufferView(int rootIndex, const void* pData, uint32_t dataSize);
	void SetDynamicVertexBuffer(int rootIndex, int elementCount, int elementSize, const void* pData);
	void SetDynamicIndexBuffer(int elementCount, const void* pData, bool smallIndices = false);
	void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY type);
	void SetVertexBuffer(Buffer* pVertexBuffer);
	void SetVertexBuffers(Buffer** pVertexBuffers, int bufferCount);
	void SetIndexBuffer(Buffer* pIndexBuffer);
	void SetViewport(const FloatRect& rect, float minDepth = 0.0f, float maxDepth = 1.0f);
	void SetScissorRect(const FloatRect& rect);

	void SetDescriptorHeap(ID3D12DescriptorHeap* pHeap, D3D12_DESCRIPTOR_HEAP_TYPE type);

	DynamicAllocation AllocateTransientMemory(uint64_t size);
	DescriptorHandle AllocateTransientDescriptor(uint32_t count, D3D12_DESCRIPTOR_HEAP_TYPE type);

private:
	void BindDescriptorHeaps();

	std::unique_ptr<OnlineDescriptorAllocator> m_pShaderResourceDescriptorAllocator;
	std::unique_ptr<OnlineDescriptorAllocator> m_pSamplerDescriptorAllocator;

	std::array<ID3D12DescriptorHeap*, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_CurrentDescriptorHeaps{};

	ResourceBarrierBatcher m_BarrierBatcher;

	std::unique_ptr<DynamicResourceAllocator> m_DynamicAllocator;
	ID3D12GraphicsCommandList* m_pCommandList{};
	ComPtr<ID3D12GraphicsCommandList4> m_pRaytracingCommandList;
	ID3D12CommandAllocator* m_pAllocator{};
	D3D12_COMMAND_LIST_TYPE m_Type{};

	RenderPassInfo m_CurrentRenderPassInfo;
	bool m_InRenderPass{ false };
};