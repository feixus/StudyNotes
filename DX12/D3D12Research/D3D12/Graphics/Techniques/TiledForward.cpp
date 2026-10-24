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
#include "Graphics/SceneView.h"
#include "Core/ConsoleVariables.h"

static constexpr int MAX_LIGHT_DENSITY = 72000;
static constexpr int FORWARD_PLUS_BLOCK_SIZE = 16;

namespace Tweakables
{
    extern ConsoleVariable<int> g_SsrSamples;
}

TiledForward::TiledForward(GraphicsDevice* pGraphicsDevice) : m_pGraphicsDevice(pGraphicsDevice)
{
    SetupPipelines();
}

void TiledForward::OnResize(int windowWidth, int windowHeight)
{
    int frustumCountX = Math::DivideAndRoundUp(windowWidth, FORWARD_PLUS_BLOCK_SIZE);
    int frustumCountY = Math::DivideAndRoundUp(windowHeight, FORWARD_PLUS_BLOCK_SIZE);

    m_pLightGridOpaque = m_pGraphicsDevice->CreateTexture(TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource), "Light Grid - Opaque");
    m_pLightGridTransparent = m_pGraphicsDevice->CreateTexture(TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource), "Light Grid - Transparent");
}

void TiledForward::Execute(RGGraph& graph, const SceneView& inputResource)
{
    // why the sky is not refresh???
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

            BindViewParameters(0, context, inputResource);

            D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
                m_pLightIndexCounter->GetUAV()->GetDescriptor(),
                m_pLightIndexListBufferOpaque->GetUAV()->GetDescriptor(),
                m_pLightGridOpaque->GetUAV()->GetDescriptor(),
                m_pLightIndexListBufferTransparent->GetUAV()->GetDescriptor(),
                m_pLightGridTransparent->GetUAV()->GetDescriptor()
            };

            D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
                inputResource.pResolvedDepth->GetSRV()->GetDescriptor(),
                inputResource.pLightBuffer->GetSRV()->GetDescriptor()
            };

            context.BindResources(1, 0, uavs, ARRAYSIZE(uavs));
            context.BindResources(2, 0, srvs, ARRAYSIZE(srvs));

            context.Dispatch(ComputeUtils::GetNumThreadGroups(
                inputResource.pResolvedDepth->GetWidth(), FORWARD_PLUS_BLOCK_SIZE,
                inputResource.pResolvedDepth->GetHeight(), FORWARD_PLUS_BLOCK_SIZE));
        });

    // 5. base pass
    //  - render the scene using the shadow mapping result and the light culling buffers
    RGPassBuilder basePass = graph.AddPass("Base Pass");
    basePass.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
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
			renderPass.RenderTargets[1].Access = RenderPassAccess::Clear_Resolve;
			renderPass.RenderTargets[1].Target = inputResource.pNormals;
			renderPass.RenderTargets[1].ResolveTarget = inputResource.pResolvedNormals;
			context.BeginRenderPass(renderPass);

            context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context.SetGraphicsRootSignature(m_pDiffuseRS.get());

            BindViewParameters(2, context, inputResource);

            {
                GPU_PROFILE_SCOPE("Opaque", &context);

				D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = 
                {
                    inputResource.pLightBuffer->GetSRV()->GetDescriptor(),
                    m_pLightGridOpaque->GetSRV()->GetDescriptor(),
                    m_pLightIndexListBufferOpaque->GetSRV()->GetDescriptor(),
                    inputResource.pLightBuffer->GetSRV()->GetDescriptor(),
                    inputResource.pAO->GetSRV()->GetDescriptor(),
                    inputResource.pResolvedDepth->GetSRV()->GetDescriptor(),
                    inputResource.pPreviousColor->GetSRV()->GetDescriptor(),
				};
				context.BindResources(3, 0, srvs, std::size(srvs));
                        
                context.SetPipelineState(m_pDiffusePSO);
				DrawScene(context, inputResource, Batch::Blending::Opaque);

				context.SetPipelineState(m_pDiffuseMaskedPSO);
				DrawScene(context, inputResource, Batch::Blending::AlphaMask);
            }

            {
                GPU_PROFILE_SCOPE("Transparent", &context);

				D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = 
                {
                    inputResource.pLightBuffer->GetSRV()->GetDescriptor(),
                    m_pLightGridTransparent->GetSRV()->GetDescriptor(),
                    m_pLightIndexListBufferTransparent->GetSRV()->GetDescriptor(),
                    inputResource.pLightBuffer->GetSRV()->GetDescriptor(),
                    inputResource.pAO->GetSRV()->GetDescriptor(),
                    inputResource.pResolvedDepth->GetSRV()->GetDescriptor(),
                    inputResource.pPreviousColor->GetSRV()->GetDescriptor(),
				};
				context.BindResources(3, 0, srvs, std::size(srvs));
                        
                context.SetPipelineState(m_pDiffuseAlphaPSO);
				DrawScene(context, inputResource, Batch::Blending::AlphaBlend);
            }

            context.EndRenderPass();
        });
}

