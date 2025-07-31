#include "stdafx.h"
#include "TiledForward.h"
#include "Mesh.h"
#include "Profiler.h"
#include "Scene/Camera.h"
#include "RenderGraph/RenderGraph.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandQueue.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/ResourceViews.h"

static constexpr int MAX_LIGHT_DENSITY = 72000;
static constexpr int FORWARD_PLUS_BLOCK_SIZE = 16;

TiledForward::TiledForward(Graphics* pGraphics)
    : m_pGraphics(pGraphics)
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
	// 3. light culling
    //  - compute shader to buckets lights in tiles depending on their screen position
    //  - require a depth buffer
    //  - outputs a: - Texture2D containing a count and an offset of lights per tile.
    //								- uint[] index buffer to indicate what are visible in each tile
    graph.AddPass("Tiled Light Culling", [&](RGPassBuilder& builder)
        {
            builder.NeverCull();
            builder.Read(inputResource.ResolvedDepthBuffer);

            return [=](CommandContext& context, const RGPassResource& passResources)
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
                        uint32_t NumThreadGroups[4]{};
                        Vector2 ScreenDimensions;
                        uint32_t LightCount{0};
                    } Data{};

                    Data.CameraView = inputResource.pCamera->GetView();
                    Data.NumThreadGroups[0] = Math::DivideAndRoundUp(pDepthTexture->GetWidth(), FORWARD_PLUS_BLOCK_SIZE);
                    Data.NumThreadGroups[1] = Math::DivideAndRoundUp(pDepthTexture->GetHeight(), FORWARD_PLUS_BLOCK_SIZE);
                    Data.NumThreadGroups[2] = 1;
                    Data.ScreenDimensions.x = (float)pDepthTexture->GetWidth();
                    Data.ScreenDimensions.y = (float)pDepthTexture->GetHeight();
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

                    context.Dispatch(Data.NumThreadGroups[0], Data.NumThreadGroups[1], Data.NumThreadGroups[2]);
                };
        });

    // 5. base pass
    //  - render the scene using the shadow mapping result and the light culling buffers
    graph.AddPass("Base Pass", [&](RGPassBuilder& builder)
        {
            builder.NeverCull();
            builder.Read(inputResource.DepthBuffer);
            return[=](CommandContext& context, const RGPassResource& passResources)
                {
                    struct PerFrameData
                    {
                        Matrix ViewInverse;
                    } frameData{};

                    struct PerObjectData
                    {
                        Matrix World;
                        Matrix WorldViewProjection;
                    } objectData{};

                    frameData.ViewInverse = inputResource.pCamera->GetViewInverse();

                    GraphicsTexture* pDepthTexture = passResources.GetTexture(inputResource.DepthBuffer);

                    context.SetViewport(FloatRect(0, 0, (float)pDepthTexture->GetWidth(), (float)pDepthTexture->GetHeight()));

                    context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    context.InsertResourceBarrier(m_pLightGridTransparent.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    context.InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    context.InsertResourceBarrier(m_pLightIndexListBufferTransparent.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    context.InsertResourceBarrier(inputResource.pShadowMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    context.InsertResourceBarrier(pDepthTexture, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                    context.InsertResourceBarrier(inputResource.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);

                    context.BeginRenderPass(RenderPassInfo(inputResource.pRenderTarget, RenderPassAccess::Clear_Store, pDepthTexture, RenderPassAccess::Load_DontCare));

                    context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    context.SetGraphicsRootSignature(m_pDiffuseRS.get());

                    context.SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
                    context.SetDynamicConstantBufferView(2, inputResource.pLightData, sizeof(LightData));

                    context.SetDynamicDescriptor(4, 0, inputResource.pShadowMap->GetSRV());
                    context.SetDynamicDescriptor(4, 3, inputResource.pLightBuffer->GetSRV());

                    {
                        GPU_PROFILE_SCOPE("Opaque", &context);
                        context.SetPipelineState(m_pDiffusePSO.get());
                        
                        context.SetDynamicDescriptor(4, 1, m_pLightGridOpaque->GetSRV());
                        context.SetDynamicDescriptor(4, 2, m_pLightIndexListBufferOpaque->GetSRV());

                        for (const Batch& b : *inputResource.pOpaqueBatches)
                        {
                            objectData.World = b.WorldMatrix;
                            objectData.WorldViewProjection = b.WorldMatrix * inputResource.pCamera->GetViewProjection();
                            context.SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));

                            context.SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
                            context.SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
                            context.SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());

                            b.pMesh->Draw(&context);
                        }
                    }

                    {
                        GPU_PROFILE_SCOPE("Transparent", &context);
                        context.SetPipelineState(m_pDiffuseAlphaPSO.get());
                        
                        context.SetDynamicDescriptor(4, 1, m_pLightGridTransparent->GetSRV());
                        context.SetDynamicDescriptor(4, 2, m_pLightIndexListBufferTransparent->GetSRV());

                        for (const Batch& b : *inputResource.pTransparentBatches)
                        {
                            objectData.World = b.WorldMatrix;
                            objectData.WorldViewProjection = b.WorldMatrix * inputResource.pCamera->GetViewProjection();
                            context.SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));

                            context.SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
                            context.SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
                            context.SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());

                            b.pMesh->Draw(&context);
                        }
                    }

                    context.EndRenderPass();
                };
        });
}

