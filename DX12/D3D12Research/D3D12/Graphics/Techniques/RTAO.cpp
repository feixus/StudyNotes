#include "stdafx.h"
#include "RTAO.h"
#include "Scene/Camera.h"
#include "Graphics/Mesh.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/StateObject.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/Core/ShaderBindingTable.h"
#include "Graphics/SceneView.h"

RTAO::RTAO(GraphicsDevice* pGraphicsDevice)
{
	if (!pGraphicsDevice->GetCapabilities().SupportsRaytracing())
	{
		return;
	}

    SetupPipelines(pGraphicsDevice);
}

void RTAO::Execute(RGGraph& graph, GraphicsTexture* pTarget, const SceneView& sceneData)
{
    static float g_AoPower = 3;
    static float g_AoRadius = 0.5f;
    static int g_AoSamples = 1;

	if (ImGui::Begin("Parameters"))
    {
        if (ImGui::CollapsingHeader("Ambient Occlusion"))
        {
            ImGui::SliderFloat("Power", &g_AoPower, 0, 10);
            ImGui::SliderFloat("Radius", &g_AoRadius, 0.1f, 5.0f);
            ImGui::SliderInt("Samples", &g_AoSamples, 1, 64);

        }
    }
    ImGui::End();

    RG_GRAPH_SCOPE("Ambient Occlusion", graph);

    RGPassBuilder raytracing = graph.AddPass("RTAO");
    raytracing.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
            context.InsertResourceBarrier(sceneData.pResolvedDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                
            context.SetComputeRootSignature(m_pGlobalRS.get());
			context.SetPipelineState(m_pStateObject);
                
            struct Parameters
            {
                Matrix ViewInverse;
                Matrix ProjectionInverse;
                Matrix ViewProjectionInverse;
                float Power;
                float Radius;
                uint32_t Samples;
                uint32_t TLASIndex;
                uint32_t FrameIndex;
            } parameters{};

			parameters.ViewInverse = sceneData.pCamera->GetViewInverse();
            parameters.ProjectionInverse = sceneData.pCamera->GetProjectionInverse();
            parameters.ViewProjectionInverse = sceneData.pCamera->GetViewProjectionInverse();
            parameters.Power = g_AoPower;
            parameters.Radius = g_AoRadius;
            parameters.Samples = g_AoSamples;
            parameters.TLASIndex = sceneData.SceneTLAS;
            parameters.FrameIndex = sceneData.FrameIndex;

            ShaderBindingTable bindingTable(m_pStateObject);
            bindingTable.BindRayGenShader("RayGen");
            bindingTable.BindMissShader("Miss", 0);

            context.SetComputeDynamicConstantBufferView(0, parameters);
			context.BindResource(1, 0, pTarget->GetUAV());
            context.BindResource(2, 0, sceneData.pResolvedDepth->GetSRV());
			context.BindResourceTable(3, sceneData.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Compute);
                
            context.DispatchRays(bindingTable, pTarget->GetWidth(), pTarget->GetHeight());
        });
}

void RTAO::SetupPipelines(GraphicsDevice* pGraphicsDevice)
{
    ShaderLibrary* pShaderLibrary = pGraphicsDevice->GetLibrary("RTAO.hlsl");

    m_pGlobalRS = std::make_unique<RootSignature>(pGraphicsDevice);
    m_pGlobalRS->FinalizeFromShader("Global RS", pShaderLibrary);

    StateObjectInitializer stateDesc;
    stateDesc.AddLibrary(pShaderLibrary, { "RayGen", "Miss" });
    stateDesc.Name = "RT AO";
    stateDesc.MaxPayloadSize = sizeof(float);
    stateDesc.MaxAttributeSize = 2 * sizeof(float);
    stateDesc.pGlobalRootSignature = m_pGlobalRS.get();
    stateDesc.RayGenShader = "RayGen";
    stateDesc.AddMissShader("Miss");
    m_pStateObject = pGraphicsDevice->CreateStateObject(stateDesc);
}