void TiledForward::VisualizeLightDensity(RGGraph& graph, const SceneView& inputResource, GraphicsTexture* pTarget, GraphicsTexture* pDepth)
{
    if (!m_pVisualizeIntermediateTexture || m_pVisualizeIntermediateTexture->GetDesc() != pTarget->GetDesc())
    {
        m_pVisualizeIntermediateTexture = m_pGraphicsDevice->CreateTexture(pTarget->GetDesc(), "LightDensity Debug Texture");
    }

    float sliceMagicA = 0;
    float sliceMagicB = 0;

    RGPassBuilder visualizePass = graph.AddPass("Visualize Light Density");
    visualizePass.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
            struct Data
            {
				IntVector2 ClusterDimensions;
				IntVector2 ClusterSize;
				float SliceMagicA;
				float SliceMagicB;
            } constantData{};

            // constantData.ClusterDimensions = IntVector2(m_lightGridWidth, m_lightGridHeight);
            // constantData.ClusterSize = IntVector2(FORWARD_PLUS_CLUSTER_SIZE, FORWARD_PLUS_CLUSTER_SIZE);
            constantData.SliceMagicA = sliceMagicA;
            constantData.SliceMagicB = sliceMagicB;

            context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pVisualizeIntermediateTexture.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            context.SetPipelineState(m_pVisualizeLightsPSO);
            context.SetComputeRootSignature(m_pVisualizeLightsRS.get());

            context.SetRootCBV(0, constantData);

            BindViewParameters(1, context, inputResource);

            context.BindResource(2, 0, pTarget->GetSRV());
            context.BindResource(2, 1, pDepth->GetSRV());
            context.BindResource(2, 2, m_pLightGridOpaque->GetSRV());
            
            context.BindResource(3, 0, m_pVisualizeIntermediateTexture->GetUAV());

            context.Dispatch(Math::DivideAndRoundUp(pTarget->GetWidth(), 16), Math::DivideAndRoundUp(pTarget->GetHeight(), 16));
            context.InsertUavBarrier();

            context.CopyTexture(m_pVisualizeIntermediateTexture.get(), pTarget);
        });
}

void TiledForward::SetupPipelines()
{
    {
        Shader* pComputeShader = m_pGraphicsDevice->GetShader("LightCulling.hlsl", ShaderType::Compute, "CSMain");

        m_pComputeLightCullRS = std::make_unique<RootSignature>(m_pGraphicsDevice);
        m_pComputeLightCullRS->FinalizeFromShader("Tiled Light Culling", pComputeShader);

        PipelineStateInitializer psoDesc;
        psoDesc.SetRootSignature(m_pComputeLightCullRS->GetRootSignature());
        psoDesc.SetComputeShader(pComputeShader);
        psoDesc.SetName("Tiled Light Culling PSO");
        m_pComputeLightCullPipeline = m_pGraphicsDevice->CreatePipeline(psoDesc);

        m_pLightIndexCounter = std::make_unique<GraphicsBuffer>(m_pGraphicsDevice, "Light Index Counter");
        m_pLightIndexCounter->Create(BufferDesc::CreateStructured(2, sizeof(uint32_t)));
        m_pLightIndexCounter->CreateUAV(&m_pLightIndexCounterRawUAV, BufferUAVDesc::CreateRaw());
        m_pLightIndexListBufferOpaque = std::make_unique<GraphicsBuffer>(m_pGraphicsDevice, "Light List Opaque");
        m_pLightIndexListBufferOpaque->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32_t)));
        m_pLightIndexListBufferTransparent = std::make_unique<GraphicsBuffer>(m_pGraphicsDevice, "Light List Transparent");
        m_pLightIndexListBufferTransparent->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32_t)));
    }

    // PBR diffuse passes
	{
		// shaders
		Shader* pVertexShader = m_pGraphicsDevice->GetShader("Diffuse.hlsl", ShaderType::Vertex, "VSMain", { "TILED_FORWARD" });
		Shader* pPixelShader = m_pGraphicsDevice->GetShader("Diffuse.hlsl", ShaderType::Pixel, "PSMain", { "TILED_FORWARD" });

		// root signature
		m_pDiffuseRS = std::make_unique<RootSignature>(m_pGraphicsDevice);
		m_pDiffuseRS->FinalizeFromShader("Diffuse PBR RS", pVertexShader);

		{
			DXGI_FORMAT formats[] = { GraphicsDevice::RENDER_TARGET_FORMAT, DXGI_FORMAT_R16G16B16A16_FLOAT };
			// opaque
            PipelineStateInitializer psoDesc;
			psoDesc.SetRootSignature(m_pDiffuseRS->GetRootSignature());
			psoDesc.SetVertexShader(pVertexShader);
			psoDesc.SetPixelShader(pPixelShader);
			psoDesc.SetRenderTargetFormats(formats, std::size(formats), GraphicsDevice::DEPTH_STENCIL_FORMAT, m_pGraphicsDevice->GetMultiSampleCount());
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			psoDesc.SetDepthWrite(false);
			psoDesc.SetName("Diffuse");
			m_pDiffusePSO = m_pGraphicsDevice->CreatePipeline(psoDesc);

			// alpha mask
			psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
			psoDesc.SetName("Diffuse Masked");
			m_pDiffuseMaskedPSO = m_pGraphicsDevice->CreatePipeline(psoDesc);
		
		    // transparent
            psoDesc.SetBlendMode(BlendMode::Alpha, false);
            psoDesc.SetName("Diffuse Alpha");
			m_pDiffuseAlphaPSO = m_pGraphicsDevice->CreatePipeline(psoDesc);
		}
	}

    {
        Shader* pComputeShader = m_pGraphicsDevice->GetShader("VisualizeLightCount.hlsl", ShaderType::Compute, "DebugLightDensityCS", { "TILED_FORWARD" });

        m_pVisualizeLightsRS = std::make_unique<RootSignature>(m_pGraphicsDevice);
        m_pVisualizeLightsRS->FinalizeFromShader("Visualize Light Density RS", pComputeShader);

        PipelineStateInitializer psoDesc;
        psoDesc.SetRootSignature(m_pVisualizeLightsRS->GetRootSignature());
        psoDesc.SetComputeShader(pComputeShader);
        psoDesc.SetName("Visualize Light Density PSO");
        m_pVisualizeLightsPSO = m_pGraphicsDevice->CreatePipeline(psoDesc);
    }
}
