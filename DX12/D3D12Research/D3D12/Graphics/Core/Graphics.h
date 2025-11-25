#pragma once
#include "../Light.h"
#include "Core/Bitfield.h"
#include "DescriptorHandle.h"

class CommandQueue;
class CommandContext;
class OfflineDescriptorAllocator;
class DynamicAllocationManager;
class ImGuiRenderer;
class GraphicsBuffer;
class RootSignature;
class PipelineState;
class GraphicsTexture;
class Mesh;
class Buffer;
struct SubMesh;
class GraphicsProfiler;
class ClusteredForward;
struct Material;
class Camera;
class UnorderedAccessView;
class TiledForward;
class RTAO;
class SSAO;
class GpuParticles;
class RTReflections;
class ShaderManager;
class PipelineStateInitializer;
class StateObject;
class StateObjectInitializer;
class GlobalOnlineDescriptorHeap;
class ResourceView;

class Graphics;
class SwapChain;

enum class GraphicsFlags
{
	None = 0,
	DebugDevice = 1 << 0,
	DRED = 1 << 1,
	GpuValidation = 1 << 2,
	Pix = 1 << 3,
};
DECLARE_BITMASK_TYPE(GraphicsFlags);

class GraphicsDevice
{
public:
	GraphicsDevice(IDXGIAdapter4* pAdapter);
	void Destroy();
	void GarbageCollect();

	bool IsFenceComplete(uint64_t fenceValue);
	void WaitForFence(uint64_t fenceValue);
	void IdleGPU();

	int RegisterBindlessResource(GraphicsTexture* pTexture, GraphicsTexture* pFallback = nullptr);
	int RegisterBindlessResource(ResourceView* pView, ResourceView* pFallback = nullptr);

	CommandQueue* GetCommandQueue(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;
	CommandContext* AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);
	void FreeCommandList(CommandContext* pCommandList);

	bool CheckTypedUAVSupport(DXGI_FORMAT format) const;
	bool UseRenderPasses() const;
	bool SupportsRaytracing() const { return m_RayTracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED; }
	bool SupportMeshShaders() const { return m_MeshShaderSupport != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED; }
	bool GetShaderModel(int& major, int& minor) const;

	void SetMultiSampleCount(uint32_t cnt) { m_SampleCount = cnt; }
	uint32_t GetMultiSampleCount() const { return m_SampleCount; }
	ShaderManager* GetShaderManager() const { return m_pShaderManager.get(); }
	GlobalOnlineDescriptorHeap* GetGlobalViewHeap() const { return m_pGlobalViewHeap.get(); }
	DynamicAllocationManager* GetAllocationManager() const { return m_pDynamicAllocationManager.get(); }

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

	ID3D12Resource* CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, D3D12_CLEAR_VALUE* pClearValue = nullptr);
	PipelineState* CreatePipeline(const PipelineStateInitializer& psoDesc);
	StateObject* CreateStateObject(const StateObjectInitializer& stateDesc);

	ID3D12Device* GetDevice() const { return m_pDevice.Get(); }
	ID3D12Device5* GetRaytracingDevice() const { return m_pRaytracingDevice.Get(); }

private:
	ComPtr<ID3D12Device> m_pDevice;
	ComPtr<ID3D12Device5> m_pRaytracingDevice;

	ComPtr<ID3D12Fence> m_pDeviceRemovalFence;
	HANDLE m_DeviceRemovedEvent{0};

	std::unique_ptr<class OnlineDescriptorAllocator> m_pPersistentDescriptorHeap;
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

	D3D12_RENDER_PASS_TIER m_RenderPassTier{ D3D12_RENDER_PASS_TIER_0 };
	D3D12_RAYTRACING_TIER m_RayTracingTier{ D3D12_RAYTRACING_TIER_NOT_SUPPORTED };
	D3D12_MESH_SHADER_TIER m_MeshShaderSupport{ D3D12_MESH_SHADER_TIER_NOT_SUPPORTED };
	D3D12_SAMPLER_FEEDBACK_TIER m_SamplerFeedbackSupport{ D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED };
	D3D12_VARIABLE_SHADING_RATE_TIER m_VSRTier{ D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED };
	uint8_t m_ShaderModelMajor{0};
	uint8_t m_ShaderModelMinor{0};
	int m_VSRTileSize{-1};

	std::map<ResourceView*, int> m_ViewToDescriptorIndex;

	int m_SampleCount{1};

	std::unique_ptr<ShaderManager> m_pShaderManager;
};

