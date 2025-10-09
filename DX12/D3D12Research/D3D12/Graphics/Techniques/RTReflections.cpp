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

GraphicsTexture* RTReflections::Execute(RGGraph& graph, const SceneData& sceneData)
{
    RGPassBuilder rt = graph.AddPass("Raytracing Reflections");
    rt.Bind([=](CommandContext& context, const RGPassResource& passResource)
        {
            context.InsertResourceBarrier(sceneData.pResolvedDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pReflections.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            context.SetComputeRootSignature(m_pGlobalRS.get());
            context.SetPipelineState(m_pRtSO.Get());

            struct Parameters
            {
                Matrix ViewInverse;
                Matrix ViewProjectionInverse;
            } parameters;

            parameters.ViewInverse = sceneData.pCamera->GetViewInverse();
            parameters.ViewProjectionInverse = sceneData.pCamera->GetProjectionInverse() * sceneData.pCamera->GetViewInverse();

            ShaderBindingTable bindingTable(m_pRtSO.Get());
            bindingTable.AddRayGenEntry("RayGen", {});
			bindingTable.AddMissEntry("Miss", {});
			bindingTable.AddMissEntry("ShadowMiss", {});

            for (int i = 0; i < sceneData.pMesh->GetMeshCount(); ++i)
            {
                SubMesh* pMesh = sceneData.pMesh->GetMesh(i);
                if (sceneData.pMesh->GetMaterial(pMesh->GetMaterialId()).IsTransparent)
                {
                    continue;
                }

                auto it = std::find_if(sceneData.OpaqueBatches.begin(), sceneData.OpaqueBatches.end(), [pMesh](const Batch& batch) { return batch.pMesh == pMesh; });

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
                bindingTable.AddHitGroupEntry("HitGroup", { allocation.GpuHandle});
				bindingTable.AddHitGroupEntry("ShadowHitGroup", {});
            }

            context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));
            context.SetDynamicDescriptor(1, 0, m_pReflections->GetUAV());
            context.SetDynamicDescriptor(2, 0, sceneData.pTLAS->GetSRV());
			context.SetDynamicDescriptor(2, 1, sceneData.pResolvedDepth->GetSRV());
			context.SetDynamicDescriptor(2, 2, sceneData.pLightBuffer->GetSRV());
			context.SetDynamicDescriptor(2, 3, sceneData.pMesh->GetData()->GetSRV());
            context.SetDynamicDescriptors(3, 0, sceneData.MaterialTextures.data(), (int)sceneData.MaterialTextures.size());

            context.DispatchRays(bindingTable, (uint32_t)m_pReflections->GetWidth(), (uint32_t)m_pReflections->GetHeight());
        });

    return m_pReflections.get();
}

void RTReflections::SetupResources(Graphics* pGraphics)
{
    m_pReflections = std::make_unique<GraphicsTexture>(pGraphics, "RTReflections Test Output");
    m_pReflections->Create(TextureDesc::Create2D(1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));
}

void RTReflections::SetupPipelines(Graphics* pGraphics)
{
    // raytracing pipeline
    {
        m_pRayGenRS = std::make_unique<RootSignature>(pGraphics);
        m_pRayGenRS->Finalize("Ray Gen RS", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

        m_pHitRS = std::make_unique<RootSignature>(pGraphics);
        m_pHitRS->SetConstantBufferView(0, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pHitRS->SetShaderResourceView(1, 100, D3D12_SHADER_VISIBILITY_ALL);
        m_pHitRS->SetShaderResourceView(2, 101, D3D12_SHADER_VISIBILITY_ALL);
        m_pHitRS->Finalize("Hit RS", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

        m_pMissRS = std::make_unique<RootSignature>(pGraphics);
        m_pMissRS->Finalize("Miss RS", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

        m_pGlobalRS = std::make_unique<RootSignature>(pGraphics);
        m_pGlobalRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        m_pGlobalRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pGlobalRS->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, D3D12_SHADER_VISIBILITY_ALL);
        m_pGlobalRS->SetDescriptorTableSimple(3, 200, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 128, D3D12_SHADER_VISIBILITY_ALL);
        m_pGlobalRS->AddStaticSampler(0, CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT), D3D12_SHADER_VISIBILITY_ALL);
        m_pGlobalRS->Finalize("Dummy Global RS", D3D12_ROOT_SIGNATURE_FLAG_NONE);

        ShaderLibrary shaderLibrary("RTReflections.hlsl");

        CD3DX12_STATE_OBJECT_HELPER stateObjectDesc;
        stateObjectDesc.AddLibrary(CD3DX12_SHADER_BYTECODE(shaderLibrary.GetByteCode(), shaderLibrary.GetByteCodeSize()),
								   {"RayGen", "ClosestHit", "Miss", "ShadowClosestHit", "ShadowMiss"});
		stateObjectDesc.AddHitGroup("HitGroup", "ClosestHit");
		stateObjectDesc.AddHitGroup("ShadowHitGroup", "ShadowClosestHit");
		stateObjectDesc.BindLocalRootSignature("RayGen", m_pRayGenRS->GetRootSignature());
		stateObjectDesc.BindLocalRootSignature("Miss", m_pMissRS->GetRootSignature());
		stateObjectDesc.BindLocalRootSignature("ShadowMiss", m_pMissRS->GetRootSignature());
		stateObjectDesc.BindLocalRootSignature("HitGroup", m_pHitRS->GetRootSignature());
		stateObjectDesc.BindLocalRootSignature("ShadowHitGroup", m_pMissRS->GetRootSignature());
        stateObjectDesc.SetRaytracingShaderConfig(3 * sizeof(float), 2 * sizeof(float));
        stateObjectDesc.SetRaytracingPipelineConfig(2);
        stateObjectDesc.SetGlobalRootSignature(m_pGlobalRS->GetRootSignature());
        D3D12_STATE_OBJECT_DESC desc = stateObjectDesc.Desc();
        VERIFY_HR_EX(pGraphics->GetRaytracingDevice()->CreateStateObject(&desc, IID_PPV_ARGS(m_pRtSO.GetAddressOf())), pGraphics->GetRaytracingDevice());
    }
}
