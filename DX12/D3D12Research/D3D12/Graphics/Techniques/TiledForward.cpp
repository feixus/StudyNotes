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

namespace Tweakables
{
    extern int g_SsrSamples;
}

TiledForward::TiledForward(Graphics* pGraphics) : m_pGraphics(pGraphics)
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
            Data.NumThreadGroups.x = Math::DivideAndRoundUp(inputResource.pResolvedDepth->GetWidth(), FORWARD_PLUS_BLOCK_SIZE);
            Data.NumThreadGroups.y = Math::DivideAndRoundUp(inputResource.pResolvedDepth->GetHeight(), FORWARD_PLUS_BLOCK_SIZE);
            Data.NumThreadGroups.z = 1;
            Data.ScreenDimensionsInv = Vector2(1.0f / inputResource.pResolvedDepth->GetWidth(), 1.0f / inputResource.pResolvedDepth->GetHeight());
            Data.LightCount = (uint32_t)inputResource.pLightBuffer->GetDesc().ElementCount;
            Data.ProjectionInverse = inputResource.pCamera->GetProjectionInverse();

            context.SetComputeDynamicConstantBufferView(0, &Data, sizeof(ShaderParameter));
            context.SetDynamicDescriptor(1, 0, m_pLightIndexCounter->GetUAV());
            context.SetDynamicDescriptor(1, 1, m_pLightIndexListBufferOpaque->GetUAV());
            context.SetDynamicDescriptor(1, 2, m_pLightGridOpaque->GetUAV());
            context.SetDynamicDescriptor(1, 3, m_pLightIndexListBufferTransparent->GetUAV());
            context.SetDynamicDescriptor(1, 4, m_pLightGridTransparent->GetUAV());
            context.SetDynamicDescriptor(2, 0, inputResource.pResolvedDepth->GetSRV());
            context.SetDynamicDescriptor(2, 1, inputResource.pLightBuffer->GetSRV());

            context.Dispatch(Data.NumThreadGroups);
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
            } objectData{};

            frameData.View = inputResource.pCamera->GetView();
            frameData.Projection = inputResource.pCamera->GetProjection();
            frameData.ViewProjection = inputResource.pCamera->GetViewProjection();
            frameData.ViewPosition = Vector4(inputResource.pCamera->GetPosition());
            frameData.InvScreenDimensions = Vector2(1.0f / inputResource.pRenderTarget->GetWidth(), 1.0f / inputResource.pRenderTarget->GetHeight());
            frameData.NearZ = inputResource.pCamera->GetNear();
            frameData.FarZ = inputResource.pCamera->GetFar();
            frameData.FrameIndex = inputResource.FrameIndex;
            frameData.SsrSamples = Tweakables::g_SsrSamples;
			frameData.LightCount = inputResource.pLightBuffer->GetDesc().ElementCount;

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
            context.InsertResourceBarrier(inputResource.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
            context.InsertResourceBarrier(inputResource.pDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
            context.InsertResourceBarrier(inputResource.pAO, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(inputResource.pPreviousColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(inputResource.pResolvedDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

            context.BeginRenderPass(RenderPassInfo(inputResource.pRenderTarget, RenderPassAccess::Clear_Store, inputResource.pDepthBuffer, RenderPassAccess::Load_DontCare));

            context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context.SetGraphicsRootSignature(m_pDiffuseRS.get());

            context.SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
            context.SetDynamicConstantBufferView(2, inputResource.pShadowData, sizeof(ShadowData));

            context.SetDynamicDescriptors(3, 0, inputResource.MaterialTextures.data(), (int)inputResource.MaterialTextures.size());
            context.SetDynamicDescriptor(4, 2, inputResource.pLightBuffer->GetSRV());
            context.SetDynamicDescriptor(4, 3, inputResource.pAO->GetSRV());
			context.SetDynamicDescriptor(4, 4, inputResource.pResolvedDepth->GetSRV());
			context.SetDynamicDescriptor(4, 5, inputResource.pPreviousColor->GetSRV());

            int idx = 0;
            for (auto& pShadowMap : *inputResource.pShadowMaps)
            {
                context.SetDynamicDescriptor(5, idx++, pShadowMap->GetSRV());
            }

            if (inputResource.pTLAS)
            {
                context.GetCommandList()->SetGraphicsRootShaderResourceView(6, inputResource.pTLAS->GetGpuHandle());
            }

            auto DrawBatches = [&](Batch::Blending blendMode)
            {
                for (const Batch& b : inputResource.Batches)
                {
                    if (Any(b.BlendMode, blendMode) && inputResource.VisibilityMask.GetBit(b.Index))
                    {
                        objectData.World = b.WorldMatrix;
						objectData.Material = b.Material;
                        context.SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));
					    b.pMesh->Draw(&context);
                    }
                }
            };

            {
                GPU_PROFILE_SCOPE("Opaque", &context);
                context.SetPipelineState(m_pDiffusePSO.get());
                        
                context.SetDynamicDescriptor(4, 0, m_pLightGridOpaque->GetSRV());
                context.SetDynamicDescriptor(4, 1, m_pLightIndexListBufferOpaque->GetSRV());

                DrawBatches(Batch::Blending::Opaque | Batch::Blending::AlphaMask);
            }

            {
                GPU_PROFILE_SCOPE("Transparent", &context);
                context.SetPipelineState(m_pDiffuseAlphaPSO.get());
                        
                context.SetDynamicDescriptor(4, 0, m_pLightGridTransparent->GetSRV());
                context.SetDynamicDescriptor(4, 1, m_pLightIndexListBufferTransparent->GetSRV());

                DrawBatches(Batch::Blending::AlphaBlend);
            }

            context.EndRenderPass();
        });
}

