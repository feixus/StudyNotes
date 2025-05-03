#pragma once

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

using namespace DirectX;
using namespace DirectX::SimpleMath;

class Graphics
{
public:
	Graphics(uint32_t width, uint32_t height);
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

	GraphicsTexture* GetDepthStencilView() const { return m_pDepthStencilBuffer.get(); }
	GraphicsTexture* GetCurrentRenderTarget() const { return m_RenderTargets[m_CurrentBackBufferIndex].get(); }

	static const uint32_t FRAME_COUNT = 3;
	static const DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;
	static const DXGI_FORMAT RENDER_TARGET_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

private:
	void InitD3D(HWND hWnd);
	void InitializeAssets();
	void CreateSwapchain(HWND hWnd);

	void UpdateImGui();

	ComPtr<IDXGIFactory7> m_pFactory;
	ComPtr<IDXGISwapChain3> m_pSwapchain;
	ComPtr<ID3D12Device> m_pDevice;

	std::array<std::unique_ptr<GraphicsTexture>, FRAME_COUNT> m_RenderTargets;
	std::unique_ptr<GraphicsTexture> m_pDepthStencilBuffer;

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
	uint32_t m_CurrentBackBufferIndex = 0;
	std::array<UINT64, FRAME_COUNT> m_FenceValues = {};

	std::unique_ptr<Mesh> m_pMesh;

	std::unique_ptr<RootSignature> m_pRootSignature;
	std::unique_ptr<GraphicsPipelineState> m_pPipelineStateObject;

	std::unique_ptr<RootSignature> m_pComputeTestRootSignature;
	std::unique_ptr<ComputePipelineState> m_pComputePipelineStateObject;
	std::unique_ptr<GraphicsTexture> m_pTestTargetTexture;
};