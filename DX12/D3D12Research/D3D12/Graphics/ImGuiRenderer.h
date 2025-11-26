#pragma once

#include "Graphics/Core/DescriptorHandle.h"

class CommandContext;
class GraphicsDevice;
class RootSignature;
class PipelineState;
class GraphicsTexture;
class RGGraph;
class ShaderManager;
struct SceneData;

class ImGuiRenderer
{
public:
	ImGuiRenderer(GraphicsDevice* pParent);
	~ImGuiRenderer();

	void NewFrame(uint32_t width, uint32_t height);
	void Render(RGGraph& graph, const SceneData& sceneData, GraphicsTexture* pRenderTarget);

private:
	static const uint32_t m_WindowWidth{ 1240 };
	static const uint32_t m_WindowHeight{ 720 };

	void CreatePipeline(GraphicsDevice* pParent);
	void InitializeImGui(GraphicsDevice* pParent);
		
	PipelineState* m_pPipelineStateObject{nullptr};
	std::unique_ptr<RootSignature> m_pRootSignature;
	std::unique_ptr<GraphicsTexture> m_pFontTexture;
};
