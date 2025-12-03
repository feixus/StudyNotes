#pragma once

#include "Shader.h"
#include "DescriptorHandle.h"

class CommandQueue;
class CommandContext;
class OfflineDescriptorAllocator;
class OnlineDescriptorAllocator;
class DynamicAllocationManager;
class GraphicsResource;
class GraphicsTexture;
class Buffer;
class RootSignature;
class PipelineState;
class ShaderManager;
class PipelineStateInitializer;
class StateObject;
class StateObjectInitializer;
class GlobalOnlineDescriptorHeap;
class ResourceView;
class SwapChain;
class Fence;
class GraphicsDevice;
class CommandSignature;
struct TextureDesc;
struct BufferDesc;

enum class GraphicsInstanceFlags
{
	None = 0,
	DebugDevice = 1 << 0,
	DRED = 1 << 1,
	GpuValidation = 1 << 2,
	Pix = 1 << 3,
	RenderDoc = 1 << 4,
};
DECLARE_BITMASK_TYPE(GraphicsInstanceFlags);

class GraphicsCapabilities
{
public:
	void Initialize(GraphicsDevice* pDevice);

	bool SupportsRaytracing() const { return RayTracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED; }
	bool SupportMeshShading() const { return MeshShaderSupport != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED; }
	bool SupportVSR() const { return VSRTier != D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED; }
	bool SupportsSamplerFeedback() const { return SamplerFeedbackSupport != D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED; }
	void GetShaderModel(uint8_t& maj, uint8_t& min) const { maj = (uint8_t)(ShaderModel >> 0x4); min = (uint8_t)(ShaderModel & 0xF); }
	bool CheckUAVSupport(DXGI_FORMAT format) const;

	D3D12_RENDER_PASS_TIER RenderPassTier{ D3D12_RENDER_PASS_TIER_0 };
	D3D12_RAYTRACING_TIER RayTracingTier{ D3D12_RAYTRACING_TIER_NOT_SUPPORTED };
	D3D12_MESH_SHADER_TIER MeshShaderSupport{ D3D12_MESH_SHADER_TIER_NOT_SUPPORTED };
	D3D12_SAMPLER_FEEDBACK_TIER SamplerFeedbackSupport{ D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED };
	D3D12_VARIABLE_SHADING_RATE_TIER VSRTier{ D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED };
	int VSRTileSize{-1};
	uint16_t ShaderModel{(D3D_SHADER_MODEL)0};

private:
	GraphicsDevice* m_pDevice{nullptr};
};

class GraphicsInstance
{
public:
	GraphicsInstance(GraphicsInstanceFlags createFlags);

	std::unique_ptr<SwapChain> CreateSwapChain(GraphicsDevice* pDevice, HWND pNativeWindow, DXGI_FORMAT format, uint32_t width, uint32_t height, uint32_t numFrames, bool vsync);
	ComPtr<IDXGIAdapter4> EnumerateAdapter(bool useWarp);
	std::unique_ptr<GraphicsDevice> CreateDevice(ComPtr<IDXGIAdapter4> pAdapter);

	static std::unique_ptr<GraphicsInstance> CreateInstance(GraphicsInstanceFlags createFlags = GraphicsInstanceFlags::None);

	bool AllowTearing() const { return m_AllowTearing; }

private:
	ComPtr<IDXGIFactory6> m_pFactory;
	bool m_AllowTearing{ false };
};

class SwapChain
{
public:
	SwapChain(GraphicsDevice* pGraphicsDevice, IDXGIFactory6* pFactory, HWND pNativeWindow, DXGI_FORMAT format, uint32_t width, uint32_t height, uint32_t numFrames, bool vsync);
	~SwapChain();
	void OnResize(uint32_t width, uint32_t height);
	void Present();

	void SetVsync(bool vsync) { m_Vsync = vsync; }
	IDXGISwapChain4* GetSwapChain() const { return m_pSwapChain.Get(); }
	GraphicsTexture* GetBackBuffer() const { return m_Backbuffers[m_CurrentImage].get(); }
	GraphicsTexture* GetBackBuffer(uint32_t index) const { return m_Backbuffers[index].get(); }
	uint32_t GetBackbufferIndex() const { return m_CurrentImage; }
	DXGI_FORMAT GetFormat() const { return m_Format; }

private:
	std::vector<std::unique_ptr<GraphicsTexture>> m_Backbuffers;
	ComPtr<IDXGISwapChain4> m_pSwapChain;
	DXGI_FORMAT m_Format;
	uint32_t m_CurrentImage;
	bool m_Vsync;
};

class GraphicsDevice
{
public:
	static const DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;
	static const DXGI_FORMAT RENDER_TARGET_FORMAT = DXGI_FORMAT_R11G11B10_FLOAT;

	GraphicsDevice(IDXGIAdapter4* pAdapter);
	~GraphicsDevice();
	void GarbageCollect();

	bool IsFenceComplete(uint64_t fenceValue);
	void WaitForFence(uint64_t fenceValue);
	uint64_t TickFrameFence();
	void IdleGPU();

	int RegisterBindlessResource(GraphicsTexture* pTexture, GraphicsTexture* pFallback = nullptr);
	int RegisterBindlessResource(ResourceView* pView, ResourceView* pFallback = nullptr);

	CommandQueue* GetCommandQueue(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;
	CommandContext* AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);
	void FreeCommandList(CommandContext* pCommandList);

