#pragma once
#include "DynamicResourceAllocator.h"

class Graphics;
class GraphicsResource;
class GraphicsBuffer;
class GraphicsTexture;
class DynamicDescriptorAllocator;
class RootSignature;
class GraphicsPipelineState;
class ComputePipelineState;

class GraphicsCommandContext;
class ComputeCommandContext;
class CopyCommandContext;

class CommandContext
{
public:
	CommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, ID3D12CommandAllocator* pAllocator, D3D12_COMMAND_LIST_TYPE type);
	virtual ~CommandContext() = default;

	void Reset();
	uint64_t Execute(bool wait);
	uint64_t ExecuteAndReset(bool wait);
	
	void InsertResourceBarrier(GraphicsResource* pBuffer, D3D12_RESOURCE_STATES state, bool executeImmediate = false);
	void FlushResourceBarriers();
	
	void SetDynamicVertexBuffer(int rootIndex, int elementCount, int elementSize, void* pData);
	void SetDynamicIndexBuffer(int elementCount, void* pData);

	void SetDynamicDescriptor(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE handle);
	void SetDynamicDescriptor(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE* handle, int count);
	void SetDynamicSamplerDescriptor(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE handle);
	void SetDynamicSamplerDescriptors(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE* handle, int count);

	void SetDescriptorHeap(ID3D12DescriptorHeap* pHeap, D3D12_DESCRIPTOR_HEAP_TYPE type);

	DynamicAllocation AllocateUploadMemory(uint32_t size);
	void InitializeBuffer(GraphicsBuffer* pResource, const void* pData, uint32_t dataSize, uint32_t offset = 0);
	void InitializeTexture(GraphicsTexture* pResource, D3D12_SUBRESOURCE_DATA* pSubresources, int subresourceCount);

	ID3D12GraphicsCommandList* GetCommandList() const { return m_pCommandList; }
	GraphicsCommandContext* AsGraphicsContext();
	ComputeCommandContext* AsComputeContext();
	CopyCommandContext* AsCopyContext();

	D3D12_COMMAND_LIST_TYPE GetType() const { return m_Type; }

	void MarkBegin(const wchar_t* pName);
	void MarkEvent(const wchar_t* pName);
	void MarkEnd();

protected:
	static const int MAX_QUEUED_BARRIERS = 12;

	void BindDescriptorHeaps();

	std::unique_ptr<DynamicDescriptorAllocator> m_pShaderResourceDescriptorAllocator;
	std::unique_ptr<DynamicDescriptorAllocator> m_pSamplerDescriptorAllocator;

	std::array<ID3D12DescriptorHeap*, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_CurrentDescriptorHeaps{};

	std::array<D3D12_RESOURCE_BARRIER, MAX_QUEUED_BARRIERS> m_QueueBarriers{};
	int m_NumQueueBarriers = 0;

	Graphics* m_pGraphics{};

	ID3D12GraphicsCommandList* m_pCommandList{};
	ID3D12CommandAllocator* m_pAllocator{};
	D3D12_COMMAND_LIST_TYPE m_Type;
};

class GraphicsCommandContext : public CommandContext
{
public:
	GraphicsCommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandlist, ID3D12CommandAllocator* pAllocator);

	void SetRootSignature(RootSignature* pRootSignature);
	void SetPipelineState(GraphicsPipelineState* pPipelineState);

	void SetRootConstants(int rootIndex, uint32_t count, const void* pConstants);

	void SetDynamicConstantBufferView(int rootIndex, void* pData, uint32_t dataSize);

	void Draw(int vertexStart, int vertexCount);
	void DrawIndexed(int indexCount, int indexStart, int minVertex = 0);
	void DrawIndexedInstanced(int indexCount, int indexStart, int instanceCount, int minVertex = 0, int instanceStart = 0);

	void ClearRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const Color& color = Color(0.f, 0.f, 0.f, 1.0f));
	void ClearDepth(D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, float depth = 1.0f, unsigned char stencil = 0);

	void SetDepthOnlyTarget(D3D12_CPU_DESCRIPTOR_HANDLE dsv);
	void SetRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv);
	void SetRenderTargets(D3D12_CPU_DESCRIPTOR_HANDLE* pRtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv);
	void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY type);
	
	void SetVertexBuffer(D3D12_VERTEX_BUFFER_VIEW vertexBufferView);
	void SetVertexBuffers(D3D12_VERTEX_BUFFER_VIEW* pBuffers, int bufferCount);
	void SetIndexBuffer(D3D12_INDEX_BUFFER_VIEW indexBufferView);

	void SetViewport(const FloatRect& rect, float minDepth = 0.0f, float maxDepth = 1.0f);
	void SetScissorRect(const FloatRect& rect);
};

class ComputeCommandContext : public CommandContext
{
public:
	ComputeCommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandlist, ID3D12CommandAllocator* pAllocator);
	
	void SetRootSignature(RootSignature* pRootSignature);
	void SetPipelineState(ComputePipelineState* pPipelineState);

	void SetRootConstants(int rootIndex, uint32_t count, const void* pConstants);

	void SetDynamicConstantBufferView(int rootIndex, void* pData, uint32_t dataSize);

	void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
};

class CopyCommandContext : public CommandContext
{
public:
	CopyCommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandlist, ID3D12CommandAllocator* pAllocator);
};

static_assert(sizeof(GraphicsCommandContext) == sizeof(CommandContext), "should not have extra member variables!");
static_assert(sizeof(ComputeCommandContext) == sizeof(CommandContext), "should not have extra member variables!");
static_assert(sizeof(CopyCommandContext) == sizeof(CommandContext), "should not have extra member variables!");