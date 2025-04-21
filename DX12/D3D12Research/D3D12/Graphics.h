#pragma once

#ifdef PLATFORM_UWP
using WindowHandle = Windows::UI::CoreWindow^;
#else
using WindowHandle = HWND;
#endif

class CommandQueue;

class CommandContext
{
public:
	ID3D12GraphicsCommandList* pCommandList;
	ID3D12CommandAllocator* pAllocator;
	D3D12_COMMAND_LIST_TYPE QueueType;
};

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

	CommandQueue* GetMainCommandQueue() const;
	CommandContext* AllocatorCommandList(D3D12_COMMAND_LIST_TYPE type);
	void FreeCommandList(CommandContext* pCommandContext);

	uint64_t ExecuteCommandList(CommandContext* pCommandContext, bool waitForCompletion = false);
	void IdleGPU();

protected:
	static const uint32_t FRAME_COUNT = 2;

	uint64_t m_CurrentFence = 0;

	std::map<D3D12_COMMAND_LIST_TYPE, std::unique_ptr<CommandQueue>> m_CommandQueues;
	std::vector<CommandContext> m_CommandListPool;
	std::queue<CommandContext*> m_FreeCommandLists;

	std::vector<ComPtr<ID3D12CommandList>> m_CommandLists;

	// pipeline objects
	D3D12_VIEWPORT m_Viewport;
	D3D12_RECT m_ScissorRect;
	ComPtr<IDXGIFactory7> m_pFactory;
	ComPtr<IDXGISwapChain3> m_pSwapchain;
	ComPtr<ID3D12Device> m_pDevice;
	ComPtr<ID3D12Resource> m_pDepthStencilBuffer;
	std::array<ComPtr<ID3D12Resource>, FRAME_COUNT> m_RenderTargets;

	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackBufferView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const;
	ID3D12Resource* CurrentBackBuffer() const;

	uint32_t m_RtvDescriptorSize;
	uint32_t m_DsvDescriptorSize;
	uint32_t m_CbvSrvDescriptorSize;

	uint32_t m_MsaaQuality = 0;

	ComPtr<ID3D12DescriptorHeap> m_pRtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_pDsvHeap;

	// synchronization objects
	uint32_t m_CurrentBackBufferIndex = 0;
	std::array<UINT64, FRAME_COUNT> m_FenceValues = {};

	HWND m_Hwnd;

	uint32_t m_WindowWidth;
	uint32_t m_WindowHeight;

	void MakeWindow();
	void InitD3D(WindowHandle pWindow);
	void CreateCommandObjects();
	virtual void CreateSwapchain(WindowHandle pWindow);
	void CreateRtvAndDsvHeaps();

	static LRESULT CALLBACK WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	bool mMaximized = false;
	bool mResizing = false;
	bool mMinimized = false;

	DXGI_FORMAT m_DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DXGI_FORMAT m_RenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	void InitializeAssets();

	void BuildDescriptorHeaps();
	void BuildConstantBuffers();
	void BuildRootSignature();
	void BuildShaderAndInputLayout();
	void BuildGeometry();
	void BuildPSO();

	ComPtr<ID3D12DescriptorHeap> m_pCbvSrvHeap;
	ComPtr<ID3D12Resource> m_pVertexBuffer;
	ComPtr<ID3D12Resource> m_pIndexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
	D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;
	ComPtr<ID3D12RootSignature> m_pRootSignature;
	ComPtr<ID3DBlob> m_pVertexShaderCode;
	ComPtr<ID3DBlob> m_pPixelShaderCode;
	std::vector<D3D12_INPUT_ELEMENT_DESC> m_InputElements;
	ComPtr<ID3D12PipelineState> m_pPipelineStateObject;
	ComPtr<ID3D12Resource> m_pConstantBuffer;
	int m_IndexCount = 0;
};