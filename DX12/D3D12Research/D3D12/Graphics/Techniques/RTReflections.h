#pragma once
class Mesh;
class GraphicsDevice;
class ShaderManager;
class RootSignature;
class GraphicsTexture;
class Camera;
class CommandContext;
class RGGraph;
class Buffer;
struct SceneData;
class StateObject;

class RTReflections
{
public:
    RTReflections(GraphicsDevice* pGraphicsDevice);

    void Execute(RGGraph& graph, const SceneData& sceneData);
    void OnResize(uint32_t width, uint32_t height);

private:
    void SetupPipelines();

    GraphicsDevice* m_pGraphicsDevice;
    
    StateObject* m_pRtSO{nullptr};
    std::unique_ptr<RootSignature> m_pGlobalRS;
    std::unique_ptr<GraphicsTexture> m_pSceneColor;
};
