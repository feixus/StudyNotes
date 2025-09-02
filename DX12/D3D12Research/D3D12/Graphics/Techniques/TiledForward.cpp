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

static constexpr int MAX_LIGHT_DENSITY = 72000;
static constexpr int FORWARD_PLUS_BLOCK_SIZE = 16;

bool g_VisualizeLightDensity = false;

TiledForward::TiledForward(Graphics* pGraphics)
{
    SetupResources(pGraphics);
    SetupPipelines(pGraphics);
}

void TiledForward::OnSwapchainCreated(int windowWidth, int windowHeight)
{
    int frustumCountX = Math::DivideAndRoundUp(windowWidth, FORWARD_PLUS_BLOCK_SIZE);
    int frustumCountY = Math::DivideAndRoundUp(windowHeight, FORWARD_PLUS_BLOCK_SIZE);
    m_pLightGridOpaque->Create(TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));
	m_pLightGridTransparent->Create(TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));

}

void TiledForward::Execute(RGGraph& graph, const TiledForwardInputResource& inputResource)
{
    RG_GRAPH_SCOPE("Tiled Lighting", graph);

	// 3. light culling
    //  - compute shader to buckets lights in tiles depending on their screen position
    //  - require a depth buffer
    //  - outputs a: - Texture2D containing a count and an offset of lights per tile.
    //								- uint[] index buffer to indicate what are visible in each tile
    RGPassBuilder lightCulling = graph.AddPass("Tiled Light Culling");
    lightCulling.Read(inputResource.ResolvedDepthBuffer);
    lightCulling.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
            GraphicsTexture* pDepthTexture = passResources.GetTexture(inputResource.ResolvedDepthBuffer);
            context.InsertResourceBarrier(pDepthTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pLightIndexCounter.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.InsertResourceBarrier(m_pLightGridTransparent.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.InsertResourceBarrier(m_pLightIndexListBufferTransparent.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            context.ClearUavUInt(m_pLightIndexCounter.get(), m_pLightIndexCounterRawUAV);

            context.SetPipelineState(m_pComputeLightCullPipeline.get());
            context.SetComputeRootSignature(m_pComputeLightCullRS.get());

            struct ShaderParameter
            {
                Matrix CameraView;
                Matrix ProjectionInverse;
                IntVector3 NumThreadGroups;
                int padding0;
                Vector2 ScreenDimensionsInv;
                uint32_t LightCount{0};
            } Data{};

            Data.CameraView = inputResource.pCamera->GetView();
            Data.NumThreadGroups.x = Math::DivideAndRoundUp(pDepthTexture->GetWidth(), FORWARD_PLUS_BLOCK_SIZE);
            Data.NumThreadGroups.y = Math::DivideAndRoundUp(pDepthTexture->GetHeight(), FORWARD_PLUS_BLOCK_SIZE);
            Data.NumThreadGroups.z = 1;
            Data.ScreenDimensionsInv = Vector2(1.0f / pDepthTexture->GetWidth(), 1.0f / pDepthTexture->GetHeight());
            Data.LightCount = (uint32_t)inputResource.pLightBuffer->GetDesc().ElementCount;
            Data.ProjectionInverse = inputResource.pCamera->GetProjectionInverse();

            context.SetComputeDynamicConstantBufferView(0, &Data, sizeof(ShaderParameter));
            context.SetDynamicDescriptor(1, 0, m_pLightIndexCounter->GetUAV());
            context.SetDynamicDescriptor(1, 1, m_pLightIndexListBufferOpaque->GetUAV());
            context.SetDynamicDescriptor(1, 2, m_pLightGridOpaque->GetUAV());
            context.SetDynamicDescriptor(1, 3, m_pLightIndexListBufferTransparent->GetUAV());
            context.SetDynamicDescriptor(1, 4, m_pLightGridTransparent->GetUAV());
            context.SetDynamicDescriptor(2, 0, pDepthTexture->GetSRV());
            context.SetDynamicDescriptor(2, 1, inputResource.pLightBuffer->GetSRV());

            context.Dispatch(Data.NumThreadGroups);
        });

    // 5. base pass
    //  - render the scene using the shadow mapping result and the light culling buffers
    RGPassBuilder basePass = graph.AddPass("Base Pass");
    basePass.Read(inputResource.DepthBuffer);
    basePass.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
            struct PerFrameData
            {
                Matrix View;
                Matrix ViewInverse;
                Matrix Projection;
                Vector2 ScreenDimensions;
                float NearZ;
                float FarZ;
            } frameData{};

            struct PerObjectData
            {
                Matrix World;
                Matrix WorldViewProjection;
            } objectData{};

            frameData.View = inputResource.pCamera->GetView();
            frameData.ViewInverse = inputResource.pCamera->GetViewInverse();
            frameData.Projection = inputResource.pCamera->GetProjection();
            frameData.ScreenDimensions = Vector2((float)inputResource.pRenderTarget->GetWidth(), (float)inputResource.pRenderTarget->GetHeight());
            frameData.NearZ = inputResource.pCamera->GetNear();
            frameData.FarZ = inputResource.pCamera->GetFar();

            GraphicsTexture* pDepthTexture = passResources.GetTexture(inputResource.DepthBuffer);

            context.SetViewport(FloatRect(0, 0, (float)pDepthTexture->GetWidth(), (float)pDepthTexture->GetHeight()));

            context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pLightGridTransparent.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pLightIndexListBufferTransparent.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(inputResource.pShadowMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(inputResource.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
            context.InsertResourceBarrier(pDepthTexture, D3D12_RESOURCE_STATE_DEPTH_READ);
            context.InsertResourceBarrier(inputResource.pAO, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

            context.BeginRenderPass(RenderPassInfo(inputResource.pRenderTarget, RenderPassAccess::Clear_Store, pDepthTexture, RenderPassAccess::Load_DontCare));

            context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context.SetGraphicsRootSignature(m_pDiffuseRS.get());

            context.SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
            context.SetDynamicConstantBufferView(2, inputResource.pShadowData, sizeof(ShadowData));

            context.SetDynamicDescriptor(4, 0, inputResource.pShadowMap->GetSRV());
            context.SetDynamicDescriptor(4, 3, inputResource.pLightBuffer->GetSRV());
            context.SetDynamicDescriptor(4, 4, inputResource.pAO->GetSRV());

            auto setMaterialDescriptors = [](CommandContext& context, const Batch& b)
            {
                D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
                    b.pMaterial->pDiffuseTexture->GetSRV(),
                    b.pMaterial->pNormalTexture->GetSRV(),
                    b.pMaterial->pSpecularTexture->GetSRV()
                };
                context.SetDynamicDescriptors(3, 0, srvs, std::size(srvs));
            };

            {
                GPU_PROFILE_SCOPE("Opaque", &context);
                context.SetPipelineState(g_VisualizeLightDensity ? m_pVisualizeDensityPSO.get() : m_pDiffusePSO.get());
                        
                context.SetDynamicDescriptor(4, 1, m_pLightGridOpaque->GetSRV());
                context.SetDynamicDescriptor(4, 2, m_pLightIndexListBufferOpaque->GetSRV());

                for (const Batch& b : *inputResource.pOpaqueBatches)
                {
                    objectData.World = b.WorldMatrix;
                    objectData.WorldViewProjection = b.WorldMatrix * inputResource.pCamera->GetViewProjection();

                    setMaterialDescriptors(context, b);
                    context.SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));

                    b.pMesh->Draw(&context);
                }
            }

            {
                GPU_PROFILE_SCOPE("Transparent", &context);
                context.SetPipelineState(g_VisualizeLightDensity ? m_pVisualizeDensityPSO.get() : m_pDiffuseAlphaPSO.get());
                        
                context.SetDynamicDescriptor(4, 1, m_pLightGridTransparent->GetSRV());
                context.SetDynamicDescriptor(4, 2, m_pLightIndexListBufferTransparent->GetSRV());

                for (const Batch& b : *inputResource.pTransparentBatches)
                {
                    objectData.World = b.WorldMatrix;
                    objectData.WorldViewProjection = b.WorldMatrix * inputResource.pCamera->GetViewProjection();

                    setMaterialDescriptors(context, b);
                    context.SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));

                    b.pMesh->Draw(&context);
                }
            }

            context.EndRenderPass();
        });
}

