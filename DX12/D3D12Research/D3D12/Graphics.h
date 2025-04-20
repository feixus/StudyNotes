#pragma once

class CommandAllocatorPool;
class CommandQueue;
class CommandQueueManager;

class Graphics
{
public:
	Graphics(UINT width, UINT height, std::wstring name);
	~Graphics();

	virtual void Initialize();
	virtual void Update();
	virtual void Shutdown();

	ID3D12Device* GetDevice() const { return m_pDevice.Get(); }
	bool IsFenceComplete(const UINT64 fenceValue) const { return false; }
	void OnResize(int width, int height);

protected:
	static const UINT FRAME_COUNT = 2;

	// pipeline objects
	D3D12_VIEWPORT m_Viewport;
	D3D12_RECT m_ScissorRect;
	ComPtr<IDXGIFactory7> m_pFactory;
	ComPtr<IDXGISwapChain3> m_pSwapchain;
	ComPtr<ID3D12Device> m_pDevice;
	ComPtr<ID3D12Resource> m_pDepthStencilBuffer;
	array<ComPtr<ID3D12Resource>, FRAME_COUNT> m_RenderTargets;

	std::unique_ptr<CommandQueueManager> m_pQueueManager;

	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackBufferView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const;
	ID3D12Resource* CurrentBackBuffer() const;

	UINT m_RtvDescriptorSize;
	UINT m_DsvDescriptorSize;
	UINT m_CbvSrvDescriptorSize;

	UINT m_MsaaQuality = 0;

	ComPtr<ID3D12DescriptorHeap> m_pRtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_pDsvHeap;

	// synchronization objects
	UINT m_CurrentBackBufferIndex = 0;
	ComPtr<ID3D12Fence> m_pFence;
	array<UINT64, FRAME_COUNT> m_FenceValues;

	HWND m_Hwnd;

	unsigned int m_WindowWidth;
	unsigned int m_WindowHeight;

	void MakeWindow();
	void InitD3D();
	void CreateCommandObjects();
	virtual void CreateSwapchain();
	void CreateRtvAndDsvHeaps();

	void MoveToNextFrame();


	static LRESULT CALLBACK WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	bool mMaximized = false;
	bool mResizing = false;
	bool mMinimized = false;

	DXGI_FORMAT m_DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DXGI_FORMAT m_RenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	// assets objects
	void InitializeAssets();

	struct PosColVertex
	{
		XMFLOAT3 Position;
		XMFLOAT4 Color;
	};

	void BuildDescriptorHeaps();
	void BuildConstantBuffers();
	void BuildRootSignature();
	void BuildShaderAndInputLayout();
	void BuildGeometry();
	void BuildPSO();

	int m_Timer = 0;

	ComPtr<ID3D12DescriptorHeap> m_pCbvSrvHeap;
	ComPtr<ID3D12Resource> pVertexUploadBuffer;
	ComPtr<ID3D12Resource> pIndexUploadBuffer;
	ComPtr<ID3D12Resource> m_pVertexBuffer;
	ComPtr<ID3D12Resource> m_pIndexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
	D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;
	ComPtr<ID3D12RootSignature> m_pRootSignature;
	ComPtr<ID3DBlob> m_pVertexShaderCode;
	ComPtr<ID3DBlob> m_pPixelShaderCode;
	vector<D3D12_INPUT_ELEMENT_DESC> m_InputElements;
	ComPtr<ID3D12PipelineState> m_pPipelineStateObject;
	ComPtr<ID3D12Resource> m_pConstantBuffer;
};