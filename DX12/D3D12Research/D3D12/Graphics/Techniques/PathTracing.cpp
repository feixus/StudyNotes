#include "stdafx.h"
#include "PathTracing.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/Core/StateObject.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/ShaderBindingTable.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/SceneView.h"
#include "Scene/Camera.h"

PathTracing::PathTracing(GraphicsDevice* pGraphicsDevice) : m_pGraphicsDevice(pGraphicsDevice)
{
	if (!IsSupported())
	{
		return;
	}

    ShaderLibrary* pLibrary = pGraphicsDevice->GetLibrary("PathTracing.hlsl");

    m_pRS = std::make_unique<RootSignature>(m_pGraphicsDevice);
    m_pRS->FinalizeFromShader("PathTracing RS", pLibrary);

    StateObjectInitializer desc{};
    desc.Name = "Path Tracing";
    desc.MaxRecursion = 1;
    desc.MaxPayloadSize = 13 * sizeof(float);
    desc.MaxAttributeSize = 2 * sizeof(float);
    desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    desc.AddLibrary(pLibrary);
    desc.AddHitGroup("PrimaryHG", "PrimaryCHS", "PrimaryAHS");
	desc.AddMissShader("PrimaryMS");
	desc.AddMissShader("ShadowMS");

    desc.pGlobalRootSignature = m_pRS.get();
    m_pSO = m_pGraphicsDevice->CreateStateObject(desc);

	m_OnShaderCompiledHandle = pGraphicsDevice->GetShaderManager()->OnLibraryRecompiledEvent().AddLambda([this](ShaderLibrary*, ShaderLibrary*)
	{
		Reset();
	});
}
    
PathTracing::~PathTracing()
{
	if (m_OnShaderCompiledHandle.IsValid())
	{
		m_pGraphicsDevice->GetShaderManager()->OnLibraryRecompiledEvent().Remove(m_OnShaderCompiledHandle);
	}
}

void PathTracing::Render(RGGraph& graph, const SceneView& sceneData)
{
	if (!IsSupported())
	{
		return;
	}

	static int32_t numBounces = 3;

	if (ImGui::Begin("Parameters"))
    {
        if (ImGui::CollapsingHeader("Path Tracing"))
        {
            if (ImGui::SliderInt("Bounces", &numBounces, 1, 15))
            {
                Reset();
            }
            if (ImGui::Button("Reset"))
            {
                Reset();
            }
        }
    }
    ImGui::End();

	if (sceneData.pCamera->GetPreviousViewProjection() != sceneData.pCamera->GetViewProjection())
	{
		Reset();
	}

	m_NumAccumulatedFrames++;

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
            uint32_t TLASIndex;
            uint32_t FrameIndex;
			uint32_t NumBounces;
            uint32_t AccumulateFrames;
        } params{};

        params.View = sceneData.pCamera->GetView();
        params.ViewInv = sceneData.pCamera->GetViewInverse();
        params.Projection = sceneData.pCamera->GetProjection();
        params.ProjectionInv = sceneData.pCamera->GetProjectionInverse();
        params.NumLights = sceneData.pLightBuffer->GetNumElements();
        params.TLASIndex = sceneData.SceneTLAS;
        params.FrameIndex = sceneData.FrameIndex;
		params.NumBounces = numBounces;
        params.AccumulateFrames = m_NumAccumulatedFrames;

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
            sceneData.pMeshInstanceBuffer->GetSRV()->GetDescriptor(),
        };

		const D3D12_CPU_DESCRIPTOR_HANDLE uavs[] =
		{
			sceneData.pRenderTarget->GetUAV()->GetDescriptor(),
			m_pAccumulationTexture->GetUAV()->GetDescriptor(),
		};

        context.SetComputeDynamicConstantBufferView(0, params);
        context.SetComputeDynamicConstantBufferView(1, *sceneData.pShadowData);
        context.BindResources(2, 0, uavs, std::size(uavs));
        context.BindResources(3, 0, srvs, std::size(srvs));
        context.BindResourceTable(4, sceneData.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Compute);

        context.DispatchRays(bindingTable, sceneData.pRenderTarget->GetWidth(), sceneData.pRenderTarget->GetHeight());
    });
}
    
void PathTracing::OnResize(uint32_t width, uint32_t height)
{
	if (!IsSupported())
	{
		return;
	}
	m_pAccumulationTexture = m_pGraphicsDevice->CreateTexture(TextureDesc::Create2D(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, TextureFlag::UnorderedAccess), "Accumulation Target");
}

void PathTracing::Reset()
{
	m_NumAccumulatedFrames = 0;
}

bool PathTracing::IsSupported()
{
	return m_pGraphicsDevice->GetCapabilities().SupportsRaytracing();
}