void TiledForward::VisualizeLightDensity(RGGraph& graph, Camera& camera, GraphicsTexture* pTarget, GraphicsTexture* pDepth)
{
    if (!m_pVisualizeIntermediateTexture)
    {
        m_pVisualizeIntermediateTexture = std::make_unique<GraphicsTexture>(m_pGraphics, "LightDensity Debug Texture");
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

            context.SetPipelineState(m_pVisualizeLightsPSO.get());
            context.SetComputeRootSignature(m_pVisualizeLightsRS.get());

            context.SetComputeDynamicConstantBufferView(0, &constantData, sizeof(Data));

            context.SetDynamicDescriptor(1, 0, pTarget->GetSRV());
            context.SetDynamicDescriptor(1, 1, pDepth->GetSRV());
            context.SetDynamicDescriptor(1, 2, m_pLightGridOpaque->GetSRV());
            
            context.SetDynamicDescriptor(2, 0, m_pVisualizeIntermediateTexture->GetUAV());

            context.Dispatch(Math::DivideAndRoundUp(pTarget->GetWidth(), 16), Math::DivideAndRoundUp(pTarget->GetHeight(), 16));
            context.InsertUavBarrier();

            context.CopyTexture(m_pVisualizeIntermediateTexture.get(), pTarget);
        });
}

void TiledForward::SetupResources(Graphics* pGraphics)
{
    m_pLightGridOpaque = std::make_unique<GraphicsTexture>(pGraphics, "Light Grid Opaque");
    m_pLightGridTransparent = std::make_unique<GraphicsTexture>(pGraphics, "Light Grid Transparent");
}

void TiledForward::SetupPipelines(Graphics* pGraphics)
{
    {
        Shader computeShader("LightCulling.hlsl", ShaderType::Compute, "CSMain");

        m_pComputeLightCullRS = std::make_unique<RootSignature>(pGraphics);
        m_pComputeLightCullRS->FinalizeFromShader("Tiled Light Culling", computeShader);

        m_pComputeLightCullPipeline = std::make_unique<PipelineState>(pGraphics);
        m_pComputeLightCullPipeline->SetRootSignature(m_pComputeLightCullRS->GetRootSignature());
        m_pComputeLightCullPipeline->SetComputeShader(computeShader);
        m_pComputeLightCullPipeline->Finalize("Tiled Light Culling PSO");

        m_pLightIndexCounter = std::make_unique<Buffer>(pGraphics, "Light Index Counter");
        m_pLightIndexCounter->Create(BufferDesc::CreateStructured(2, sizeof(uint32_t)));
        m_pLightIndexCounter->CreateUAV(&m_pLightIndexCounterRawUAV, BufferUAVDesc::CreateRaw());
        m_pLightIndexListBufferOpaque = std::make_unique<Buffer>(pGraphics, "Light List Opaque");
        m_pLightIndexListBufferOpaque->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32_t)));
        m_pLightIndexListBufferTransparent = std::make_unique<Buffer>(pGraphics, "Light List Transparent");
        m_pLightIndexListBufferTransparent->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32_t)));
    }
    
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

		// root signature
		m_pDiffuseRS = std::make_unique<RootSignature>(pGraphics);
		m_pDiffuseRS->FinalizeFromShader("Diffuse PBR RS", vertexShader);

		// opaque
		{
			m_pDiffusePSO = std::make_unique<PipelineState>(pGraphics);
			m_pDiffusePSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
			m_pDiffusePSO->SetRootSignature(m_pDiffuseRS->GetRootSignature());
			m_pDiffusePSO->SetVertexShader(vertexShader);
			m_pDiffusePSO->SetPixelShader(pixelShader);
			m_pDiffusePSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, pGraphics->GetMultiSampleCount());
			m_pDiffusePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pDiffusePSO->SetDepthWrite(false);
			m_pDiffusePSO->Finalize("Diffuse PBR Pipeline");
		
		    // transparent
			m_pDiffuseAlphaPSO = std::make_unique<PipelineState>(*m_pDiffusePSO.get());
            m_pDiffuseAlphaPSO->SetBlendMode(BlendMode::Alpha, false);
            m_pDiffuseAlphaPSO->Finalize("Diffuse PBR (Alpha) Pipeline");
		}
	}

    {
        Shader computeShader("VisualizeLightCount.hlsl", ShaderType::Compute, "DebugLightDensityCS", { "TILED_FORWARD" });

        m_pVisualizeLightsRS = std::make_unique<RootSignature>(pGraphics);
        m_pVisualizeLightsRS->FinalizeFromShader("Visualize Light Density RS", computeShader);

        m_pVisualizeLightsPSO = std::make_unique<PipelineState>(pGraphics);
        m_pVisualizeLightsPSO->SetRootSignature(m_pVisualizeLightsRS->GetRootSignature());
        m_pVisualizeLightsPSO->SetComputeShader(computeShader);
        m_pVisualizeLightsPSO->Finalize("Visualize Light Density PSO");
    }
}
