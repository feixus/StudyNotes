#include "stdafx.h"
#include "RTReflections.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/StateObject.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Core/ShaderBindingTable.h"
#include "Graphics/Core/ResourceViews.h"
#include "Graphics/Mesh.h"
#include "Scene/Camera.h"

RTReflections::RTReflections(Graphics* pGraphics)
{
    if (pGraphics->SupportsRaytracing())
    {
        SetupResources(pGraphics);
        SetupPipelines(pGraphics);
    }
}

void RTReflections::Execute(RGGraph& graph, const SceneData& sceneData)
{
    RGPassBuilder rt = graph.AddPass("Raytracing Reflections");
    rt.Bind([=](CommandContext& context, const RGPassResource& passResource)
        {
            context.CopyTexture(sceneData.pResolvedTarget, m_pSceneColor.get());

            context.InsertResourceBarrier(sceneData.pResolvedDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(sceneData.pResolvedNormals, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pSceneColor.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(sceneData.pResolvedTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            context.SetComputeRootSignature(m_pGlobalRS.get());
            context.SetPipelineState(m_pRtSO);

            struct Parameters
            {
                Matrix ViewInverse;
                Matrix ProjectionInverse;
                uint32_t NumLights;
                float ViewPixelSpreadAngle;
            } parameters;

            parameters.ViewInverse = sceneData.pCamera->GetViewInverse();
            parameters.ProjectionInverse = sceneData.pCamera->GetProjectionInverse();
            parameters.NumLights = sceneData.pLightBuffer->GetNumElements();
            parameters.ViewPixelSpreadAngle = atanf(2.0f * tanf(sceneData.pCamera->GetFoV() * 0.5f) / (float)m_pSceneColor->GetHeight());

            ShaderBindingTable bindingTable(m_pRtSO);
            bindingTable.BindRayGenShader("RayGen");
			bindingTable.BindMissShader("Miss", 0);
			bindingTable.BindMissShader("ShadowMiss", 1);

            for (const Batch& b : sceneData.Batches)
            {
				struct HitData
				{
					MaterialData Material;
				} hitData;
				hitData.Material = b.Material;

                DynamicAllocation allocation = context.AllocateTransientMemory(sizeof(HitData));
                memcpy(allocation.pMappedMemory, &hitData, sizeof(HitData));

                std::vector<uint64_t> handles = {
                    allocation.GpuHandle,
                    b.pMesh->GetVertexBuffer().Location,
                    b.pMesh->GetIndexBuffer().Location,
                };

                bindingTable.BindHitGroup("ReflectionHitGroup", b.Index, handles);
            }

            const D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
                sceneData.pLightBuffer->GetSRV()->GetDescriptor(),
                sceneData.pLightBuffer->GetSRV()->GetDescriptor(),  // dummy
                sceneData.pResolvedDepth->GetSRV(),
                m_pSceneColor->GetSRV(),
                sceneData.pResolvedNormals->GetSRV(),
            };

            context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));
            context.BindResource(1, 0, sceneData.pResolvedTarget->GetUAV());
            context.BindResources(2, 0, srvs, (int)std::size(srvs));
            context.BindResource(3, 0, sceneData.pTLAS->GetSRV()->GetDescriptor());
            context.BindResourceTable(4, sceneData.GlobalSRVHeapHandle, CommandListContext::Compute);

            context.DispatchRays(bindingTable, sceneData.pResolvedTarget->GetWidth(), sceneData.pResolvedTarget->GetHeight());
        });
}

void RTReflections::OnResize(uint32_t width, uint32_t height)
{
	if (m_pSceneColor != nullptr)
	{
		m_pSceneColor->Create(TextureDesc::Create2D(width, height, Graphics::RENDER_TARGET_FORMAT, TextureFlag::ShaderResource, 1, 1));
	}
}

void RTReflections::SetupResources(Graphics* pGraphics)
{
    m_pSceneColor = std::make_unique<GraphicsTexture>(pGraphics, "RTReflections Test Output");
}

void RTReflections::SetupPipelines(Graphics* pGraphics)
{
    // raytracing pipeline
    {
        ShaderLibrary* pShaderLibrary = pGraphics->GetShaderManager()->GetLibrary("RTReflections.hlsl");

        m_pHitRS = std::make_unique<RootSignature>(pGraphics);
        m_pHitRS->SetConstantBufferView(0, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pHitRS->SetShaderResourceView(1, 0, D3D12_SHADER_VISIBILITY_ALL);
        m_pHitRS->SetShaderResourceView(2, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pHitRS->Finalize("Hit RS", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

        m_pGlobalRS = std::make_unique<RootSignature>(pGraphics);
        m_pGlobalRS->FinalizeFromShader("Global RS", pShaderLibrary);

        StateObjectInitializer stateDesc;
        stateDesc.Name = "RT Reflections";
        stateDesc.RayGenShader = "RayGen";
        stateDesc.AddLibrary(pShaderLibrary, { "RayGen", "ClosestHit", "Miss", "ShadowMiss" });
        stateDesc.AddHitGroup("ReflectionHitGroup", "ClosestHit", "", "", m_pHitRS.get());
        stateDesc.AddMissShader("Miss");
        stateDesc.AddMissShader("ShadowMiss");
        stateDesc.MaxPayloadSize = 5 * sizeof(float);
        stateDesc.MaxAttributeSize = 2 * sizeof(float);
        stateDesc.MaxRecursion = 2;
        stateDesc.pGlobalRootSignature = m_pGlobalRS.get();
        stateDesc.RayGenShader = "RayGen";
        m_pRtSO = pGraphics->CreateStateObject(stateDesc);
    }
}
