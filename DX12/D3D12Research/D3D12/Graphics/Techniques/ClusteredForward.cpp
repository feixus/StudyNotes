#include "stdafx.h"
#include "ClusteredForward.h"
#include "Scene/Camera.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/ResourceViews.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Mesh.h"
#include "Graphics/Profiler.h"

static constexpr int cClusterSize = 64;
static constexpr int cClusterCountZ = 32;

namespace Tweakables
{
    extern int g_SsrSamples;
}

bool g_VisualizeClusters = false;

ClusteredForward::ClusteredForward(Graphics* pGraphics)
    : m_pGraphics(pGraphics)
{
    SetupResources(pGraphics);
    SetupPipelines(pGraphics);
}

void ClusteredForward::OnSwapchainCreated(int windowWidth, int windowHeight)
{
	m_ClusterCountX = Math::RoundUp((float)windowWidth / cClusterSize);
	m_ClusterCountY = Math::RoundUp((float)windowHeight / cClusterSize);
    m_MaxClusters = m_ClusterCountX * m_ClusterCountY * cClusterCountZ;

    m_pAabbBuffer->Create(BufferDesc::CreateStructured(m_MaxClusters, sizeof(Vector4) * 2));
    m_pUniqueClusterBuffer->Create(BufferDesc::CreateStructured(m_MaxClusters, sizeof(uint32_t)));
    m_pUniqueClusterBuffer->CreateUAV(&m_pUniqueClusterBufferRawUAV, BufferUAVDesc::CreateRaw());
    m_pCompactedClusterBuffer->Create(BufferDesc::CreateStructured(m_MaxClusters, sizeof(uint32_t)));
    m_pCompactedClusterBuffer->CreateUAV(&m_pCompactedClusterBufferRawUAV, BufferUAVDesc::CreateRaw());
    m_pDebugCompactedClusterBuffer->Create(BufferDesc::CreateStructured(m_MaxClusters, sizeof(uint32_t)));

    m_pLightIndexGrid->Create(BufferDesc::CreateStructured(m_MaxClusters * 32, sizeof(uint32_t)));
    m_pLightGrid->Create(BufferDesc::CreateStructured(m_MaxClusters, 2 * sizeof(uint32_t)));
    m_pLightGrid->CreateUAV(&m_pLightGridRawUAV, BufferUAVDesc::CreateRaw());
    m_pDebugLightGrid->Create(BufferDesc::CreateStructured(m_MaxClusters, 2 * sizeof(uint32_t)));

    m_ViewportDirty = true;
}

Vector2 ComputeLightGridParams(float nearZ, float farZ)
{
    Vector2 lightGridParams;
    float n = Math::Min(nearZ, farZ);
    float f = Math::Max(nearZ, farZ);
	lightGridParams.x = (float)cClusterCountZ / log(f / n);
	lightGridParams.y = (float)cClusterCountZ * log(n) / log(f / n);
    return lightGridParams;
}

