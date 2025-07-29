#pragma once

#include "DescriptorHandle.h"

class CommandContext;
class Graphics;
class RootSignature;
class PipelineState;
class GraphicsTexture;
class RGGraph;

DECLARE_MULTICAST_DELEGATE(ImGuiCallback);

class ImGuiRenderer
{
public:
	ImGuiRenderer(Graphics* pDevice);
	~ImGuiRenderer();

	void NewFrame();
	void Render(RGGraph& graph, GraphicsTexture* pRenderTarget);
	void Update();
	DelegateHandle AddUpdateCallback(ImGuiCallbackDelegate&& callback);
	void RemoveUpdateCallback(DelegateHandle handle);

private:
	static const uint32_t m_WindowWidth{ 1240 };
	static const uint32_t m_WindowHeight{ 720 };

	void CreatePipeline();
	void InitializeImGui();
		
	ImGuiCallback m_UpdateCallback;

	Graphics* m_pGraphics;
	std::unique_ptr<PipelineState> m_pPipelineStateObject;
	std::unique_ptr<RootSignature> m_pRootSignature;
	std::unique_ptr<GraphicsTexture> m_pFontTexture;
};
