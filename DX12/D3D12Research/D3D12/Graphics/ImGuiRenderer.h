#pragma once

#include "Graphics/Core/DescriptorHandle.h"

class GraphicsDevice;
class GraphicsTexture;
class RGGraph;
struct SceneView;

using WindowHandle = HWND;
namespace ImGui
{
	void Image(GraphicsTexture* pTexture, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
	void ImageAutoSize(GraphicsTexture* textureId, const ImVec2& imageDimensions);
}

class ImGuiRenderer
{
public:
	ImGuiRenderer(GraphicsDevice* pGraphicsDevice, WindowHandle window, uint32_t numBufferedFrames);
	~ImGuiRenderer();

	void NewFrame(uint32_t width, uint32_t height);
	void Render(RGGraph& graph, const SceneView& sceneData, GraphicsTexture* pRenderTarget);
};
