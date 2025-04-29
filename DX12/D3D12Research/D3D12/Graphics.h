#pragma once
#include "DescriptorHandle.h"

using WindowHandle = HWND;

class CommandQueue;
class CommandContext;
class DescriptorAllocator;
class DynamicResourceAllocator;
class ImGuiRenderer;
class GraphicsBuffer;
class GraphicsResource;
class RootSignature;
class PipelineState;
class GraphicsTexture;
class Mesh;

using namespace DirectX;
using namespace DirectX::SimpleMath;

class Graphics
{
public:
	Graphics(uint32_t width, uint32_t height);
	~Graphics();

	virtual void Initialize(WindowHandle window);
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
	DescriptorAllocator* GetGpuVisibleSRVAllocator() const { return m_pTextureGpuDescriptorHeap.get(); }
	D3D12_CPU_DESCRIPTOR_HANDLE AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type);

	int32_t GetWindowWidth() const { return m_WindowWidth; }
	int32_t GetWindowHeight() const { return m_WindowHeight; }

private:
	void MakeWindow();
	void InitD3D(WindowHandle pWindow);
	void InitializeAssets();
	void SetDynamicConstantBufferView(CommandContext* pCommandContext);
	void CreateSwapchain(WindowHandle pWindow);

	static const uint32_t FRAME_COUNT = 2;

	ComPtr<IDXGIFactory7> m_pFactory;
	ComPtr<IDXGISwapChain3> m_pSwapchain;
	ComPtr<ID3D12Device> m_pDevice;
	std::array<std::unique_ptr<GraphicsResource>, FRAME_COUNT> m_RenderTargets;
	std::unique_ptr<GraphicsResource> m_pDepthStencilBuffer;
	std::array<std::unique_ptr<DescriptorAllocator>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_DescriptorHeaps;
	std::unique_ptr<DescriptorAllocator> m_pTextureGpuDescriptorHeap;
	std::unique_ptr<DynamicResourceAllocator> m_pDynamicCpuVisibleAllocator;
	std::array<std::unique_ptr<CommandQueue>, 1> m_CommandQueues;
	std::vector<std::unique_ptr<CommandContext>> m_CommandListPool;
	std::queue<CommandContext*> m_FreeCommandContexts;
	std::vector<ComPtr<ID3D12CommandList>> m_CommandLists;

	std::unique_ptr<ImGuiRenderer> m_pImGuiRenderer;

	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, FRAME_COUNT> m_RenderTargetHandles;
	D3D12_CPU_DESCRIPTOR_HANDLE m_DepthStencilHandle;

	FloatRect m_Viewport;
	FloatRect m_ScissorRect;
	uint32_t m_WindowWidth;
	uint32_t m_WindowHeight;

	// synchronization objects
	uint32_t m_CurrentBackBufferIndex = 0;
	std::array<UINT64, FRAME_COUNT> m_FenceValues = {};

	DXGI_FORMAT m_DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DXGI_FORMAT m_RenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	std::unique_ptr<Mesh> m_pMesh;
	std::unique_ptr<GraphicsTexture> m_pTexture;
	DescriptorHandle m_TextureHandle;

	std::unique_ptr<RootSignature> m_pRootSignature;
	std::unique_ptr<PipelineState> m_pPipelineStateObject;

	int m_IndexCount = 0;
	uint32_t m_MsaaQuality = 0;

	HWND m_Hwnd;
	bool mMaximized = false;
	bool mResizing = false;
	bool mMinimized = false;

	static LRESULT CALLBACK WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
};