	void SetMultiSampleCount(uint32_t cnt) { m_SampleCount = cnt; }
	uint32_t GetMultiSampleCount() const { return m_SampleCount; }
	DescriptorHandle GetViewHeapHandle() const;
	GlobalOnlineDescriptorHeap* GetGlobalViewHeap() const { return m_pGlobalViewHeap.get(); }
	DynamicAllocationManager* GetAllocationManager() const { return m_pDynamicAllocationManager.get(); }
	ShaderManager* GetShaderManager() const { return m_pShaderManager.get(); }
	const GraphicsCapabilities& GetCapabilities() const { return m_Capabilities; }

	template<typename DESC_TYPE>
	struct DescriptorSelector {};

	template<>
	struct DescriptorSelector<D3D12_SHADER_RESOURCE_VIEW_DESC>
	{
		static constexpr D3D12_DESCRIPTOR_HEAP_TYPE Type() { return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; }
	};
	template<>
	struct DescriptorSelector<D3D12_UNORDERED_ACCESS_VIEW_DESC>
	{
		static constexpr D3D12_DESCRIPTOR_HEAP_TYPE Type() { return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; }
	};
	template<>
	struct DescriptorSelector<D3D12_CONSTANT_BUFFER_VIEW_DESC>
	{
		static constexpr D3D12_DESCRIPTOR_HEAP_TYPE Type() { return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; }
	};
	template<>
	struct DescriptorSelector<D3D12_RENDER_TARGET_VIEW_DESC>
	{
		static constexpr D3D12_DESCRIPTOR_HEAP_TYPE Type() { return D3D12_DESCRIPTOR_HEAP_TYPE_RTV; }
	};
	template<>
	struct DescriptorSelector<D3D12_DEPTH_STENCIL_VIEW_DESC>
	{
		static constexpr D3D12_DESCRIPTOR_HEAP_TYPE Type() { return D3D12_DESCRIPTOR_HEAP_TYPE_DSV; }
	};

	template<typename DESC_TYPE>
	D3D12_CPU_DESCRIPTOR_HANDLE AllocateDescriptor()
	{
		return m_DescriptorHeaps[DescriptorSelector<DESC_TYPE>::Type()]->AllocateDescriptor();
	}

	template<typename DESC_TYPE>
	void FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
	{
		return m_DescriptorHeaps[DescriptorSelector<DESC_TYPE>::Type()]->FreeDescriptor(descriptor);
	}

	std::unique_ptr<GraphicsTexture> CreateTexture(const TextureDesc& desc, const char* pName);
	std::unique_ptr<Buffer> CreateBuffer(const BufferDesc& desc, const char* pName);

	ID3D12Resource* CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, D3D12_CLEAR_VALUE* pClearValue = nullptr);
	void ReleaseResource(ID3D12Resource* pResource);

	PipelineState* CreatePipeline(const PipelineStateInitializer& psoDesc);
	StateObject* CreateStateObject(const StateObjectInitializer& stateDesc);

	CommandSignature* GetIndirectDrawSignature() const { return m_pIndirectDrawSignature.get(); }
	CommandSignature* GetIndirectDispatchSignature() const { return m_pIndirectDispatchSignature.get(); }

	ID3D12Device* GetDevice() const { return m_pDevice.Get(); }
	ID3D12Device5* GetRaytracingDevice() const { return m_pRaytracingDevice.Get(); }
	Shader* GetShader(const char* pShaderPath, ShaderType shaderType, const char* pEntryPoint = "", const std::vector<ShaderDefine>& defines = {});
	ShaderLibrary* GetLibrary(const char* pShaderPath, const std::vector<ShaderDefine>& defines = {});
	Fence* GetFrameFence() const { return m_pFrameFence.get(); }

private:
	bool m_IsTearingDown{false};
	GraphicsCapabilities m_Capabilities;

	ComPtr<ID3D12Device> m_pDevice;
	ComPtr<ID3D12Device5> m_pRaytracingDevice;

	HANDLE m_DeviceRemovedEvent{0};
	std::unique_ptr<Fence> m_pDeviceRemovalFence;
	std::unique_ptr<Fence> m_pFrameFence;

	std::unique_ptr<OnlineDescriptorAllocator> m_pPersistentDescriptorHeap;
	std::unique_ptr<GlobalOnlineDescriptorHeap> m_pGlobalViewHeap;

	std::array<std::unique_ptr<OfflineDescriptorAllocator>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_DescriptorHeaps;
	std::unique_ptr<DynamicAllocationManager> m_pDynamicAllocationManager;

	std::vector<std::unique_ptr<PipelineState>> m_Pipelines;
	std::vector<std::unique_ptr<StateObject>> m_StateObjects;

	std::array<std::unique_ptr<CommandQueue>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandQueues;
	std::array<std::vector<std::unique_ptr<CommandContext>>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandListPool;
	std::array<std::queue<CommandContext*>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_FreeCommandLists;
	std::vector<ComPtr<ID3D12CommandList>> m_CommandLists;
	std::mutex m_ContextAllocationMutex;

	std::queue<std::pair<uint64_t, ID3D12Resource*>> m_DeferredDeletionQueue;

	std::map<ResourceView*, int> m_ViewToDescriptorIndex;

	int m_SampleCount{1};

	std::unique_ptr<ShaderManager> m_pShaderManager;

	std::unique_ptr<CommandSignature> m_pIndirectDrawSignature;
	std::unique_ptr<CommandSignature> m_pIndirectDispatchSignature;
};
