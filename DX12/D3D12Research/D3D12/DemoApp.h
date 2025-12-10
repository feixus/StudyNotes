#pragma once
#include "Graphics/Light.h"
#include "Core/Bitfield.h"
#include "Graphics/Core/DescriptorHandle.h"
#include "Graphics/Core/Graphics.h"
#include "ShaderCommon.h"

class CommandContext;
class ImGuiRenderer;
class RootSignature;
class PipelineState;
class GraphicsTexture;
class Mesh;
class Buffer;
struct SubMesh;
class ClusteredForward;
class Camera;
class UnorderedAccessView;
class TiledForward;
class RTAO;
class SSAO;
class GpuParticles;
class RTReflections;
class SwapChain;
class PathTracing;

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

struct Batch
{
	enum class Blending
	{
		Opaque = 1,
		AlphaMask = 2,
		AlphaBlend = 4,
	};
	int Index{0};
	Blending BlendMode{ Blending::Opaque };
	const SubMesh* pMesh{};
	Matrix WorldMatrix;
	BoundingBox LocalBounds;
	BoundingBox Bounds;
	float Radius{0};
	int Material{0};
};
DECLARE_BITMASK_TYPE(Batch::Blending);

using VisibilityMask = BitField<2048>;

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
	Buffer* pMaterialBuffer{ nullptr };
	Buffer* pMeshBuffer{ nullptr };
	Camera* pCamera{ nullptr };
	ShaderInterop::ShadowData* pShadowData{ nullptr };
	int SceneTLAS{ 0 };
	int FrameIndex{ 0 };
	VisibilityMask VisibilityMask;
};

enum class RenderPath
{
	Tiled,
	Clustered,
	PathTracing,
	MAX
};

void DrawScene(CommandContext& context, const SceneData& scene, const VisibilityMask& visibility, Batch::Blending blendModes);
void DrawScene(CommandContext& context, const SceneData& scene, Batch::Blending blendModes);

class DemoApp
{
public:
	DemoApp(HWND hWnd, const IntVector2& windowRect, int sampleCount = 1);
	~DemoApp();

	void Update();
	void OnResize(int width, int height);

	ImGuiRenderer* GetImGui() const { return m_pImGuiRenderer.get(); }
	GraphicsTexture* GetDefaultTexture(DefaultTexture type) const { return m_DefaultTextures[(int)type].get(); }
	GraphicsTexture* GetDepthStencil() const { return m_pDepthStencil.get(); }
	GraphicsTexture* GetResolveDepthStencil() const { return m_pResolveDepthStencil.get(); }
	GraphicsTexture* GetCurrentRenderTarget() const { return m_SampleCount > 1 ? m_pMultiSampleRenderTarget.get() : m_pHDRRenderTarget.get(); }
	GraphicsTexture* GetCurrentBackbuffer() const { return m_pSwapChain->GetBackBuffer(); }
	uint32_t GetMultiSampleCount() const { return m_SampleCount; }
	GraphicsDevice* GetDevice() const { return m_pDevice.get(); }

private:
	void InitializePipelines();
	void InitializeAssets(CommandContext& context);
	void SetupScene(CommandContext& context);

	void UpdateImGui();
	void UpdateTLAS(CommandContext& context);

	uint32_t m_WindowWidth;
	uint32_t m_WindowHeight;

	std::unique_ptr<GraphicsDevice> m_pDevice;
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

	std::unique_ptr<TiledForward> m_pTiledForward;
	std::unique_ptr<ClusteredForward> m_pClusteredForward;
	std::unique_ptr<PathTracing> m_pPathTracing;

	std::unique_ptr<ImGuiRenderer> m_pImGuiRenderer;
	std::unique_ptr<RTAO> m_pRTAO;
	std::unique_ptr<SSAO> m_pSSAO;
	std::unique_ptr<RTReflections> m_pRTReflections;

	int32_t m_ScreenshotDelay{-1};
	int32_t m_ScreenshotRowPitch{0};
	std::unique_ptr<Buffer> m_pScreenshotBuffer;

	RenderPath m_RenderPath = RenderPath::PathTracing;

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
	std::unique_ptr<GraphicsTexture> m_pDebugHistogramTexture;

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
	std::unique_ptr<Buffer> m_pMaterialBuffer;
	std::unique_ptr<Buffer> m_pMeshBuffer;
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
