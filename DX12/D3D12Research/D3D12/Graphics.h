#pragma once

#ifdef PLATFORM_UWP
using WindowHandle = Windows::UI::CoreWindow^;
#else
using WindowHandle = HWND;
#endif

class CommandQueue;
class CommandContext;
class DescriptorAllocator;
class DynamicResourceAllocator;
class ImGuiRenderer;

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
	CommandQueue* GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
	CommandContext* AllocateCommandList(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);
	void FreeCommandList(CommandContext* pCommandContext);

	void IdleGPU();

	DynamicResourceAllocator* GetCpuVisibleAllocator() const { return m_pDynamicCpuVisibleAllocator.get(); }

private:
	void SetDynamicConstantBufferView(CommandContext* pCommandContext);

protected:
	static const uint32_t FRAME_COUNT = 2;

	std::unique_ptr<DynamicResourceAllocator> m_pDynamicCpuVisibleAllocator;

	std::array<std::unique_ptr<CommandQueue>, 1> m_CommandQueues;
	std::vector<std::unique_ptr<CommandContext>> m_CommandListPool;
	std::queue<CommandContext*> m_FreeCommandContexts;

	std::vector<ComPtr<ID3D12CommandList>> m_CommandLists;

	// pipeline objects
	DirectX::SimpleMath::Rectangle m_Viewport;
	DirectX::SimpleMath::Rectangle m_ScissorRect;
	ComPtr<IDXGIFactory7> m_pFactory;
	ComPtr<IDXGISwapChain3> m_pSwapchain;
	ComPtr<ID3D12Device> m_pDevice;
	ComPtr<ID3D12Resource> m_pDepthStencilBuffer;
	std::array<ComPtr<ID3D12Resource>, FRAME_COUNT> m_RenderTargets;

	std::array<std::unique_ptr<DescriptorAllocator>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_DescriptorHeaps;
	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, FRAME_COUNT> m_RenderTargetHandles;
	D3D12_CPU_DESCRIPTOR_HANDLE m_DepthStencilHandle;

	uint32_t m_MsaaQuality = 0;

	// synchronization objects
	uint32_t m_CurrentBackBufferIndex = 0;
	std::array<UINT64, FRAME_COUNT> m_FenceValues = {};

	HWND m_Hwnd;

	uint32_t m_WindowWidth;
	uint32_t m_WindowHeight;

	void MakeWindow();
	void InitD3D(WindowHandle pWindow);
	virtual void CreateSwapchain(WindowHandle pWindow);
	void CreateDescriptorHeaps();

	static LRESULT CALLBACK WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	bool mMaximized = false;
	bool mResizing = false;
	bool mMinimized = false;

	DXGI_FORMAT m_DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DXGI_FORMAT m_RenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	void InitializeAssets();
	void BuildRootSignature();
	void BuildShaderAndInputLayout();
	void BuildGeometry();
	void LoadTexture();
	void BuildPSO();

	std::unique_ptr<ImGuiRenderer> m_pImGuiRenderer;

	ComPtr<ID3D12Resource> m_pTexture;
	DescriptorHandle m_TextureHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE m_SamplerHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE m_SamplerGpuHandle;


	ComPtr<ID3D12Resource> m_pVertexBuffer;
	ComPtr<ID3D12Resource> m_pIndexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
	D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;
	ComPtr<ID3D12RootSignature> m_pRootSignature;
	ComPtr<ID3DBlob> m_pVertexShaderCode;
	ComPtr<ID3DBlob> m_pPixelShaderCode;
	std::vector<D3D12_INPUT_ELEMENT_DESC> m_InputElements;
	ComPtr<ID3D12PipelineState> m_pPipelineStateObject;
	int m_IndexCount = 0;
};