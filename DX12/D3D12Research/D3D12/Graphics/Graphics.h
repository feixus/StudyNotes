#pragma once
#include "Light.h"

class CommandQueue;
class CommandContext;
class OfflineDescriptorAllocator;
class DynamicAllocationManager;
class ImGuiRenderer;
class GraphicsBuffer;
class RootSignature;
class GraphicsPipelineState;
class ComputePipelineState;
class GraphicsTexture;
class Mesh;
class Buffer;
class SubMesh;
class GraphicsProfiler;
class ClusteredForward;
struct Material;
class Camera;
class RGResourceAllocator;
class DebugRenderer;
class UnorderedAccessView;

struct Batch
{
	const SubMesh* pMesh{};
	const Material* pMaterial{};
	Matrix WorldMatrix;
	BoundingBox Bounds;
};

struct LightData
{
	Matrix LightViewProjections[8];
	Vector4 ShadowMapOffsets[8];
};

enum class RenderPath
{
	Tiled,
	Clustered,
};

#define PIX_CAPTURE_SCOPE() PixScopeCapture<__COUNTER__> pix_scope_##__COUNTER__

template<size_t IDX>
class PixScopeCapture
{
public:
	PixScopeCapture()
	{
		static bool hit = false;
		if (hit == false)
		{
			hit = true;
			DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pGa));
			if (pGa)
			{
				pGa->BeginCapture();
			}
		}
	}

	~PixScopeCapture()
	{
		if (pGa)
		{
			pGa->EndCapture();
		}
	}

private:
	ComPtr<IDXGraphicsAnalysis> pGa;
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

	bool BeginPixCapture() const;
	bool EndPixCapture() const;

	CommandQueue* GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
	CommandContext* AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);
	void FreeCommandList(CommandContext* pCommandContext);

	DynamicAllocationManager* GetAllocationManager() const { return m_pDynamicAllocationManager.get(); }
	OfflineDescriptorAllocator* GetDescriptorManager(D3D12_DESCRIPTOR_HEAP_TYPE type) const { return m_DescriptorHeaps[type].get(); }

	int32_t GetWindowWidth() const { return m_WindowWidth; }
	int32_t GetWindowHeight() const { return m_WindowHeight; }

	bool CheckTypedUAVSupport(DXGI_FORMAT format) const;
	bool UseRenderPasses() const;
	bool IsFenceComplete(uint64_t fenceValue);

	GraphicsTexture* GetDepthStencil() const { return m_pDepthStencil.get(); }
	GraphicsTexture* GetResolveDepthStencil() const { return m_SampleCount > 1 ? m_pResolveDepthStencil.get() : m_pDepthStencil.get(); }
	GraphicsTexture* GetCurrentRenderTarget() const { return m_SampleCount > 1 ? m_pMultiSampleRenderTarget.get() : m_pHDRRenderTarget.get(); }
	GraphicsTexture* GetCurrentBackbuffer() const { return m_Backbuffers[m_CurrentBackBufferIndex].get(); }

	Camera* GetCamera() const { return m_pCamera.get(); }

	uint32_t GetMultiSampleCount() const { return m_SampleCount; }
	uint32_t GetMultiSampleQualityLevel(uint32_t msaa);

	ID3D12Resource* CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, D3D12_CLEAR_VALUE* pClearValue = nullptr);

	// constants
	static const uint32_t FRAME_COUNT = 3;
	static const int32_t FORWARD_PLUS_BLOCK_SIZE = 16;
	static const int32_t MAX_SHADOW_CASTERS = 8;
	static const int32_t SHADOW_MAP_SIZE = 4096;
	static const int32_t MAX_LIGHT_DENSITY = 720000;
	static const DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;
	static const DXGI_FORMAT DEPTH_STENCIL_SHADOW_FORMAT = DXGI_FORMAT_D16_UNORM;
	static const DXGI_FORMAT RENDER_TARGET_FORMAT = DXGI_FORMAT_R11G11B10_FLOAT;
	static const DXGI_FORMAT SWAPCHAIN_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

