#pragma once

class RootSignature;
class StateObject;
class GraphicsDevice;
class GraphicsTexture;
class RGGraph;
struct SceneView;

class PathTracing
{
public:
    PathTracing(GraphicsDevice* pGraphicsDevice);
    ~PathTracing();

    void Render(RGGraph& graph, const SceneView& sceneData);
    void OnResize(uint32_t width, uint32_t height);
    void Reset();
	bool IsSupported();

private:
    GraphicsDevice* m_pGraphicsDevice;
    std::unique_ptr<RootSignature> m_pRS;
    StateObject* m_pSO{ nullptr };

	std::unique_ptr<GraphicsTexture> m_pAccumulationTexture;
	DelegateHandle m_OnShaderCompiledHandle;
	int m_NumAccumulatedFrames{1};
};
