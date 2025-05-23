#pragma once

#include "DescriptorHandle.h"

class GraphicsCommandContext;
class Graphics;
class RootSignature;
class GraphicsPipelineState;
class GraphicsTexture2D;

class ImGuiRenderer
{
public:
	ImGuiRenderer(Graphics* pDevice);
	~ImGuiRenderer();

	void NewFrame();
	void Render(GraphicsCommandContext& context);

private:
	static const uint32_t m_WindowWidth{ 1240 };
	static const uint32_t m_WindowHeight{ 720 };

	void CreatePipeline();
	void InitializeImGui();

	Graphics* m_pGraphics;
	std::unique_ptr<GraphicsPipelineState> m_pPipelineStateObject;
	std::unique_ptr<RootSignature> m_pRootSignature;
	std::unique_ptr<GraphicsTexture2D> m_pFontTexture;
};
