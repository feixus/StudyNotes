#pragma once
#include "Light.h"

class CommandQueue;
class CommandContext;
class DescriptorAllocator;
class DynamicResourceAllocator;
class ImGuiRenderer;
class GraphicsBuffer;
class GraphicsResource;
class RootSignature;
class GraphicsPipelineState;
class ComputePipelineState;
class GraphicsTexture;
class Mesh;
class StructuredBuffer;

using namespace DirectX;
using namespace DirectX::SimpleMath;

class Graphics
{
public:
	Graphics(uint32_t width, uint32_t height, int sampleCount = 1);
	~Graphics();

	virtual void Initialize(HWND hWnd);
	virtual void Update();
	virtual void Shutdown();

	ID3D12Device* GetDevice() const { return m_pDevice.Get(); }
	void OnResize(int width, int height);

	void WaitForFence(uint64_t fenceValue);
	void IdleGPU();

	CommandQueue* GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
	CommandContext* AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);
	void FreeCommandList(CommandContext* pCommandContext);

	DynamicResourceAllocator* GetCpuVisibleAllocator() const { return m_pDynamicCpuVisibleAllocator.get(); }
	D3D12_CPU_DESCRIPTOR_HANDLE AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type);

	int32_t GetWindowWidth() const { return m_WindowWidth; }
	int32_t GetWindowHeight() const { return m_WindowHeight; }

	bool IsFenceComplete(uint64_t fenceValue);

	GraphicsTexture* GetDepthStencil() const { return m_pDepthStencil.get(); }
	GraphicsTexture* GetResolveDepthStencil() const { return m_SampleCount > 1 ? m_pResolveDepthStencil.get() : m_pDepthStencil.get(); }
	GraphicsTexture* GetCurrentRenderTarget() const { return m_SampleCount > 1 ? m_MultiSampleRenderTargets[m_CurrentBackBufferIndex].get() : GetCurrentBackbuffer(); }
	GraphicsTexture* GetCurrentBackbuffer() const { return m_RenderTargets[m_CurrentBackBufferIndex].get(); }

	uint32_t GetMultiSampleCount() const { return m_SampleCount; }
	uint32_t GetMultiSampleQualityLevel(uint32_t msaa);

	static const uint32_t FRAME_COUNT = 3;
	static const DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;
	static const DXGI_FORMAT RENDER_TARGET_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
	static const int FORWARD_PLUS_BLOCK_SIZE = 16;
	static const int MAX_LIGHT_COUNT = 512;

private:
	uint64_t GetFenceToWaitFor();

	void BeginFrame();
	void EndFrame(uint64_t fenceValue);

	void InitD3D();
	void InitializeAssets();
	void CreateSwapchain();

	void UpdateImGui();

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

	std::array<std::unique_ptr<GraphicsTexture>, FRAME_COUNT> m_MultiSampleRenderTargets;
	std::array<std::unique_ptr<GraphicsTexture>, FRAME_COUNT> m_RenderTargets;

	std::array<std::unique_ptr<DescriptorAllocator>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_DescriptorHeaps;
	std::unique_ptr<DynamicResourceAllocator> m_pDynamicCpuVisibleAllocator;

	std::array<std::unique_ptr<CommandQueue>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandQueues;
	std::array<std::vector<std::unique_ptr<CommandContext>>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandListPool;
	std::array<std::queue<CommandContext*>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_FreeCommandContexts;
	std::vector<ComPtr<ID3D12CommandList>> m_CommandLists;
	std::mutex m_ContextAllocationMutex;

	std::unique_ptr<ImGuiRenderer> m_pImGuiRenderer;

	FloatRect m_Viewport;
	FloatRect m_ScissorRect;
	uint32_t m_WindowWidth;
	uint32_t m_WindowHeight;

	// synchronization objects
	uint32_t m_CurrentBackBufferIndex{0};
	std::array<UINT64, FRAME_COUNT> m_FenceValues{};

	std::unique_ptr<Mesh> m_pMesh;

	std::unique_ptr<RootSignature> m_pRootSignature;
	std::unique_ptr<GraphicsPipelineState> m_pPipelineStateObject;

	std::unique_ptr<GraphicsTexture> m_pShadowMap;
	std::unique_ptr<RootSignature> m_pShadowRootSignature;
	std::unique_ptr<GraphicsPipelineState> m_pShadowPipelineStateObject;

	std::unique_ptr<RootSignature> m_pComputeGenerateFrustumsRootSignature;
	std::unique_ptr<ComputePipelineState> m_pComputeGenerateFrustumsPipeline;
	std::unique_ptr<StructuredBuffer> m_pFrustumBuffer;
	bool m_FrustumDirty = true;

	std::unique_ptr<RootSignature> m_pComputeLightCullRootSignature;
	std::unique_ptr<ComputePipelineState> m_pComputeLightCullPipeline;
	std::unique_ptr<StructuredBuffer> m_pLightIndexCounterBuffer;
	std::unique_ptr<StructuredBuffer> m_pLightIndexListBuffer;
	std::unique_ptr<GraphicsTexture> m_pLightGrid;

	std::unique_ptr<RootSignature> m_pDepthPrepassRootSignature;
	std::unique_ptr<GraphicsPipelineState> m_pDepthPrepassPipelineStateObject;
	std::unique_ptr<GraphicsTexture> m_pDepthStencil;
	std::unique_ptr<GraphicsTexture> m_pResolveDepthStencil;

	std::vector<Light> m_Lights;
};