class GraphicsInstance
{
public:
	static GraphicsInstance CreateInstance(GraphicsFlags createFlags = GraphicsFlags::None);
	std::unique_ptr<SwapChain> CreateSwapChain(GraphicsDevice* pDevice, void* pNativeWindow, DXGI_FORMAT format, uint32_t width, uint32_t height, uint32_t numFrames, bool vsync);
	ComPtr<IDXGIAdapter4> EnumerateAdapter(bool useWarp);
	std::unique_ptr<GraphicsDevice> CreateDevice(ComPtr<IDXGIAdapter4> pAdapter, GraphicsFlags createFlags = GraphicsFlags::None);

	bool AllowTearing() const { return m_AllowTearing; }

private:
	ComPtr<IDXGIFactory6> m_pFactory;
	bool m_AllowTearing{false};
};

class SwapChain
{
public:
	SwapChain(GraphicsDevice* pGraphicsDevice, IDXGIFactory6* pFactory, void* pNativeWindow, DXGI_FORMAT format, uint32_t width, uint32_t height, uint32_t numFrames, bool vsync);
	void Destroy();
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

enum class DefaultTexture
{
	White2D,
	Black2D,
	Magenta2D,
	Gray2D,
	Normal2D,
	RoughnessMetalness,
	BlackCube,
	ColorNoise256,
	BlueNoise512,
	MAX
};

struct MaterialData
{
	int Diffuse{ 0 };
	int Normal{ 0 };
	int RoughnessMetalness{ 0 };
	int Emissive;
};

struct Batch
{
	enum class Blending
	{
		Opaque = 1,
		AlphaMask = 2,
		AlphaBlend = 4,
	};
	int Index{ 0 };
	Blending BlendMode{ Blending::Opaque };
	const SubMesh* pMesh{};
	MaterialData Material;
	Matrix WorldMatrix;
	BoundingBox LocalBounds;
	BoundingBox Bounds;
	float Radius{ 0 };
	int VertexBufferDescriptor{ -1 };
	int IndexBufferDescriptor{ -1 };
};
DECLARE_BITMASK_TYPE(Batch::Blending);

constexpr const int MAX_SHADOW_CASTERS = 32;
struct ShadowData
{
	Matrix LightViewProjections[MAX_SHADOW_CASTERS];
	float CascadeDepths[4]{};
	uint32_t NumCascades{ 0 };
	uint32_t ShadowMapOffset{ 0 };
};

struct SceneData
{
	GraphicsTexture* pDepthBuffer{ nullptr };
	GraphicsTexture* pResolvedDepth{ nullptr };
	GraphicsTexture* pRenderTarget{ nullptr };
	GraphicsTexture* pResolvedTarget{ nullptr };
	GraphicsTexture* pPreviousColor{ nullptr };
	GraphicsTexture* pNormals{ nullptr };
	GraphicsTexture* pResolvedNormals{ nullptr };
	GraphicsTexture* pAO{ nullptr };
	GraphicsTexture* pReflection{ nullptr };
	std::vector<Batch> Batches;
	DescriptorHandle GlobalSRVHeapHandle{};
	Buffer* pLightBuffer{ nullptr };
	Camera* pCamera{ nullptr };
	ShadowData* pShadowData{ nullptr };
	int SceneTLAS{ 0 };
	int FrameIndex{ 0 };
	BitField<2048> VisibilityMask;
};

enum class RenderPath
{
	Tiled,
	Clustered,
};

void DrawScene(CommandContext& context, const SceneData& scene, Batch::Blending blendModes);

class Graphics
{
public:
	Graphics(HWND hWnd, uint32_t width, uint32_t height, int sampleCount = 1);
	~Graphics();