private:
	void BeginFrame();
	void EndFrame(uint64_t fenceValue);

	void InitD3D();
	void InitializeAssets();
	void CreateSwapchain();

	void UpdateImGui();

	void RandomizeLights(int count);

	int m_Frame{0};
	std::array<float, 180> m_FrameTimes{};

	int m_DesiredLightCount = 4096;

	std::unique_ptr<Camera> m_pCamera;

	HWND m_pWindow{};

	ComPtr<IDXGIFactory7> m_pFactory;
	ComPtr<IDXGISwapChain3> m_pSwapchain;
	ComPtr<ID3D12Device> m_pDevice;

	D3D12_RENDER_PASS_TIER m_RenderPassTier{D3D12_RENDER_PASS_TIER_0};

	int m_SampleCount{1};
	int m_SampleQuality{0};


	std::array<std::unique_ptr<GraphicsTexture>, FRAME_COUNT> m_Backbuffers;
	std::unique_ptr<GraphicsTexture> m_pMultiSampleRenderTarget;
	std::unique_ptr<GraphicsTexture> m_pHDRRenderTarget;
	std::unique_ptr<GraphicsTexture> m_pDepthStencil;
	std::unique_ptr<GraphicsTexture> m_pResolveDepthStencil;
	std::unique_ptr<GraphicsTexture> m_pResolvedRenderTarget;
	std::unique_ptr<GraphicsTexture> m_pMSAANormals;
	std::unique_ptr<GraphicsTexture> m_pNormals;

	std::array<std::unique_ptr<OfflineDescriptorAllocator>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_DescriptorHeaps;
	std::unique_ptr<DynamicAllocationManager> m_pDynamicAllocationManager;

	std::array<std::unique_ptr<CommandQueue>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandQueues;
	std::array<std::vector<std::unique_ptr<CommandContext>>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandListPool;
	std::array<std::queue<CommandContext*>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_FreeCommandContexts;
	std::vector<ComPtr<ID3D12CommandList>> m_CommandLists;
	std::mutex m_ContextAllocationMutex;

	std::unique_ptr<ImGuiRenderer> m_pImGuiRenderer;
	std::unique_ptr<RGResourceAllocator> m_pResourceAllocator;
	std::unique_ptr<ClusteredForward> m_pClusteredForward;
	std::unique_ptr<DebugRenderer> m_pDebugRenderer;

	uint32_t m_WindowWidth;
	uint32_t m_WindowHeight;
	bool m_StartPixCapture{false};
	bool m_EndPixCapture{false};

	// synchronization objects
	uint32_t m_CurrentBackBufferIndex{0};
	std::array<UINT64, FRAME_COUNT> m_FenceValues{};

	RenderPath m_RenderPath = RenderPath::Clustered;

	std::unique_ptr<Mesh> m_pMesh;
	std::vector<Batch> m_OpaqueBatches;
	std::vector<Batch> m_TransparentBatches;

	// shadow mapping
	std::unique_ptr<GraphicsTexture> m_pShadowMap;
	std::unique_ptr<RootSignature> m_pShadowRS;
	std::unique_ptr<GraphicsPipelineState> m_pShadowPSO;
	std::unique_ptr<GraphicsPipelineState> m_pShadowAlphaPSO;

	// light culling
	std::unique_ptr<RootSignature> m_pComputeLightCullRS;
	std::unique_ptr<ComputePipelineState> m_pComputeLightCullPipeline;
	std::unique_ptr<Buffer> m_pLightIndexCounter;
	UnorderedAccessView* m_pLightIndexCounterRawUAV{nullptr};
	std::unique_ptr<Buffer> m_pLightIndexListBufferOpaque;
	std::unique_ptr<GraphicsTexture> m_pLightGridOpaque;
	std::unique_ptr<Buffer> m_pLightIndexListBufferTransparent;
	std::unique_ptr<GraphicsTexture> m_pLightGridTransparent;

	// depth prepass
	std::unique_ptr<RootSignature> m_pDepthPrepassRS;
	std::unique_ptr<GraphicsPipelineState> m_pDepthPrepassPSO;

	// MSAA depth resolve
	std::unique_ptr<RootSignature> m_pResolveDepthRS;
	std::unique_ptr<ComputePipelineState> m_pResolveDepthPSO;

	//PBR
	std::unique_ptr<RootSignature> m_pPBRDiffuseRS;
	std::unique_ptr<GraphicsPipelineState> m_pPBRDiffusePSO;
	std::unique_ptr<GraphicsPipelineState> m_pPBRDiffuseAlphaPSO;

	// Tonemapping
	std::unique_ptr<GraphicsTexture> m_pDownscaledColor;
	std::unique_ptr<RootSignature> m_pLuminanceHistogramRS;
	std::unique_ptr<ComputePipelineState> m_pLuminanceHistogramPSO;
	std::unique_ptr<RootSignature> m_pAverageLuminanceRS;
	std::unique_ptr<ComputePipelineState> m_pAverageLuminancePSO;
	std::unique_ptr<RootSignature> m_pToneMapRS;
	std::unique_ptr<GraphicsPipelineState> m_pToneMapPSO;
	std::unique_ptr<Buffer> m_pLuminanceHistogram;
	std::unique_ptr<GraphicsTexture> m_pAverageLuminance;

	// SSAO
	std::unique_ptr<GraphicsTexture> m_pNoiseTexture;
	std::unique_ptr<GraphicsTexture> m_pSSAOTarget;
	std::unique_ptr<RootSignature> m_pSSAORS;
	std::unique_ptr<ComputePipelineState> m_pSSAOPSO;

	// mip generation
	std::unique_ptr<RootSignature> m_pGenerateMipsRS;
	std::unique_ptr<ComputePipelineState> m_pGenerateMipsPSO;

	// light data
	int m_ShadowCasters{0};
	std::vector<Light> m_Lights;
	std::unique_ptr<Buffer> m_pLightBuffer;

	std::unique_ptr<class Clouds> m_pClouds;
};