void ClusteredForward::Execute(RGGraph& graph, const SceneData& inputResource)
{
    RG_GRAPH_SCOPE("Clustered Lighting", graph);

    Vector2 screenDimensions((float)inputResource.pRenderTarget->GetWidth(), (float)inputResource.pRenderTarget->GetHeight());
    float nearZ = inputResource.pCamera->GetNear();
    float farZ = inputResource.pCamera->GetFar();
    Vector2 lightGridParams = ComputeLightGridParams(nearZ, farZ);

    if (m_ViewportDirty)
    {
        RGPassBuilder calculateAabbs = graph.AddPass("Create AABBs");
        calculateAabbs.Bind([=](CommandContext& context, const RGPassResource& passResources)
            {
                context.InsertResourceBarrier(m_pAabbBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                context.SetPipelineState(m_pCreateAabbPSO);
                context.SetComputeRootSignature(m_pCreateAabbRS.get());

                struct ConstantBuffer
                {
                    Matrix ProjectionInverse;
                    Vector2 ScreenDimensionsInv;
                    IntVector2 ClusterSize;
                    IntVector3 ClusterDimensions;
                    float NearZ{0};
                    float FarZ{0};
                } constantBuffer;

                constantBuffer.ScreenDimensionsInv = Vector2(1.0f / screenDimensions.x, 1.0f / screenDimensions.y);
                constantBuffer.NearZ = inputResource.pCamera->GetFar();
                constantBuffer.FarZ = inputResource.pCamera->GetNear();
                constantBuffer.ProjectionInverse = inputResource.pCamera->GetProjectionInverse();
                constantBuffer.ClusterSize = IntVector2(cClusterSize, cClusterSize);
                constantBuffer.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, cClusterCountZ);

                context.SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(constantBuffer));
                context.BindResource(1, 0, m_pAabbBuffer->GetUAV());

                // Cluster count in z is 32 so fits nicely in a wavefront on Nvidia so make groupsize in shader 32
                context.Dispatch(m_ClusterCountX, m_ClusterCountY, Math::DivideAndRoundUp(cClusterCountZ, 32));
            });

        m_ViewportDirty = false;
    }

    RGPassBuilder markClusters = graph.AddPass("Mark Clusters");
    markClusters.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
			context.InsertResourceBarrier(inputResource.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
			context.InsertResourceBarrier(m_pUniqueClusterBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.InsertResourceBarrier(inputResource.pDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);

            context.ClearUavUInt(m_pUniqueClusterBuffer.get(), m_pUniqueClusterBufferRawUAV);

            context.BeginRenderPass(RenderPassInfo(inputResource.pDepthBuffer, RenderPassAccess::Load_Store, true));

            context.SetGraphicsRootSignature(m_pMarkUniqueClustersRS.get());
            context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            
			struct PerFrameParameters
			{
                IntVector3 ClusterDimensions;
                int padding0;
				IntVector2 ClusterSize;
                Vector2 LightGridParams;
                Matrix View;
                Matrix ViewProjection;
			} perFrameParameters{};
            
            struct PerObjectParameters
            {
                Matrix World;
            } perObjectParameters{};

			perFrameParameters.LightGridParams = lightGridParams;
			perFrameParameters.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, cClusterCountZ);
			perFrameParameters.ClusterSize = IntVector2(cClusterSize, cClusterSize);
            perFrameParameters.View = inputResource.pCamera->GetView();
            perFrameParameters.ViewProjection = inputResource.pCamera->GetViewProjection();
            
            context.SetGraphicsDynamicConstantBufferView(1, &perFrameParameters, sizeof(perFrameParameters));
            context.BindResource(2, 0, m_pUniqueClusterBuffer->GetUAV());

            auto DrawBatches = [&](Batch::Blending blendMode)
            {
                for (const Batch& b : inputResource.Batches)
                {
                    if (Any(b.BlendMode, blendMode) && inputResource.VisibilityMask.GetBit(b.Index))
                    {
                        perObjectParameters.World = b.WorldMatrix;
                        context.SetGraphicsDynamicConstantBufferView(0, &perObjectParameters, sizeof(perObjectParameters));
					    b.pMesh->Draw(&context);
                    }
                }
            };

			{
                GPU_PROFILE_SCOPE("Opaque", &context);
                context.SetPipelineState(m_pMarkUniqueClustersOpaquePSO);
                DrawBatches(Batch::Blending::Opaque);
			}
            
			{
                GPU_PROFILE_SCOPE("Transparent", &context);
                context.SetPipelineState(m_pMarkUniqueClustersTransparentPSO);
                DrawBatches(Batch::Blending::AlphaBlend | Batch::Blending::AlphaMask);
			}

            context.EndRenderPass();
        });

    RGPassBuilder compactClusters = graph.AddPass("Compact Clusters");
    compactClusters.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
			context.InsertResourceBarrier(m_pUniqueClusterBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pCompactedClusterBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            UnorderedAccessView* pCompactedClusterUav = m_pCompactedClusterBuffer->GetUAV();
            context.InsertResourceBarrier(pCompactedClusterUav->GetCounter(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            context.ClearUavUInt(m_pCompactedClusterBuffer.get(), m_pCompactedClusterBufferRawUAV);
            context.ClearUavUInt(pCompactedClusterUav->GetCounter(), pCompactedClusterUav->GetCounterUAV());

            context.SetPipelineState(m_pCompactClusterListPSO);
            context.SetComputeRootSignature(m_pCompactClusterListRS.get());

            context.BindResource(0, 0, m_pUniqueClusterBuffer->GetSRV());
            context.BindResource(1, 0, m_pCompactedClusterBuffer->GetUAV());

            context.Dispatch(Math::RoundUp(m_MaxClusters / 64.f));
        });

    RGPassBuilder updateArguments = graph.AddPass("Update Indirect Arguments");
    updateArguments.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
			UnorderedAccessView* pCompactedClusterUav = m_pCompactedClusterBuffer->GetUAV();
            context.InsertResourceBarrier(pCompactedClusterUav->GetCounter(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            context.SetPipelineState(m_pUpdateIndirectArgumentsPSO);
            context.SetComputeRootSignature(m_pUpdateIndirectArgumentsRS.get());

            context.BindResource(0, 0, m_pCompactedClusterBuffer->GetUAV()->GetCounter()->GetSRV());
            context.BindResource(1, 0, m_pIndirectArguments->GetUAV());

            context.Dispatch(1, 1, 1);
        });

    RGPassBuilder lightCulling = graph.AddPass("Clustered Light Culling");
    lightCulling.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
            context.SetPipelineState(m_pLightCullingPSO);
            context.SetComputeRootSignature(m_pLightCullingRS.get());

            context.InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            context.InsertResourceBarrier(m_pCompactedClusterBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pAabbBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.InsertResourceBarrier(inputResource.pLightBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

            context.ClearUavUInt(m_pLightGrid.get(), m_pLightGridRawUAV);
            context.ClearUavUInt(m_pLightIndexCounter.get(), m_pLightIndexCounter->GetUAV());

            struct ConstantBuffer
            {
                Matrix View;
                int LightCount{ 0 };
            } constantBuffer{};

            constantBuffer.View = inputResource.pCamera->GetView();
            constantBuffer.LightCount = inputResource.pLightBuffer->GetNumElements();

            context.SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));
            context.BindResource(1, 0, inputResource.pLightBuffer->GetSRV());
            context.BindResource(1, 1, m_pAabbBuffer->GetSRV());
            context.BindResource(1, 2, m_pCompactedClusterBuffer->GetSRV());
            context.BindResource(2, 0, m_pLightIndexCounter->GetUAV());
            context.BindResource(2, 1, m_pLightIndexGrid->GetUAV());
            context.BindResource(2, 2, m_pLightGrid->GetUAV());

            context.ExecuteIndirect(m_pLightCullingCommandSignature.get(), 1, m_pIndirectArguments.get(), nullptr);
    });
    
    RGPassBuilder basePass = graph.AddPass("Base Pass");
    basePass.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
			struct PerObjectData
			{
				Matrix World;
                MaterialData Material;
			} objectData;

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
				IntVector3 ClusterDimensions;
                int pad;
				IntVector2 ClusterSize;
				Vector2 LightGridParams;
			} frameData{};

			frameData.View = inputResource.pCamera->GetView();
			frameData.Projection = inputResource.pCamera->GetProjection();
            frameData.ProjectionInverse = inputResource.pCamera->GetProjectionInverse();
            frameData.ViewProjection = inputResource.pCamera->GetViewProjection();
            frameData.ViewPosition = Vector4(inputResource.pCamera->GetPosition());
			frameData.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, cClusterCountZ);
			frameData.InvScreenDimensions = Vector2(1.0f / screenDimensions.x, 1.0f / screenDimensions.y);
			frameData.NearZ = nearZ;
			frameData.FarZ = farZ;
			frameData.ClusterSize = IntVector2(cClusterSize, cClusterSize);
			frameData.LightGridParams = lightGridParams;
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

            context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(inputResource.pAO, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(inputResource.pPreviousColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(inputResource.pResolvedDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(inputResource.pDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
            context.InsertResourceBarrier(inputResource.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
            context.InsertResourceBarrier(inputResource.pNormals, D3D12_RESOURCE_STATE_RENDER_TARGET);

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

            context.SetGraphicsDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
            context.SetGraphicsDynamicConstantBufferView(2, inputResource.pShadowData, sizeof(ShadowData));
            context.BindResourceTable(3, inputResource.GlobalSRVHeapHandle, CommandListContext::Graphics);
            context.BindResource(4, 0, m_pLightGrid->GetSRV());
            context.BindResource(4, 1, m_pLightIndexGrid->GetSRV());
            context.BindResource(4, 2, inputResource.pLightBuffer->GetSRV());
            context.BindResource(4, 3, inputResource.pAO->GetSRV());
            context.BindResource(4, 4, inputResource.pResolvedDepth->GetSRV());
            context.BindResource(4, 5, inputResource.pPreviousColor->GetSRV());
        
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
                        objectData.World = Matrix::Identity;
                        objectData.Material = b.Material;
                        context.SetGraphicsDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));
                        b.pMesh->Draw(&context);
                    }
                }
			};

            {
                GPU_PROFILE_SCOPE("Opaque", &context);
                context.SetPipelineState(m_pDiffusePSO);
                DrawBatches(Batch::Blending::Opaque | Batch::Blending::AlphaMask);
            }

            {
                GPU_PROFILE_SCOPE("Transparent", &context);
                context.SetPipelineState(m_pDiffuseTransparencyPSO);
                DrawBatches(Batch::Blending::AlphaBlend);
            }

            context.EndRenderPass();
        });
 

    if (g_VisualizeClusters)
    {
        RGPassBuilder visualize = graph.AddPass("Visualize Clusters");
        visualize.Bind([=](CommandContext& context, const RGPassResource& passResources)
            {
				if (m_DidCopyDebugClusterData == false)
				{
                    context.CopyTexture(m_pCompactedClusterBuffer.get(), m_pDebugCompactedClusterBuffer.get());
                    context.CopyTexture(m_pLightGrid.get(), m_pDebugLightGrid.get());
                    m_DebugClusterViewMatrix = inputResource.pCamera->GetView();
                    m_DebugClusterViewMatrix.Invert(m_DebugClusterViewMatrix);
                    m_DidCopyDebugClusterData = true;
                }

                context.BeginRenderPass(RenderPassInfo(inputResource.pRenderTarget, RenderPassAccess::Load_Store, inputResource.pDepthBuffer, RenderPassAccess::Load_Store, false));

                context.SetPipelineState(m_pDebugClusterPSO);
                context.SetGraphicsRootSignature(m_pDebugClusterRS.get());

                Matrix p = m_DebugClusterViewMatrix * inputResource.pCamera->GetViewProjection();
                context.SetGraphicsDynamicConstantBufferView(0, &p, sizeof(Matrix));
                context.BindResource(1, 0, m_pAabbBuffer->GetSRV());
                context.BindResource(1, 1, m_pDebugCompactedClusterBuffer->GetSRV());
                context.BindResource(1, 2, m_pDebugLightGrid->GetSRV());
                context.BindResource(1, 3, m_pHeatMapTexture->GetSRV());

                if (m_pDebugClusterPSO->GetType() == PipelineStateType::Mesh)
                {
                    context.DispatchMesh(m_MaxClusters);
                }
                else
                {
                    context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
                    context.Draw(0, m_MaxClusters);
                }


                context.EndRenderPass();
            });
    }
    else
    {
        m_DidCopyDebugClusterData = false;
    }
}