	void Update();
	void OnResize(int width, int height);

	int32_t GetWindowWidth() const { return m_WindowWidth; }
	int32_t GetWindowHeight() const { return m_WindowHeight; }

	ImGuiRenderer* GetImGui() const { return m_pImGuiRenderer.get(); }
	GraphicsTexture* GetDefaultTexture(DefaultTexture type) const { return m_DefaultTextures[(int)type].get(); }
	GraphicsTexture* GetDepthStencil() const { return m_pDepthStencil.get(); }
	GraphicsTexture* GetResolveDepthStencil() const { return m_pResolveDepthStencil.get(); }
	GraphicsTexture* GetCurrentRenderTarget() const { return m_SampleCount > 1 ? m_pMultiSampleRenderTarget.get() : m_pHDRRenderTarget.get(); }
	GraphicsTexture* GetCurrentBackbuffer() const { return m_pSwapChain->GetBackBuffer(); }
	uint32_t GetMultiSampleCount() const { return m_SampleCount; }
	ShaderManager* GetShaderManager() const { return m_pDevice->GetShaderManager(); }
	GraphicsDevice* GetDevice() const { return m_pDevice.get(); }

	// constants
	static const uint32_t FRAME_COUNT = 3;
	static const DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;
	static const DXGI_FORMAT DEPTH_STENCIL_SHADOW_FORMAT = DXGI_FORMAT_D16_UNORM;
	static const DXGI_FORMAT RENDER_TARGET_FORMAT = DXGI_FORMAT_R11G11B10_FLOAT;
	static const DXGI_FORMAT SWAPCHAIN_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

private:
	void BeginFrame();
	void EndFrame();

	void InitD3D();
	void InitializePipelines();
	void InitializeAssets(CommandContext& context);
	void SetupScene(CommandContext& context);

	void UpdateImGui();
	void UpdateTLAS(CommandContext& context);

	std::unique_ptr<GraphicsDevice> m_pDevice;

	HWND m_pWindow{};
	uint32_t m_WindowWidth;
	uint32_t m_WindowHeight;
	std::unique_ptr<SwapChain> m_pSwapChain;

	int m_Frame{0};
	std::array<float, 180> m_FrameTimes{};

	std::unique_ptr<GraphicsTexture> m_pLightCookie;
	std::array<std::unique_ptr<GraphicsTexture>, (int)DefaultTexture::MAX> m_DefaultTextures;

	std::unique_ptr<GraphicsTexture> m_pMultiSampleRenderTarget;
	std::unique_ptr<GraphicsTexture> m_pHDRRenderTarget;
	std::unique_ptr<GraphicsTexture> m_pPreviousColor;
	std::unique_ptr<GraphicsTexture> m_pTonemapTarget;
	std::unique_ptr<GraphicsTexture> m_pDepthStencil;
	std::unique_ptr<GraphicsTexture> m_pResolveDepthStencil;
	std::unique_ptr<GraphicsTexture> m_pTAASource;
	std::unique_ptr<GraphicsTexture> m_pVelocity;
	std::unique_ptr<GraphicsTexture> m_pNormals;
	std::unique_ptr<GraphicsTexture> m_pResolvedNormals;
	std::vector<std::unique_ptr<GraphicsTexture>> m_ShadowMaps;

