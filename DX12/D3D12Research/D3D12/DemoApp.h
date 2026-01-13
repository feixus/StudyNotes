#pragma once
#include "Graphics/Light.h"
#include "Core/Bitfield.h"
#include "Graphics/Core/DescriptorHandle.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/SceneView.h"
#include "ShaderCommon.h"

class CommandContext;
class ImGuiRenderer;
class RootSignature;
class PipelineState;
class GraphicsTexture;
class Mesh;
class Buffer;
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
struct SubMesh;
struct Material;

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

enum class RenderPath
{
	Tiled,
	Clustered,
	PathTracing,
	Visibility,
	MAX
};

class DemoApp
{
public:
	DemoApp(HWND hWnd, const IntVector2& windowRect, int sampleCount = 1);
	~DemoApp();

	void Update();
	void OnResize(int width, int height);

private:
	void InitializePipelines();
	void InitializeAssets(CommandContext& context);
	void SetupScene(CommandContext& context);

	void UpdateImGui();
	void UpdateTLAS(CommandContext& context);

	GraphicsTexture* GetDefaultTexture(DefaultTexture type) const { return m_DefaultTextures[(int)type].get(); }
	GraphicsTexture* GetDepthStencil() const { return m_pDepthStencil.get(); }
	GraphicsTexture* GetResolveDepthStencil() const { return m_pResolveDepthStencil.get(); }
	GraphicsTexture* GetCurrentRenderTarget() const { return m_SampleCount > 1 ? m_pMultiSampleRenderTarget.get() : m_pHDRRenderTarget.get(); }
	GraphicsTexture* GetCurrentBackbuffer() const { return m_pSwapChain->GetBackBuffer(); }

	uint32_t GetMultiSampleCount() const { return m_SampleCount; }
	GraphicsDevice* GetDevice() const { return m_pDevice.get(); }
	ImGuiRenderer* GetImGui() const { return m_pImGuiRenderer.get(); }

	uint32_t m_WindowWidth;
	uint32_t m_WindowHeight;

	std::unique_ptr<GraphicsDevice> m_pDevice;
	std::unique_ptr<SwapChain> m_pSwapChain;

	uint32_t m_Frame{0};
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

	struct ScreenshotRequest
	{
		uint64_t Fence;
		uint32_t Width;
		uint32_t Height;
		uint32_t RowPitch;
		Buffer* pBuffer;
	};
	std::queue<ScreenshotRequest> m_ScreenshotBuffers;
	int32_t m_ScreenshotRowPitch{0};

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
	std::unique_ptr<Buffer> m_pMeshInstanceBuffer;
	std::vector<Light> m_Lights;
	std::unique_ptr<Buffer> m_pLightBuffer;

	// particles
	std::unique_ptr<GpuParticles> m_pGpuParticles;

	// clouds
	std::unique_ptr<class Clouds> m_pClouds;

	// visibility buffer
	std::unique_ptr<RootSignature> m_pVisibilityRenderingRS;
	PipelineState* m_pVisibilityRenderingPSO{nullptr};
	std::unique_ptr<GraphicsTexture> m_pVisibilityTexture;
	std::unique_ptr<GraphicsTexture> m_pBarycentricsTexture;
	std::unique_ptr<RootSignature> m_pVisibilityShadingRS;
	PipelineState* m_pVisibilityShadingPSO;

	// CBT
	std::unique_ptr<RootSignature> m_pCBTRS;
	std::unique_ptr<Buffer> m_pCBTBuffer;
	std::unique_ptr<Buffer> m_pCBTIndirectArgs;
	PipelineState* m_pCBTIndirectArgsPSO{nullptr};
	PipelineState* m_pCBTSumReductionPSO{nullptr};
	PipelineState* m_pCBTUpdatePSO{nullptr};

	PipelineState* m_pCBTRenderPSO{nullptr};
	std::unique_ptr<GraphicsTexture> m_pCBTTargetTexture;

	SceneView m_SceneData;
	std::unique_ptr<Camera> m_pCamera;

	GraphicsTexture* m_pVisualizeTexture{nullptr};

	uint32_t m_SampleCount{1};
	bool m_CapturePix{false};
};
