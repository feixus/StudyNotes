#pragma once
#include "../Light.h"
#include "Core/Bitfield.h"

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
class SubMesh;
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

struct MaterialData
{
	int Diffuse{ 0 };
	int Normal{ 0 };
	int Roughness{ 0 };
	int Metallic{ 0 };
};

struct Batch
{
	enum class Blending
	{
		Opaque = 1,
		AlphaMask = 2,
		AlphaBlend = 4,
	};
	int Index{0};
	Blending BlendMode{Blending::Opaque};
	const SubMesh* pMesh{};
	MaterialData Material;
	Matrix WorldMatrix;
	BoundingBox Bounds;
};
DECLARE_BITMASK_TYPE(Batch::Blending);

constexpr const int MAX_SHADOW_CASTERS = 32;
struct ShadowData
{
	Matrix LightViewProjections[MAX_SHADOW_CASTERS];
	float CascadeDepths[4]{};
	uint32_t NumCascades{ 0 };
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
	std::vector<std::unique_ptr<GraphicsTexture>>* pShadowMaps{ nullptr };
	std::vector<Batch> Batches;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> MaterialTextures;
	Buffer* pLightBuffer{ nullptr };
	Camera* pCamera{ nullptr };
	ShadowData* pShadowData{ nullptr };
	Buffer* pTLAS{ nullptr };
	Mesh* pMesh{ nullptr };
	int FrameIndex{ 0 };
	BitField<128> VisibilityMask;
};

enum class RenderPath
{
	Tiled,
	Clustered,
};

enum class ShowGraph
{
	ShadowMap,
	AO,
};

class Graphics
{
public:
	Graphics(uint32_t width, uint32_t height, int sampleCount = 1);
	~Graphics();

	void Initialize(HWND hWnd);
	void Update();
	void Shutdown();

	void OnResize(int width, int height);

	void WaitForFence(uint64_t fenceValue);
	void IdleGPU();

	inline ID3D12Device* GetDevice() const { return m_pDevice.Get(); }
	inline ID3D12Device5* GetRaytracingDevice() const { return m_pRaytracingDevice.Get(); }
	ImGuiRenderer* GetImGui() const { return m_pImGuiRenderer.get(); }
	CommandQueue* GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
	CommandContext* AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);
	void FreeCommandList(CommandContext* pCommandContext);

	DynamicAllocationManager* GetAllocationManager() const { return m_pDynamicAllocationManager.get(); }
	OfflineDescriptorAllocator* GetDescriptorManager(D3D12_DESCRIPTOR_HEAP_TYPE type) const { return m_DescriptorHeaps[type].get(); }

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

	bool CheckTypedUAVSupport(DXGI_FORMAT format) const;
	bool UseRenderPasses() const;
	bool SupportsRaytracing() const { return m_RayTracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED; }
	bool SupportMeshShaders() const { return m_MeshShaderSupport != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED; }
	bool IsFenceComplete(uint64_t fenceValue);
	
	int32_t GetWindowWidth() const { return m_WindowWidth; }
	int32_t GetWindowHeight() const { return m_WindowHeight; }
	bool GetShaderModel(int& major, int& minor) const;
	ShaderManager* GetShaderManager() const { return m_pShaderManager.get(); }
	GraphicsTexture* GetDepthStencil() const { return m_pDepthStencil.get(); }
	GraphicsTexture* GetResolveDepthStencil() const { return m_pResolveDepthStencil.get(); }
	GraphicsTexture* GetCurrentRenderTarget() const { return m_SampleCount > 1 ? m_pMultiSampleRenderTarget.get() : m_pHDRRenderTarget.get(); }
	GraphicsTexture* GetCurrentBackbuffer() const { return m_Backbuffers[m_CurrentBackBufferIndex].get(); }
	uint32_t GetMultiSampleCount() const { return m_SampleCount; }

	ID3D12Resource* CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, D3D12_CLEAR_VALUE* pClearValue = nullptr);

	// constants
	static const uint32_t FRAME_COUNT = 3;
	static const uint32_t SHADOW_MAP_SIZE = 4096;
	static const DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;
	static const DXGI_FORMAT DEPTH_STENCIL_SHADOW_FORMAT = DXGI_FORMAT_D16_UNORM;
	static const DXGI_FORMAT RENDER_TARGET_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
	static const DXGI_FORMAT SWAPCHAIN_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