	std::unique_ptr<ImGuiRenderer> m_pImGuiRenderer;
	std::unique_ptr<ClusteredForward> m_pClusteredForward;
	std::unique_ptr<TiledForward> m_pTiledForward;
	std::unique_ptr<RTAO> m_pRTAO;
	std::unique_ptr<SSAO> m_pSSAO;
	std::unique_ptr<RTReflections> m_pRTReflections;

	int32_t m_ScreenshotDelay{-1};
	int32_t m_ScreenshotRowPitch{0};
	std::unique_ptr<Buffer> m_pScreenshotBuffer;

	RenderPath m_RenderPath = RenderPath::Clustered;

	std::vector<std::unique_ptr<Mesh>> m_Meshes;
	std::unique_ptr<Buffer> m_pTLAS;
	std::unique_ptr<Buffer> m_pTLASScratch;

	// shadow mapping
	std::unique_ptr<RootSignature> m_pShadowRS;
	PipelineState* m_pShadowOpaquePSO{nullptr};
	PipelineState* m_pShadowAlphaMaskPSO{nullptr};

	// depth prepass
	std::unique_ptr<RootSignature> m_pDepthPrepassRS;
	PipelineState* m_pDepthPrepassOpaquePSO{nullptr};
	PipelineState* m_pDepthPrepassAlphaMaskPSO{nullptr};

	// MSAA depth resolve
	std::unique_ptr<RootSignature> m_pResolveDepthRS;
	PipelineState* m_pResolveDepthPSO{nullptr};

	// Tonemapping
	std::unique_ptr<GraphicsTexture> m_pDownscaledColor;
	std::unique_ptr<RootSignature> m_pLuminanceHistogramRS;
	PipelineState* m_pLuminanceHistogramPSO{nullptr};
	std::unique_ptr<RootSignature> m_pAverageLuminanceRS;
	PipelineState* m_pAverageLuminancePSO{nullptr};
	std::unique_ptr<RootSignature> m_pToneMapRS;
	PipelineState* m_pToneMapPSO{nullptr};
	std::unique_ptr<RootSignature> m_pDrawHistogramRS;
	PipelineState* m_pDrawHistogramPSO{nullptr};
	std::unique_ptr<Buffer> m_pLuminanceHistogram;
	std::unique_ptr<Buffer> m_pAverageLuminance;

	// SSAO
	std::unique_ptr<GraphicsTexture> m_pAmbientOcclusion;

	// mip generation
	std::unique_ptr<RootSignature> m_pGenerateMipsRS;
	PipelineState* m_pGenerateMipsPSO{nullptr};

	// depth reduction
	PipelineState* m_pPrepareReduceDepthPSO{nullptr};
	PipelineState* m_pPrepareReduceDepthMsaaPSO{nullptr};
	PipelineState* m_pReduceDepthPSO{nullptr};
	std::unique_ptr<RootSignature> m_pReduceDepthRS;
	std::vector<std::unique_ptr<GraphicsTexture>> m_ReductionTargets;
	std::vector<std::unique_ptr<Buffer>> m_ReductionReadbackTargets;

	// TAA
	std::unique_ptr<RootSignature> m_pTemporalResolveRS;
	PipelineState* m_pTemporalResolvePSO{nullptr};

	// Camera motion
	PipelineState* m_pCameraMotionPSO{nullptr};
	std::unique_ptr<RootSignature> m_pCameraMotionRS;

	// sky
	std::unique_ptr<RootSignature> m_pSkyboxRS;
	PipelineState* m_pSkyboxPSO{nullptr};

	// light data
	std::vector<Light> m_Lights;
	std::unique_ptr<Buffer> m_pLightBuffer;

	// particles
	std::unique_ptr<GpuParticles> m_pGpuParticles;

	// clouds
	std::unique_ptr<class Clouds> m_pClouds;

	SceneData m_SceneData;
	std::unique_ptr<Camera> m_pCamera;

	GraphicsTexture* m_pVisualizeTexture{nullptr};

	int m_SampleCount{1};
	bool m_CapturePix{false};
};
