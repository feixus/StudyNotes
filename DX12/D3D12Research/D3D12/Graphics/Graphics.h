#pragma once
#include "Light.h"

class CommandQueue;
class CommandContext;
class DescriptorAllocator;
class DynamicAllocationManager;
class ImGuiRenderer;
class GraphicsBuffer;
class RootSignature;
class GraphicsPipelineState;
class ComputePipelineState;
class GraphicsTexture2D;
class Mesh;
class StructuredBuffer;
class SubMesh;
class GraphicsProfiler;
struct Material;

struct Batch
{
	const SubMesh* pMesh{};
	const Material* pMaterial{};
	Matrix WorldMatrix;
};

class Graphics
{
public:
	Graphics(uint32_t width, uint32_t height, int sampleCount = 1);
	~Graphics();

	virtual void Initialize(HWND hWnd);
	virtual void Update();
	virtual void Shutdown();

	inline ID3D12Device* GetDevice() const { return m_pDevice.Get(); }
	void OnResize(int width, int height);

	void WaitForFence(uint64_t fenceValue);
	void IdleGPU();

	CommandQueue* GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
	CommandContext* AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);
	void FreeCommandList(CommandContext* pCommandContext);

	DynamicAllocationManager* GetAllocationManager() const { return m_pDynamicAllocationManager.get(); }
	D3D12_CPU_DESCRIPTOR_HANDLE AllocateCpuDescriptors(int count, D3D12_DESCRIPTOR_HEAP_TYPE type);

	int32_t GetWindowWidth() const { return m_WindowWidth; }
	int32_t GetWindowHeight() const { return m_WindowHeight; }

	bool IsFenceComplete(uint64_t fenceValue);

	GraphicsTexture2D* GetDepthStencil() const { return m_pDepthStencil.get(); }
	GraphicsTexture2D* GetResolveDepthStencil() const { return m_SampleCount > 1 ? m_pResolveDepthStencil.get() : m_pDepthStencil.get(); }
	GraphicsTexture2D* GetCurrentRenderTarget() const { return m_SampleCount > 1 ? m_MultiSampleRenderTargets[m_CurrentBackBufferIndex].get() : GetCurrentBackbuffer(); }
	GraphicsTexture2D* GetCurrentBackbuffer() const { return m_RenderTargets[m_CurrentBackBufferIndex].get(); }

	uint32_t GetMultiSampleCount() const { return m_SampleCount; }
	uint32_t GetMultiSampleQualityLevel(uint32_t msaa);

	ID3D12Resource* CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType);

	// constants
	static const uint32_t FRAME_COUNT = 3;
	static const int32_t FORWARD_PLUS_BLOCK_SIZE = 16;
	static const int32_t MAX_SHADOW_CASTERS = 8;
	static const int32_t SHADOW_MAP_SIZE = 4096;
	static const int32_t MAX_LIGHT_COUNT = 2048;
	static const int32_t MAX_LIGHT_DENSITY = 720000;
	static const DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;
	static const DXGI_FORMAT DEPTH_STENCIL_SHADOW_FORMAT = DXGI_FORMAT_D16_UNORM;
	static const DXGI_FORMAT RENDER_TARGET_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

private:
	void BeginFrame();
	void EndFrame(uint64_t fenceValue);

	void InitD3D();
	void InitializeAssets();
	void CreateSwapchain();

	void UpdateImGui();

	void RandomizeLights();

	void SortBatchesBackToFront(const Vector3& cameraPosition, std::vector<Batch>& batches);

	double m_LoadSponzaTime{0.0f};
	std::vector<float> m_FrameTimes;

	Vector3 m_CameraPosition;
	Quaternion m_CameraRotation;

	HWND m_pWindow{};

	ComPtr<IDXGIFactory7> m_pFactory;
	ComPtr<IDXGISwapChain3> m_pSwapchain;
	ComPtr<ID3D12Device> m_pDevice;

	int m_SampleCount{1};
	int m_SampleQuality{0};

	std::array<std::unique_ptr<GraphicsTexture2D>, FRAME_COUNT> m_MultiSampleRenderTargets;
	std::array<std::unique_ptr<GraphicsTexture2D>, FRAME_COUNT> m_RenderTargets;

	std::array<std::unique_ptr<DescriptorAllocator>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_DescriptorHeaps;
	std::unique_ptr<DynamicAllocationManager> m_pDynamicAllocationManager;

	std::array<std::unique_ptr<CommandQueue>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandQueues;
	std::array<std::vector<std::unique_ptr<CommandContext>>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandListPool;
	std::array<std::queue<CommandContext*>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_FreeCommandContexts;
	std::vector<ComPtr<ID3D12CommandList>> m_CommandLists;
	std::mutex m_ContextAllocationMutex;

	std::unique_ptr<ImGuiRenderer> m_pImGuiRenderer;

	uint32_t m_WindowWidth;
	uint32_t m_WindowHeight;

	// synchronization objects
	uint32_t m_CurrentBackBufferIndex{0};
	std::array<UINT64, FRAME_COUNT> m_FenceValues{};

	std::unique_ptr<Mesh> m_pMesh;
	std::vector<Batch> m_OpaqueBatches;
	std::vector<Batch> m_TransparentBatches;

	// diffuse scene passes
	std::unique_ptr<RootSignature> m_pDiffuseRS;
	std::unique_ptr<GraphicsPipelineState> m_pDiffusePSO;
	std::unique_ptr<GraphicsPipelineState> m_pDiffuseAlphaPSO;
	std::unique_ptr<GraphicsPipelineState> m_pDiffusePSODebug;
	bool m_UseDebugView = false;

	// directional light shadow mapping
	std::unique_ptr<GraphicsTexture2D> m_pShadowMap;
	std::unique_ptr<RootSignature> m_pShadowRS;
	std::unique_ptr<GraphicsPipelineState> m_pShadowPSO;
	std::unique_ptr<RootSignature> m_pShadowAlphaRS;
	std::unique_ptr<GraphicsPipelineState> m_pShadowAlphaPSO;

	// light culling
	std::unique_ptr<RootSignature> m_pComputeLightCullRS;
	std::unique_ptr<ComputePipelineState> m_pComputeLightCullPipeline;
	std::unique_ptr<StructuredBuffer> m_pLightIndexCounter;
	std::unique_ptr<StructuredBuffer> m_pLightIndexListBufferOpaque;
	std::unique_ptr<GraphicsTexture2D> m_pLightGridOpaque;
	std::unique_ptr<StructuredBuffer> m_pLightIndexListBufferTransparent;
	std::unique_ptr<GraphicsTexture2D> m_pLightGridTransparent;

	// depth prepass
	std::unique_ptr<RootSignature> m_pDepthPrepassRS;
	std::unique_ptr<GraphicsPipelineState> m_pDepthPrepassPSO;

	// MSAA depth resolve
	std::unique_ptr<RootSignature> m_pResolveDepthRS;
	std::unique_ptr<ComputePipelineState> m_pResolveDepthPSO;
	std::unique_ptr<GraphicsTexture2D> m_pDepthStencil;
	std::unique_ptr<GraphicsTexture2D> m_pResolveDepthStencil;

	// light data
	int m_ShadowCasters{0};
	std::vector<Light> m_Lights;
	std::unique_ptr<StructuredBuffer> m_pLightBuffer;
};