private:
	void BeginFrame();
	void EndFrame(uint64_t fenceValue);

	void InitD3D();
	void InitializePipelines();
	void InitializeAssets(CommandContext& context);
	void CreateSwapchain();

	void UpdateImGui();

	void GenerateAccelerationStructure(Mesh* pMesh, CommandContext& context);

	ComPtr<IDXGIFactory7> m_pFactory;
	ComPtr<IDXGISwapChain3> m_pSwapchain;
	ComPtr<ID3D12Device> m_pDevice;
	ComPtr<ID3D12Device5> m_pRaytracingDevice;
	ComPtr<ID3D12Fence> m_pDeviceRemovalFence;
	HANDLE m_DeviceRemovedEvent{0};

	std::unique_ptr<ShaderManager> m_pShaderManager;

	int m_Frame{ 0 };
	std::array<float, 180> m_FrameTimes{};

	std::array<std::unique_ptr<GraphicsTexture>, FRAME_COUNT> m_Backbuffers;
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

	std::array<std::unique_ptr<OfflineDescriptorAllocator>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_DescriptorHeaps;
	std::unique_ptr<DynamicAllocationManager> m_pDynamicAllocationManager;

	std::array<std::unique_ptr<CommandQueue>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandQueues;
	std::array<std::vector<std::unique_ptr<CommandContext>>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandListPool;
	std::array<std::queue<CommandContext*>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_FreeCommandContexts;
	std::vector<ComPtr<ID3D12CommandList>> m_CommandLists;
	std::mutex m_ContextAllocationMutex;

	std::unique_ptr<ClusteredForward> m_pClusteredForward;
	std::unique_ptr<TiledForward> m_pTiledForward;
	std::unique_ptr<ImGuiRenderer> m_pImGuiRenderer;
	std::unique_ptr<RTAO> m_pRTAO;
	std::unique_ptr<SSAO> m_pSSAO;
	std::unique_ptr<RTReflections> m_pRTReflections;

	std::unique_ptr<Camera> m_pCamera;
	HWND m_pWindow{};

	D3D12_RENDER_PASS_TIER m_RenderPassTier{ D3D12_RENDER_PASS_TIER_0 };
	D3D12_RAYTRACING_TIER m_RayTracingTier{ D3D12_RAYTRACING_TIER_NOT_SUPPORTED };
	uint8_t m_ShaderModelMajor{0};
	uint8_t m_ShaderModelMinor{0};
	D3D12_MESH_SHADER_TIER m_MeshShaderSupport{ D3D12_MESH_SHADER_TIER_NOT_SUPPORTED };
	D3D12_SAMPLER_FEEDBACK_TIER m_SamplerFeedbackSupport{ D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED };
	D3D12_VARIABLE_SHADING_RATE_TIER m_VSRTier{ D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED };
	int m_VSRTileSize{-1};

	int m_SampleCount{ 1 };

	uint32_t m_WindowWidth;
	uint32_t m_WindowHeight;
	std::unique_ptr<Buffer> m_pScreenshotBuffer;
	int32_t m_ScreenshotDelay{ -1 };
	int32_t m_ScreenshotRowPitch{ 0 };

	uint32_t m_CurrentBackBufferIndex{ 0 };
	//std::array<UINT64, FRAME_COUNT> m_FenceValues{};

	RenderPath m_RenderPath = RenderPath::Clustered;
	ShowGraph m_ShowGraph = ShowGraph::AO;

	std::unique_ptr<Mesh> m_pMesh;
	std::unique_ptr<Buffer> m_pBLAS;
	std::unique_ptr<Buffer> m_pTLAS;
	std::unique_ptr<Buffer> m_pBLASScratch;
	std::unique_ptr<Buffer> m_pTLASScratch;

	// shadow mapping
	std::unique_ptr<RootSignature> m_pShadowRS;
	std::unique_ptr<PipelineState> m_pShadowOpaquePSO;
	std::unique_ptr<PipelineState> m_pShadowAlphaMaskPSO;

	// depth prepass
	std::unique_ptr<RootSignature> m_pDepthPrepassRS;
	std::unique_ptr<PipelineState> m_pDepthPrepassOpaquePSO;
	std::unique_ptr<PipelineState> m_pDepthPrepassAlphaMaskPSO;

	// MSAA depth resolve
	std::unique_ptr<RootSignature> m_pResolveDepthRS;
	std::unique_ptr<PipelineState> m_pResolveDepthPSO;

	// Tonemapping
	std::unique_ptr<GraphicsTexture> m_pDownscaledColor;
	std::unique_ptr<RootSignature> m_pLuminanceHistogramRS;
	std::unique_ptr<PipelineState> m_pLuminanceHistogramPSO;
	std::unique_ptr<RootSignature> m_pAverageLuminanceRS;
	std::unique_ptr<PipelineState> m_pAverageLuminancePSO;
	std::unique_ptr<RootSignature> m_pToneMapRS;
	std::unique_ptr<PipelineState> m_pToneMapPSO;
	std::unique_ptr<RootSignature> m_pDrawHistogramRS;
	std::unique_ptr<PipelineState> m_pDrawHistogramPSO;
	std::unique_ptr<Buffer> m_pLuminanceHistogram;
	std::unique_ptr<Buffer> m_pAverageLuminance;

	// SSAO
	std::unique_ptr<GraphicsTexture> m_pAmbientOcclusion;

	// mip generation
	std::unique_ptr<RootSignature> m_pGenerateMipsRS;
	std::unique_ptr<PipelineState> m_pGenerateMipsPSO;

	// depth reduction
	std::unique_ptr<PipelineState> m_pPrepareReduceDepthPSO;
	std::unique_ptr<PipelineState> m_pPrepareReduceDepthMsaaPSO;
	std::unique_ptr<PipelineState> m_pReduceDepthPSO;
	std::unique_ptr<RootSignature> m_pReduceDepthRS;
	std::vector<std::unique_ptr<GraphicsTexture>> m_ReductionTargets;
	std::vector<std::unique_ptr<Buffer>> m_ReductionReadbackTargets;

	// TAA
	std::unique_ptr<RootSignature> m_pTemporalResolveRS;
	std::unique_ptr<PipelineState> m_pTemporalResolvePSO;

	// Camera motion
	std::unique_ptr<PipelineState> m_pCameraMotionPSO;
	std::unique_ptr<RootSignature> m_pCameraMotionRS;

	// sky
	std::unique_ptr<RootSignature> m_pSkyboxRS;
	std::unique_ptr<PipelineState> m_pSkyboxPSO;

	// light data
	std::vector<Light> m_Lights;
	std::unique_ptr<Buffer> m_pLightBuffer;

	GraphicsTexture* m_pVisualizeTexture{ nullptr };

	// particles
	std::unique_ptr<GpuParticles> m_pGpuParticles;
	// clouds
	std::unique_ptr<class Clouds> m_pClouds;

	SceneData m_SceneData;

	bool m_CapturePix{ false };
};