void ClusteredForward::VisualizeLightDensity(RGGraph& graph, Camera& camera, GraphicsTexture* pTarget, GraphicsTexture* pDepth)
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
    Vector2 lightGridParams = ComputeLightGridParams(nearZ, farZ);

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
            constantData.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, cClusterCountZ);
            constantData.ClusterSize = IntVector2(cClusterSize, cClusterSize);
            constantData.LightGridParams = lightGridParams;
            constantData.Near = nearZ;
            constantData.Far = farZ;
            constantData.FoV = camera.GetFoV();

            context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pVisualizeIntermediateTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            context.SetPipelineState(m_pVisualizeLightsPSO);
            context.SetComputeRootSignature(m_pVisualizeLightsRS.get());

            context.SetComputeDynamicConstantBufferView(0, &constantData, sizeof(Data));

            context.BindResource(1, 0, pTarget->GetSRV());
            context.BindResource(1, 1, pDepth->GetSRV());
            context.BindResource(1, 2, m_pLightGrid->GetSRV());
            
            context.BindResource(2, 0, m_pVisualizeIntermediateTexture->GetUAV());

            context.Dispatch(Math::DivideAndRoundUp(pTarget->GetWidth(), 16), Math::DivideAndRoundUp(pTarget->GetHeight(), 16));
            context.InsertUavBarrier();

            context.CopyTexture(m_pVisualizeIntermediateTexture.get(), pTarget);
        });
}

