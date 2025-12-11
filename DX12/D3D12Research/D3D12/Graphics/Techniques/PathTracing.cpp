#include "stdafx.h"
#include "PathTracing.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/Core/StateObject.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/ShaderBindingTable.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Scene/Camera.h"
#include "DemoApp.h"

PathTracing::PathTracing(GraphicsDevice* pGraphicsDevice) : m_pGraphicsDevice(pGraphicsDevice)
{
	if (!pGraphicsDevice->GetCapabilities().SupportsRaytracing())
	{
		return;
	}

    ShaderLibrary* pLibrary = pGraphicsDevice->GetLibrary("PathTracing.hlsl");

    m_pRS = std::make_unique<RootSignature>(m_pGraphicsDevice);
    m_pRS->FinalizeFromShader("PathTracing RS", pLibrary);

    StateObjectInitializer desc{};
    desc.Name = "Path Tracing";
    desc.MaxRecursion = 1;
    desc.MaxPayloadSize = 14 * sizeof(float);
    desc.MaxAttributeSize = 2 * sizeof(float);
    desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    desc.AddLibrary(pLibrary);
    desc.AddHitGroup("PrimaryHG", "PrimaryCHS", "PrimaryAHS");
    desc.AddMissShader("PrimaryMS");
    desc.AddMissShader("ShadowMS");

    desc.pGlobalRootSignature = m_pRS.get();
    m_pSO = m_pGraphicsDevice->CreateStateObject(desc);
}
    
PathTracing::~PathTracing()
{}

void PathTracing::Render(RGGraph& graph, const SceneData& sceneData)
{
    RGPassBuilder passBuilder = graph.AddPass("Path Tracing");
    passBuilder.Bind([=](CommandContext& context, const RGPassResource& resources)
    {
        context.InsertResourceBarrier(sceneData.pRenderTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        context.SetComputeRootSignature(m_pRS.get());
        context.SetPipelineState(m_pSO);

        struct Parameters
        {
            Matrix View;
            Matrix ViewInv;
            Matrix Projection;
            Matrix ProjectionInv;
            uint32_t NumLights;
            float ViewPixelSpreadAngle;
            uint32_t TLASIndex;
            uint32_t FrameIndex;
            uint32_t Accumulate;
        } params{};

        params.View = sceneData.pCamera->GetView();
        params.ViewInv = sceneData.pCamera->GetViewInverse();
        params.Projection = sceneData.pCamera->GetProjection();
        params.ProjectionInv = sceneData.pCamera->GetProjectionInverse();
        params.NumLights = sceneData.pLightBuffer->GetNumElements();
        params.ViewPixelSpreadAngle = atanf(2.0f * tanf(sceneData.pCamera->GetFoV() * 0.5f) / sceneData.pRenderTarget->GetHeight());
        params.TLASIndex = sceneData.SceneTLAS;
        params.FrameIndex = sceneData.FrameIndex;
        params.Accumulate = sceneData.pCamera->GetPreviousViewProjection() == sceneData.pCamera->GetViewProjection();

        ShaderBindingTable bindingTable(m_pSO);
        bindingTable.BindRayGenShader("RayGen");
        bindingTable.BindMissShader("PrimaryMS", 0);
        bindingTable.BindMissShader("ShadowMS", 1);
        bindingTable.BindHitGroup("PrimaryHG", 0);

        const D3D12_CPU_DESCRIPTOR_HANDLE srvs[] =
        {
            sceneData.pLightBuffer->GetSRV()->GetDescriptor(),
            sceneData.pLightBuffer->GetSRV()->GetDescriptor(),
            sceneData.pResolvedDepth->GetSRV()->GetDescriptor(),
            sceneData.pPreviousColor->GetSRV()->GetDescriptor(),
            sceneData.pResolvedNormals->GetSRV()->GetDescriptor(),
            sceneData.pMaterialBuffer->GetSRV()->GetDescriptor(),
            sceneData.pMeshBuffer->GetSRV()->GetDescriptor(),
        };

        context.SetComputeDynamicConstantBufferView(0, params);
        context.SetComputeDynamicConstantBufferView(1, *sceneData.pShadowData);
        context.BindResource(2, 0, sceneData.pRenderTarget->GetUAV());
        context.BindResources(3, 0, srvs, std::size(srvs));
        context.BindResourceTable(4, sceneData.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Compute);

        context.DispatchRays(bindingTable, sceneData.pRenderTarget->GetWidth(), sceneData.pRenderTarget->GetHeight());
    });
}
    
void PathTracing::OnResize(uint32_t width, uint32_t height)
{}