void TiledForward::SetupResources(Graphics* pGraphics)
{
    m_pLightGridOpaque = std::make_unique<GraphicsTexture>(pGraphics, "Light Grid Opaque");
    m_pLightGridTransparent = std::make_unique<GraphicsTexture>(pGraphics, "Light Grid Transparent");
}

void TiledForward::SetupPipelines(Graphics* pGraphics)
{
    Shader computeShader("LightCulling.hlsl", ShaderType::Compute, "CSMain");

    m_pComputeLightCullRS = std::make_unique<RootSignature>();
    m_pComputeLightCullRS->FinalizeFromShader("Tiled Light Culling", computeShader, pGraphics->GetDevice());

    m_pComputeLightCullPipeline = std::make_unique<PipelineState>();
    m_pComputeLightCullPipeline->SetRootSignature(m_pComputeLightCullRS->GetRootSignature());
    m_pComputeLightCullPipeline->SetComputeShader(computeShader);
    m_pComputeLightCullPipeline->Finalize("Tiled Light Culling PSO", pGraphics->GetDevice());

    m_pLightIndexCounter = std::make_unique<Buffer>(pGraphics, "Light Index Counter");
    m_pLightIndexCounter->Create(BufferDesc::CreateStructured(2, sizeof(uint32_t)));
    m_pLightIndexCounter->CreateUAV(&m_pLightIndexCounterRawUAV, BufferUAVDesc::CreateRaw());
    m_pLightIndexListBufferOpaque = std::make_unique<Buffer>(pGraphics, "Light List Opaque");
    m_pLightIndexListBufferOpaque->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32_t)));
    m_pLightIndexListBufferTransparent = std::make_unique<Buffer>(pGraphics, "Light List Transparent");
    m_pLightIndexListBufferTransparent->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32_t)));

    CD3DX12_INPUT_ELEMENT_DESC inputElements[] = {
            CD3DX12_INPUT_ELEMENT_DESC{ "POSITION", DXGI_FORMAT_R32G32B32_FLOAT },
            CD3DX12_INPUT_ELEMENT_DESC{ "TEXCOORD", DXGI_FORMAT_R32G32_FLOAT },
            CD3DX12_INPUT_ELEMENT_DESC{ "NORMAL", DXGI_FORMAT_R32G32B32_FLOAT },
            CD3DX12_INPUT_ELEMENT_DESC{ "TANGENT", DXGI_FORMAT_R32G32B32_FLOAT },
            CD3DX12_INPUT_ELEMENT_DESC{ "TEXCOORD", DXGI_FORMAT_R32G32B32_FLOAT, 1 },
	};

    // PBR diffuse passes
	{
		// shaders
		Shader vertexShader("Diffuse.hlsl", ShaderType::Vertex, "VSMain", { "TILED_FORWARD" });
		Shader pixelShader("Diffuse.hlsl", ShaderType::Pixel, "PSMain", { "TILED_FORWARD" });
		Shader debugPixelShader("Diffuse.hlsl", ShaderType::Pixel, "DebugLightDensityPS", { "TILED_FORWARD" });

		// root signature
		m_pDiffuseRS = std::make_unique<RootSignature>();
		m_pDiffuseRS->FinalizeFromShader("Diffuse PBR RS", vertexShader, pGraphics->GetDevice());

		// opaque
		{
			m_pDiffusePSO = std::make_unique<PipelineState>();
			m_pDiffusePSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
			m_pDiffusePSO->SetRootSignature(m_pDiffuseRS->GetRootSignature());
			m_pDiffusePSO->SetVertexShader(vertexShader);
			m_pDiffusePSO->SetPixelShader(pixelShader);
			m_pDiffusePSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, pGraphics->GetMultiSampleCount());
			m_pDiffusePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pDiffusePSO->SetDepthWrite(false);
			m_pDiffusePSO->Finalize("Diffuse PBR Pipeline", pGraphics->GetDevice());
		
		    // transparent
			m_pDiffuseAlphaPSO = std::make_unique<PipelineState>(*m_pDiffusePSO.get());
            m_pDiffuseAlphaPSO->SetBlendMode(BlendMode::Alpha, false);
            m_pDiffuseAlphaPSO->Finalize("Diffuse PBR (Alpha) Pipeline", pGraphics->GetDevice());

            // debug light density
            m_pVisualizeDensityPSO = std::make_unique<PipelineState>(*m_pDiffusePSO.get());
            m_pVisualizeDensityPSO->SetPixelShader(debugPixelShader);
            m_pVisualizeDensityPSO->Finalize("Debug Light Density PSO", pGraphics->GetDevice());
		}
	}
}