void ClusteredForward::SetupResources(Graphics* pGraphics)
{
    m_pAabbBuffer = std::make_unique<Buffer>(pGraphics, "AABBs");
    m_pUniqueClusterBuffer = std::make_unique<Buffer>(pGraphics, "Unique Clusters");
    m_pCompactedClusterBuffer = std::make_unique<Buffer>(pGraphics, "Compacted Cluster");
    m_pDebugCompactedClusterBuffer = std::make_unique<Buffer>(pGraphics, "Debug Compacted Cluster");
	
    m_pIndirectArguments = std::make_unique<Buffer>(pGraphics, "Light Culling Indirect Arguments");
    m_pIndirectArguments->Create(BufferDesc::CreateIndirectArgumemnts<uint32_t>(3));

	m_pLightIndexCounter = std::make_unique<Buffer>(pGraphics, "Light Index Counter");
	m_pLightIndexCounter->Create(BufferDesc::CreateByteAddress(sizeof(uint32_t)));
	m_pLightIndexGrid = std::make_unique<Buffer>(pGraphics, "Light Index Grid");
	
    m_pLightGrid = std::make_unique<Buffer>(pGraphics, "Light Grid");
    m_pDebugLightGrid = std::make_unique<Buffer>(pGraphics, "Debug Light Grid");

    CommandContext* pContext = m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
    m_pHeatMapTexture = std::make_unique<GraphicsTexture>(pGraphics, "Heatmap texture");
    m_pHeatMapTexture->Create(pContext, "Resources/textures/HeatMap.png");
    pContext->Execute(true);
}

