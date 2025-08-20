#pragma once

#include "Graphics/Core/DescriptorHandle.h"

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

	void NewFrame(uint32_t width, uint32_t height);
	void Render(RGGraph& graph, GraphicsTexture* pRenderTarget);
	void Update();
	DelegateHandle AddUpdateCallback(ImGuiCallbackDelegate&& callback);
	void RemoveUpdateCallback(DelegateHandle handle);

private:
	static const uint32_t m_WindowWidth{ 1240 };
	static const uint32_t m_WindowHeight{ 720 };

	void CreatePipeline(Graphics* pGraphics);
	void InitializeImGui(Graphics* pGraphics);
		
	ImGuiCallback m_UpdateCallback;

	std::unique_ptr<PipelineState> m_pPipelineStateObject;
	std::unique_ptr<RootSignature> m_pRootSignature;
	std::unique_ptr<GraphicsTexture> m_pFontTexture;
};