void TiledForward::SetupResources(Graphics* pGraphics)
{
    m_pLightGridOpaque = std::make_unique<GraphicsTexture>(pGraphics, "Light Grid Opaque");
    m_pLightGridTransparent = std::make_unique<GraphicsTexture>(pGraphics, "Light Grid Transparent");
}

void TiledForward::SetupPipelines(Graphics* pGraphics)
{
    Shader computeShader("Resources/Shaders/LightCulling.hlsl", Shader::Type::Compute, "CSMain");

    m_pComputeLightCullRS = std::make_unique<RootSignature>();
    m_pComputeLightCullRS->FinalizeFromShader("Tiled Light Culling", computeShader, pGraphics->GetDevice());

    m_pComputeLightCullPipeline = std::make_unique<PipelineState>();
    m_pComputeLightCullPipeline->SetRootSignature(m_pComputeLightCullRS->GetRootSignature());
    m_pComputeLightCullPipeline->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
    m_pComputeLightCullPipeline->Finalize("Tiled Light Culling PSO", pGraphics->GetDevice());

    m_pLightIndexCounter = std::make_unique<Buffer>(pGraphics, "Light Index Counter");
    m_pLightIndexCounter->Create(BufferDesc::CreateStructured(2, sizeof(uint32_t)));
    m_pLightIndexCounter->CreateUAV(&m_pLightIndexCounterRawUAV, BufferUAVDesc::CreateRaw());
    m_pLightIndexListBufferOpaque = std::make_unique<Buffer>(pGraphics, "Light List Opaque");
    m_pLightIndexListBufferOpaque->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32_t)));
    m_pLightIndexListBufferTransparent = std::make_unique<Buffer>(pGraphics, "Light List Transparent");
    m_pLightIndexListBufferTransparent->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32_t)));

    D3D12_INPUT_ELEMENT_DESC inputElements[] = {
			D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

    // PBR diffuse passes
	{
		// shaders
		Shader vertexShader("Resources/Shaders/Diffuse.hlsl", Shader::Type::Vertex, "VSMain", { "SHADOW" });
		Shader pixelShader("Resources/Shaders/Diffuse.hlsl", Shader::Type::Pixel, "PSMain", { "SHADOW" });

		// root signature
		m_pDiffuseRS = std::make_unique<RootSignature>();
		m_pDiffuseRS->FinalizeFromShader("Diffuse PBR RS", vertexShader, pGraphics->GetDevice());

		// opaque
		{
			m_pDiffusePSO = std::make_unique<PipelineState>();
			m_pDiffusePSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
			m_pDiffusePSO->SetRootSignature(m_pDiffuseRS->GetRootSignature());
			m_pDiffusePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pDiffusePSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
			m_pDiffusePSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, pGraphics->GetMultiSampleCount(), pGraphics->GetMultiSampleQualityLevel(pGraphics->GetMultiSampleCount()));
			m_pDiffusePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pDiffusePSO->SetDepthWrite(false);
			m_pDiffusePSO->Finalize("Diffuse PBR Pipeline", pGraphics->GetDevice());
		
		    // transparent
			m_pDiffuseAlphaPSO = std::make_unique<PipelineState>(*m_pDiffusePSO.get());
            m_pDiffuseAlphaPSO->SetBlendMode(BlendMode::Alpha, false);
            m_pDiffuseAlphaPSO->Finalize("Diffuse PBR (Alpha) Pipeline", pGraphics->GetDevice());
		}
	}
}