void ClusteredForward::SetupPipelines(Graphics* pGraphics)
{
    // AABB
    {
        Shader* pComputeShader = pGraphics->GetShaderManager()->GetShader("ClusterAABBGeneration.hlsl", ShaderType::Compute, "GenerateAABBs");

        m_pCreateAabbRS = std::make_unique<RootSignature>(pGraphics);
        m_pCreateAabbRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        m_pCreateAabbRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pCreateAabbRS->Finalize("Create AABBs", D3D12_ROOT_SIGNATURE_FLAG_NONE);

        PipelineStateInitializer psoDesc;
        psoDesc.SetComputeShader(pComputeShader);
        psoDesc.SetRootSignature(m_pCreateAabbRS->GetRootSignature());
        psoDesc.SetName("Create AABBs");
        m_pCreateAabbPSO = pGraphics->CreatePipeline(psoDesc);
    }

    // mark unique clusters
    {
        CD3DX12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            CD3DX12_INPUT_ELEMENT_DESC("POSITION", DXGI_FORMAT_R32G32B32_FLOAT),
            CD3DX12_INPUT_ELEMENT_DESC("TEXCOORD", DXGI_FORMAT_R32G32_FLOAT),
        };

        Shader* pVertexShader = pGraphics->GetShaderManager()->GetShader("ClusterMarking.hlsl", ShaderType::Vertex, "MarkClusters_VS");
		Shader* pPixelShaderOpaque = pGraphics->GetShaderManager()->GetShader("ClusterMarking.hlsl", ShaderType::Pixel, "MarkClusters_PS");

        m_pMarkUniqueClustersRS = std::make_unique<RootSignature>(pGraphics);
        m_pMarkUniqueClustersRS->FinalizeFromShader("Mark Unique Clusters", pVertexShader);

        PipelineStateInitializer psoDesc;
        psoDesc.SetInputLayout(inputElementDescs, ARRAYSIZE(inputElementDescs));
        psoDesc.SetVertexShader(pVertexShader);
        psoDesc.SetPixelShader(pPixelShaderOpaque);
        psoDesc.SetBlendMode(BlendMode::Replace, false);
        psoDesc.SetRootSignature(m_pMarkUniqueClustersRS->GetRootSignature());
        psoDesc.SetRenderTargetFormats(nullptr, 0, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount());
        psoDesc.SetDepthWrite(false);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);  // mark cluster must need GREATER_EQUAL on reverse-Z depth
        psoDesc.SetName("Mark Unique Clusters");
        m_pMarkUniqueClustersOpaquePSO = pGraphics->CreatePipeline(psoDesc);

		psoDesc.SetName("Mark Unique Clusters");
		m_pMarkUniqueClustersTransparentPSO = pGraphics->CreatePipeline(psoDesc);
    }

    // compact cluster list
    {
        Shader* pComputeShader = pGraphics->GetShaderManager()->GetShader("ClusterCompaction.hlsl", ShaderType::Compute, "CompactClusters");

        m_pCompactClusterListRS = std::make_unique<RootSignature>(pGraphics);
        m_pCompactClusterListRS->FinalizeFromShader("Compact Cluster List", pComputeShader);

        PipelineStateInitializer psoDesc;
        psoDesc.SetComputeShader(pComputeShader);
        psoDesc.SetRootSignature(m_pCompactClusterListRS->GetRootSignature());
        psoDesc.SetName("Compact Cluster List");
        m_pCompactClusterListPSO = pGraphics->CreatePipeline(psoDesc);
    }

    // prepare indirect dispatch buffer
    {
        Shader* pComputeShader = pGraphics->GetShaderManager()->GetShader("ClusterLightCullingArguments.hlsl", ShaderType::Compute, "UpdateIndirectArguments");

        m_pUpdateIndirectArgumentsRS = std::make_unique<RootSignature>(pGraphics);
        m_pUpdateIndirectArgumentsRS->FinalizeFromShader("Update Indirect Dispatch Buffer", pComputeShader);

        PipelineStateInitializer psoDesc;
        psoDesc.SetComputeShader(pComputeShader);
        psoDesc.SetRootSignature(m_pUpdateIndirectArgumentsRS->GetRootSignature());
        psoDesc.SetName("Update Indirect Dispatch Buffer");
        m_pUpdateIndirectArgumentsPSO = pGraphics->CreatePipeline(psoDesc);
    }

    // light culling
    {
        Shader* pComputeShader = pGraphics->GetShaderManager()->GetShader("ClusterLightCulling.hlsl", ShaderType::Compute, "LightCulling");

        m_pLightCullingRS = std::make_unique<RootSignature>(pGraphics);
        m_pLightCullingRS->FinalizeFromShader("Light Culling", pComputeShader);

        PipelineStateInitializer psoDesc;
        psoDesc.SetComputeShader(pComputeShader);
        psoDesc.SetRootSignature(m_pLightCullingRS->GetRootSignature());
        psoDesc.SetName("Light Culling");
        m_pLightCullingPSO = pGraphics->CreatePipeline(psoDesc);

        m_pLightCullingCommandSignature = std::make_unique<CommandSignature>(m_pGraphics);
        m_pLightCullingCommandSignature->AddDispatch();
        m_pLightCullingCommandSignature->Finalize("Light Culling Command Signature");
    }

    // diffuse
    {
        CD3DX12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", DXGI_FORMAT_R32G32B32_FLOAT },
            { "TEXCOORD", DXGI_FORMAT_R32G32_FLOAT },
            { "NORMAL", DXGI_FORMAT_R32G32B32_FLOAT },
            { "TANGENT", DXGI_FORMAT_R32G32B32_FLOAT },
            { "TEXCOORD", DXGI_FORMAT_R32G32B32_FLOAT, 1 },
        };

        Shader* pVertexShader = pGraphics->GetShaderManager()->GetShader("Diffuse.hlsl", ShaderType::Vertex, "VSMain", { "CLUSTERED_FORWARD" });
        Shader* pPixelShader = pGraphics->GetShaderManager()->GetShader("Diffuse.hlsl", ShaderType::Pixel, "PSMain", { "CLUSTERED_FORWARD" });

        m_pDiffuseRS = std::make_unique<RootSignature>(pGraphics);
        m_pDiffuseRS->FinalizeFromShader("Diffuse", pVertexShader);

        DXGI_FORMAT formats[] = { Graphics::RENDER_TARGET_FORMAT, DXGI_FORMAT_R16G16B16A16_FLOAT };

        // opaque
        PipelineStateInitializer psoDesc;
        psoDesc.SetInputLayout(inputElementDescs, ARRAYSIZE(inputElementDescs));
        psoDesc.SetVertexShader(pVertexShader);
        psoDesc.SetPixelShader(pPixelShader);
        psoDesc.SetBlendMode(BlendMode::Replace, false);
        psoDesc.SetRootSignature(m_pDiffuseRS->GetRootSignature());
        psoDesc.SetRenderTargetFormats(formats, std::size(formats), Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount());
        psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
        psoDesc.SetDepthWrite(false);
        psoDesc.SetName("Diffuse (Opaque)");
        m_pDiffusePSO = pGraphics->CreatePipeline(psoDesc);

        // transparent
		psoDesc.SetBlendMode(BlendMode::Alpha, false);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		psoDesc.SetName("Diffuse (Transparent)");
		m_pDiffuseTransparencyPSO = pGraphics->CreatePipeline(psoDesc);
    }

    // cluster debug rendering
    {
        Shader* pPixelShader = pGraphics->GetShaderManager()->GetShader("ClusterDebugDrawing.hlsl", ShaderType::Pixel, "PSMain");

        m_pDebugClusterRS = std::make_unique<RootSignature>(pGraphics);

        PipelineStateInitializer psoDesc;
        psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
        psoDesc.SetDepthWrite(false);
        psoDesc.SetPixelShader(pPixelShader);
        psoDesc.SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount());
        psoDesc.SetBlendMode(BlendMode::Add, false);
        
        if (m_pGraphics->SupportMeshShaders())
        {
            Shader* pMeshShader = pGraphics->GetShaderManager()->GetShader("ClusterDebugDrawing.hlsl", ShaderType::Mesh, "MSMain");
            
		    m_pDebugClusterRS->FinalizeFromShader("Debug Cluster", pMeshShader);
            
            psoDesc.SetRootSignature(m_pDebugClusterRS->GetRootSignature());
            psoDesc.SetMeshShader(pMeshShader);
			psoDesc.SetName("Debug Cluster PSO");
        }
        else
        {
            Shader* pVertexShader = pGraphics->GetShaderManager()->GetShader("ClusterDebugDrawing.hlsl", ShaderType::Vertex, "VSMain");
            Shader* pGeometryShader = pGraphics->GetShaderManager()->GetShader("ClusterDebugDrawing.hlsl", ShaderType::Geometry, "GSMain");
            
		    m_pDebugClusterRS->FinalizeFromShader("Debug Cluster", pVertexShader);
            
            psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
            psoDesc.SetVertexShader(pVertexShader);
            psoDesc.SetGeometryShader(pGeometryShader);
            psoDesc.SetRootSignature(m_pDebugClusterRS->GetRootSignature());
            psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
            psoDesc.SetName("Debug Cluster PSO");
        }

        m_pDebugClusterPSO = pGraphics->CreatePipeline(psoDesc);
    }

    {
        Shader* pComputeShader = pGraphics->GetShaderManager()->GetShader("VisualizeLightCount.hlsl", ShaderType::Compute, "DebugLightDensityCS", { "CLUSTERED_FORWARD" });

        m_pVisualizeLightsRS = std::make_unique<RootSignature>(pGraphics);
        m_pVisualizeLightsRS->FinalizeFromShader("Visualize Light Density RS", pComputeShader);

        PipelineStateInitializer psoDesc;
        psoDesc.SetRootSignature(m_pVisualizeLightsRS->GetRootSignature());
        psoDesc.SetComputeShader(pComputeShader);
        psoDesc.SetName("Visualize Light Density PSO");
        m_pVisualizeLightsPSO = pGraphics->CreatePipeline(psoDesc);
    }
}

