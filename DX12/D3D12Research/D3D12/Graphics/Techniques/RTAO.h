#pragma once

class Mesh;
class GraphicsDevice;
class ShaderManager;
class RootSignature;
class GraphicsTexture;
class Camera;
class CommandContext;
class Buffer;
class RGGraph;
class StateObject;
struct SceneView;

class RTAO
{
public:
    RTAO(GraphicsDevice* pGraphicsDevice);

    void Execute(RGGraph& graph, GraphicsTexture* pTarget, const SceneView& sceneData);

private:
    void SetupPipelines();

    GraphicsDevice* m_pGraphicsDevice;
    std::unique_ptr<RootSignature> m_pGlobalRS;
    StateObject* m_pStateObject{nullptr};
};
