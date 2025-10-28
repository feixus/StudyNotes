#include "stdafx.h"
#include "RTReflections.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Core/RaytracingCommon.h"
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
            context.SetPipelineState(m_pRtSO.Get());

            struct Parameters
            {
                Matrix ViewInverse;
                Matrix ProjectionInverse;
                uint32_t NumLights;
                float ViewPixelSpreadAngle;
            } parameters;

            parameters.ViewInverse = sceneData.pCamera->GetViewInverse();
            parameters.ProjectionInverse = sceneData.pCamera->GetProjectionInverse();
            parameters.NumLights = sceneData.pLightBuffer->GetDesc().ElementCount;
            parameters.ViewPixelSpreadAngle = atanf(2.0f * tanf(sceneData.pCamera->GetFoV() * 0.5f) / (float)m_pSceneColor->GetHeight());

            ShaderBindingTable bindingTable(m_pRtSO.Get());
            bindingTable.BindRayGenShader("RayGen");
			bindingTable.BindMissShader("Miss", 0);
			bindingTable.BindMissShader("ShadowMiss", 1);

            for (int i = 0; i < sceneData.pMesh->GetMeshCount(); ++i)
            {
                SubMesh* pMesh = sceneData.pMesh->GetMesh(i);

                auto it = std::find_if(sceneData.Batches.begin(), sceneData.Batches.end(), [pMesh](const Batch& batch) { return batch.pMesh == pMesh; });

				struct HitData
				{
					MaterialData Material;
					uint32_t VertexBufferOffset;
					uint32_t IndexBufferOffset;
				} hitData;
				hitData.Material = it->Material;
				hitData.VertexBufferOffset = (uint32_t)(pMesh->GetVertexBuffer().Location - sceneData.pMesh->GetData()->GetGpuHandle());
				hitData.IndexBufferOffset = (uint32_t)(pMesh->GetIndexBuffer().Location - sceneData.pMesh->GetData()->GetGpuHandle());

                DynamicAllocation allocation = context.AllocateTransientMemory(sizeof(HitData));
                memcpy(allocation.pMappedMemory, &hitData, sizeof(HitData));
                bindingTable.BindHitGroup("ReflectionHitGroup", { allocation.GpuHandle});
            }

            const D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
                sceneData.pLightBuffer->GetSRV()->GetDescriptor(),
                sceneData.pLightBuffer->GetSRV()->GetDescriptor(),  // dummy
                sceneData.pResolvedDepth->GetSRV(),
                m_pSceneColor->GetSRV(),
                sceneData.pResolvedNormals->GetSRV(),
                sceneData.pMesh->GetData()->GetSRV()->GetDescriptor(),
            };

            context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));
            context.SetDynamicDescriptor(1, 0, sceneData.pResolvedTarget->GetUAV());
            context.SetDynamicDescriptors(2, 0, srvs, (int)std::size(srvs));
            context.SetDynamicDescriptor(3, 0, sceneData.pTLAS->GetSRV()->GetDescriptor());
            context.SetDynamicDescriptors(4, 0, sceneData.MaterialTextures.data(), (int)sceneData.MaterialTextures.size());

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
        ShaderLibrary shaderLibrary("RTReflections.hlsl");

        m_pHitRS = std::make_unique<RootSignature>(pGraphics);
        m_pHitRS->SetConstantBufferView(0, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pHitRS->Finalize("Hit RS", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

        m_pGlobalRS = std::make_unique<RootSignature>(pGraphics);
        m_pGlobalRS->FinalizeFromShader("Global RS", shaderLibrary);

        CD3DX12_STATE_OBJECT_HELPER stateObjectDesc;
        const char* pLibraryExports[] = { "RayGen", "ClosestHit", "Miss", "ShadowMiss" };
        stateObjectDesc.AddLibrary(CD3DX12_SHADER_BYTECODE(shaderLibrary.GetByteCode(), shaderLibrary.GetByteCodeSize()), pLibraryExports, (uint32_t)std::size(pLibraryExports));
		stateObjectDesc.AddHitGroup("ReflectionHitGroup", "ClosestHit");
		stateObjectDesc.BindLocalRootSignature("ReflectionHitGroup", m_pHitRS->GetRootSignature());
        stateObjectDesc.SetRaytracingShaderConfig(5 * sizeof(float), 2 * sizeof(float));
        stateObjectDesc.SetRaytracingPipelineConfig(2);
        stateObjectDesc.SetGlobalRootSignature(m_pGlobalRS->GetRootSignature());
    
        D3D12_STATE_OBJECT_DESC desc = stateObjectDesc.Desc();
        VERIFY_HR_EX(pGraphics->GetRaytracingDevice()->CreateStateObject(&desc, IID_PPV_ARGS(m_pRtSO.GetAddressOf())), pGraphics->GetRaytracingDevice());
    }
}
