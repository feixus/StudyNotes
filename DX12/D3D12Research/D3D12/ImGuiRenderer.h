#pragma once

#include "DescriptorHandle.h"

class CommandContext;
class Graphics;
class RootSignature;
class PipelineState;
class GraphicsTexture;
class ImGuiRenderer
{
public:
	ImGuiRenderer(Graphics* pDevice);
	~ImGuiRenderer();

	void NewFrame();
	void Render(CommandContext& context);

private:
	static const uint32_t m_WindowWidth{ 1240 };
	static const uint32_t m_WindowHeight{ 720 };

	void CreatePipeline();
	void InitializeImGui();

	Graphics* m_pGraphics;
	std::unique_ptr<PipelineState> m_pPipelineStateObject;
	std::unique_ptr<RootSignature> m_pRootSignature;
	std::unique_ptr<GraphicsTexture> m_pFontTexture;
	DescriptorHandle m_FontTextureHandle;
};
