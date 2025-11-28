#include "stdafx.h"
#include "TiledForward.h"
#include "Scene/Camera.h"
#include "Graphics/Mesh.h"
#include "Graphics/Profiler.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/ResourceViews.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "DemoApp.h"
#include "Core/ConsoleVariables.h"

static constexpr int MAX_LIGHT_DENSITY = 72000;
static constexpr int FORWARD_PLUS_BLOCK_SIZE = 16;

namespace Tweakables
{
    extern ConsoleVariable<int> g_SsrSamples;
}

TiledForward::TiledForward(GraphicsDevice* pGraphicsDevice)
{
    SetupResources(pGraphicsDevice);
    SetupPipelines(pGraphicsDevice);
}

void TiledForward::OnSwapChainCreated(int windowWidth, int windowHeight)
{
    int frustumCountX = Math::DivideAndRoundUp(windowWidth, FORWARD_PLUS_BLOCK_SIZE);
    int frustumCountY = Math::DivideAndRoundUp(windowHeight, FORWARD_PLUS_BLOCK_SIZE);
    m_pLightGridOpaque->Create(TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));
	m_pLightGridTransparent->Create(TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));

}

void TiledForward::Execute(RGGraph& graph, const SceneData& inputResource)
{
    RG_GRAPH_SCOPE("Tiled Lighting", graph);

	// 3. light culling
    //  - compute shader to buckets lights in tiles depending on their screen position
    //  - require a depth buffer
    //  - outputs a: - Texture2D containing a count and an offset of lights per tile.
    //								- uint[] index buffer to indicate what are visible in each tile
    RGPassBuilder lightCulling = graph.AddPass("Tiled Light Culling");
    lightCulling.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
            context.InsertResourceBarrier(inputResource.pResolvedDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pLightIndexCounter.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.InsertResourceBarrier(m_pLightGridTransparent.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.InsertResourceBarrier(m_pLightIndexListBufferTransparent.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            context.ClearUavUInt(m_pLightIndexCounter.get(), m_pLightIndexCounterRawUAV);

            context.SetPipelineState(m_pComputeLightCullPipeline);
            context.SetComputeRootSignature(m_pComputeLightCullRS.get());

            struct ShaderParameter
            {
                Matrix CameraView;
                Matrix ProjectionInverse;
                IntVector3 NumThreadGroups;
                int padding0;
                Vector2 ScreenDimensionsInv;
                uint32_t LightCount{0};
            } data{};

			data.CameraView = inputResource.pCamera->GetView();
			data.NumThreadGroups.x = Math::DivideAndRoundUp(inputResource.pResolvedDepth->GetWidth(), FORWARD_PLUS_BLOCK_SIZE);
			data.NumThreadGroups.y = Math::DivideAndRoundUp(inputResource.pResolvedDepth->GetHeight(), FORWARD_PLUS_BLOCK_SIZE);
			data.NumThreadGroups.z = 1;
			data.ScreenDimensionsInv = Vector2(1.0f / inputResource.pResolvedDepth->GetWidth(), 1.0f / inputResource.pResolvedDepth->GetHeight());
			data.LightCount = (uint32_t)inputResource.pLightBuffer->GetNumElements();
			data.ProjectionInverse = inputResource.pCamera->GetProjectionInverse();

			context.SetComputeDynamicConstantBufferView(0, data);
            context.BindResource(1, 0, m_pLightIndexCounter->GetUAV());
            context.BindResource(1, 1, m_pLightIndexListBufferOpaque->GetUAV());
            context.BindResource(1, 2, m_pLightGridOpaque->GetUAV());
            context.BindResource(1, 3, m_pLightIndexListBufferTransparent->GetUAV());
            context.BindResource(1, 4, m_pLightGridTransparent->GetUAV());
            context.BindResource(2, 0, inputResource.pResolvedDepth->GetSRV());
            context.BindResource(2, 1, inputResource.pLightBuffer->GetSRV());

            context.Dispatch(data.NumThreadGroups);
        });

    // 5. base pass
    //  - render the scene using the shadow mapping result and the light culling buffers
    RGPassBuilder basePass = graph.AddPass("Base Pass");
    basePass.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
            struct PerFrameData
            {
                Matrix View;
                Matrix Projection;
                Matrix ProjectionInverse;
                Matrix ViewProjection;
                Matrix ReprojectionMatrix;
                Vector4 ViewPosition;
                Vector2 InvScreenDimensions;
                float NearZ;
                float FarZ;
                int FrameIndex;
				int SsrSamples;
				int LightCount;
				int padd;
            } frameData{};

            struct PerObjectData
            {
                Matrix World;
                MaterialData Material;
				uint32_t VertexBuffer;
            } objectData{};

            frameData.View = inputResource.pCamera->GetView();
            frameData.Projection = inputResource.pCamera->GetProjection();
            frameData.ProjectionInverse = inputResource.pCamera->GetProjectionInverse();
            frameData.ViewProjection = inputResource.pCamera->GetViewProjection();
            frameData.ViewPosition = Vector4(inputResource.pCamera->GetPosition());
            frameData.InvScreenDimensions = Vector2(1.0f / inputResource.pRenderTarget->GetWidth(), 1.0f / inputResource.pRenderTarget->GetHeight());
            frameData.NearZ = inputResource.pCamera->GetNear();
            frameData.FarZ = inputResource.pCamera->GetFar();
            frameData.FrameIndex = inputResource.FrameIndex;
            frameData.SsrSamples = Tweakables::g_SsrSamples;
			frameData.LightCount = inputResource.pLightBuffer->GetNumElements();

            Matrix reprojectionMatrix = inputResource.pCamera->GetViewProjection().Invert() * inputResource.pCamera->GetPreviousViewProjection();
            // tranform from uv to clip space: texcoord * 2 - 1
            Matrix premult = {
                2, 0, 0, 0,
                0, -2, 0, 0,
                0, 0, 1, 0,
                -1, 1, 0, 1
            };

            // transform from clip space to uv space: texcoord * 0.5 + 0.5
            Matrix postmult = {
                0.5f, 0, 0, 0,
                0, -0.5f, 0, 0,
                0, 0, 1, 0,
                0.5f, 0.5f, 0, 1
            };
            frameData.ReprojectionMatrix = premult * reprojectionMatrix * postmult;

            context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pLightGridTransparent.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pLightIndexListBufferTransparent.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(inputResource.pDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
			context.InsertResourceBarrier(inputResource.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
			context.InsertResourceBarrier(inputResource.pNormals, D3D12_RESOURCE_STATE_RENDER_TARGET);
            context.InsertResourceBarrier(inputResource.pAO, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(inputResource.pPreviousColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(inputResource.pResolvedDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

			RenderPassInfo renderPass;
			renderPass.DepthStencilTarget.Access = RenderPassAccess::Load_Store;
			renderPass.DepthStencilTarget.StencilAccess = RenderPassAccess::DontCare_DontCare;
			renderPass.DepthStencilTarget.Target = inputResource.pDepthBuffer;
            renderPass.DepthStencilTarget.Write = false;
			renderPass.RenderTargetCount = 2;
			renderPass.RenderTargets[0].Access = RenderPassAccess::DontCare_Store;
			renderPass.RenderTargets[0].Target = inputResource.pRenderTarget;
			renderPass.RenderTargets[1].Access = RenderPassAccess::DontCare_Resolve;
			renderPass.RenderTargets[1].Target = inputResource.pNormals;
			renderPass.RenderTargets[1].ResolveTarget = inputResource.pResolvedNormals;
			context.BeginRenderPass(renderPass);

            context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context.SetGraphicsRootSignature(m_pDiffuseRS.get());

            context.SetGraphicsDynamicConstantBufferView(1, frameData);
            context.SetGraphicsDynamicConstantBufferView(2, *inputResource.pShadowData);

            context.BindResourceTable(3, inputResource.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Graphics);
            context.BindResource(4, 2, inputResource.pLightBuffer->GetSRV());
            context.BindResource(4, 3, inputResource.pAO->GetSRV());
			context.BindResource(4, 4, inputResource.pResolvedDepth->GetSRV());
			context.BindResource(4, 5, inputResource.pPreviousColor->GetSRV());

            {
                GPU_PROFILE_SCOPE("Opaque", &context);
                context.SetPipelineState(m_pDiffusePSO);
                        
                context.BindResource(4, 0, m_pLightGridOpaque->GetSRV());
                context.BindResource(4, 1, m_pLightIndexListBufferOpaque->GetSRV());

				DrawScene(context, inputResource, Batch::Blending::Opaque | Batch::Blending::AlphaMask);
            }

            {
                GPU_PROFILE_SCOPE("Transparent", &context);
                context.SetPipelineState(m_pDiffuseAlphaPSO);
                        
                context.BindResource(4, 0, m_pLightGridTransparent->GetSRV());
                context.BindResource(4, 1, m_pLightIndexListBufferTransparent->GetSRV());

				DrawScene(context, inputResource, Batch::Blending::AlphaBlend);
            }

            context.EndRenderPass();
        });
}

void TiledForward::VisualizeLightDensity(RGGraph& graph, GraphicsDevice* pGraphicsDevice, Camera& camera, GraphicsTexture* pTarget, GraphicsTexture* pDepth)
{
    if (!m_pVisualizeIntermediateTexture)
    {
        m_pVisualizeIntermediateTexture = std::make_unique<GraphicsTexture>(pGraphicsDevice, "LightDensity Debug Texture");
    }

    if (m_pVisualizeIntermediateTexture->GetDesc() != pTarget->GetDesc())
    {
        m_pVisualizeIntermediateTexture->Create(pTarget->GetDesc());
    }

    float nearZ = camera.GetNear();
    float farZ = camera.GetFar();

    RGPassBuilder visualizePass = graph.AddPass("Visualize Light Density");
    visualizePass.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
            struct Data
            {
				Matrix ProjectionInverse;
				IntVector3 ClusterDimensions;
				float padding;
				IntVector2 ClusterSize;
				Vector2 LightGridParams;
				float Near;
				float Far;
                float FoV;
            } constantData{};

            constantData.ProjectionInverse = camera.GetProjectionInverse();
            constantData.Near = nearZ;
            constantData.Far = farZ;
            constantData.FoV = camera.GetFoV();

            context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pVisualizeIntermediateTexture.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            context.SetPipelineState(m_pVisualizeLightsPSO);
            context.SetComputeRootSignature(m_pVisualizeLightsRS.get());

            context.SetComputeDynamicConstantBufferView(0, constantData);

            context.BindResource(1, 0, pTarget->GetSRV());
            context.BindResource(1, 1, pDepth->GetSRV());
            context.BindResource(1, 2, m_pLightGridOpaque->GetSRV());
            
            context.BindResource(2, 0, m_pVisualizeIntermediateTexture->GetUAV());

            context.Dispatch(Math::DivideAndRoundUp(pTarget->GetWidth(), 16), Math::DivideAndRoundUp(pTarget->GetHeight(), 16));
            context.InsertUavBarrier();

            context.CopyTexture(m_pVisualizeIntermediateTexture.get(), pTarget);
        });
}

void TiledForward::SetupResources(GraphicsDevice* pGraphicsDevice)
{
    m_pLightGridOpaque = std::make_unique<GraphicsTexture>(pGraphicsDevice, "Light Grid Opaque");
    m_pLightGridTransparent = std::make_unique<GraphicsTexture>(pGraphicsDevice, "Light Grid Transparent");
}

void TiledForward::SetupPipelines(GraphicsDevice* pGraphicsDevice)
{
    {
        Shader* pComputeShader = pGraphicsDevice->GetShader("LightCulling.hlsl", ShaderType::Compute, "CSMain");

        m_pComputeLightCullRS = std::make_unique<RootSignature>(pGraphicsDevice);
        m_pComputeLightCullRS->FinalizeFromShader("Tiled Light Culling", pComputeShader);

        PipelineStateInitializer psoDesc;
        psoDesc.SetRootSignature(m_pComputeLightCullRS->GetRootSignature());
        psoDesc.SetComputeShader(pComputeShader);
        psoDesc.SetName("Tiled Light Culling PSO");
        m_pComputeLightCullPipeline = pGraphicsDevice->CreatePipeline(psoDesc);

        m_pLightIndexCounter = std::make_unique<Buffer>(pGraphicsDevice, "Light Index Counter");
        m_pLightIndexCounter->Create(BufferDesc::CreateStructured(2, sizeof(uint32_t)));
        m_pLightIndexCounter->CreateUAV(&m_pLightIndexCounterRawUAV, BufferUAVDesc::CreateRaw());
        m_pLightIndexListBufferOpaque = std::make_unique<Buffer>(pGraphicsDevice, "Light List Opaque");
        m_pLightIndexListBufferOpaque->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32_t)));
        m_pLightIndexListBufferTransparent = std::make_unique<Buffer>(pGraphicsDevice, "Light List Transparent");
        m_pLightIndexListBufferTransparent->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32_t)));
    }

    // PBR diffuse passes
	{
		// shaders
		Shader* pVertexShader = pGraphicsDevice->GetShader("Diffuse.hlsl", ShaderType::Vertex, "VSMain", { "TILED_FORWARD" });
		Shader* pPixelShader = pGraphicsDevice->GetShader("Diffuse.hlsl", ShaderType::Pixel, "PSMain", { "TILED_FORWARD" });

		// root signature
		m_pDiffuseRS = std::make_unique<RootSignature>(pGraphicsDevice);
		m_pDiffuseRS->FinalizeFromShader("Diffuse PBR RS", pVertexShader);

		{
			DXGI_FORMAT formats[] = { GraphicsDevice::RENDER_TARGET_FORMAT, DXGI_FORMAT_R16G16B16A16_FLOAT };
			// opaque
            PipelineStateInitializer psoDesc;
			psoDesc.SetRootSignature(m_pDiffuseRS->GetRootSignature());
			psoDesc.SetVertexShader(pVertexShader);
			psoDesc.SetPixelShader(pPixelShader);
			psoDesc.SetRenderTargetFormats(formats, std::size(formats), GraphicsDevice::DEPTH_STENCIL_FORMAT, pGraphicsDevice->GetMultiSampleCount());
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			psoDesc.SetDepthWrite(false);
			psoDesc.SetName("Diffuse PBR Pipeline");
			m_pDiffusePSO = pGraphicsDevice->CreatePipeline(psoDesc);
		
		    // transparent
            psoDesc.SetBlendMode(BlendMode::Alpha, false);
            psoDesc.SetName("Diffuse PBR (Alpha) Pipeline");
			m_pDiffuseAlphaPSO = pGraphicsDevice->CreatePipeline(psoDesc);
		}
	}

    {
        Shader* pComputeShader = pGraphicsDevice->GetShader("VisualizeLightCount.hlsl", ShaderType::Compute, "DebugLightDensityCS", { "TILED_FORWARD" });

        m_pVisualizeLightsRS = std::make_unique<RootSignature>(pGraphicsDevice);
        m_pVisualizeLightsRS->FinalizeFromShader("Visualize Light Density RS", pComputeShader);

        PipelineStateInitializer psoDesc;
        psoDesc.SetRootSignature(m_pVisualizeLightsRS->GetRootSignature());
        psoDesc.SetComputeShader(pComputeShader);
        psoDesc.SetName("Visualize Light Density PSO");
        m_pVisualizeLightsPSO = pGraphicsDevice->CreatePipeline(psoDesc);
    }
}
