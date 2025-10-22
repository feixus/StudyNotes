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

                context.SetPipelineState(m_pCreateAabbPSO.get());
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
                context.SetDynamicDescriptor(1, 0, m_pAabbBuffer->GetUAV());

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

            context.BeginRenderPass(RenderPassInfo(inputResource.pDepthBuffer, RenderPassAccess::Load_DontCare, true));

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
            
            context.SetDynamicConstantBufferView(1, &perFrameParameters, sizeof(perFrameParameters));
            context.SetDynamicDescriptor(2, 0, m_pUniqueClusterBuffer->GetUAV());

            auto DrawBatches = [&](Batch::Blending blendMode)
            {
                for (const Batch& b : inputResource.Batches)
                {
                    if (Any(b.BlendMode, blendMode) && inputResource.VisibilityMask.GetBit(b.Index))
                    {
                        perObjectParameters.World = b.WorldMatrix;
                        context.SetDynamicConstantBufferView(0, &perObjectParameters, sizeof(perObjectParameters));
					    b.pMesh->Draw(&context);
                    }
                }
            };

			{
                GPU_PROFILE_SCOPE("Opaque", &context);
                context.SetPipelineState(m_pMarkUniqueClustersOpaquePSO.get());
                DrawBatches(Batch::Blending::Opaque);
			}
            
			{
                GPU_PROFILE_SCOPE("Transparent", &context);
                context.SetPipelineState(m_pMarkUniqueClustersTransparentPSO.get());
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

            context.SetPipelineState(m_pCompactClusterListPSO.get());
            context.SetComputeRootSignature(m_pCompactClusterListRS.get());

            context.SetDynamicDescriptor(0, 0, m_pUniqueClusterBuffer->GetSRV());
            context.SetDynamicDescriptor(1, 0, m_pCompactedClusterBuffer->GetUAV());

            context.Dispatch(Math::RoundUp(m_MaxClusters / 64.f));
        });

    RGPassBuilder updateArguments = graph.AddPass("Update Indirect Arguments");
    updateArguments.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
			UnorderedAccessView* pCompactedClusterUav = m_pCompactedClusterBuffer->GetUAV();
            context.InsertResourceBarrier(pCompactedClusterUav->GetCounter(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            context.SetPipelineState(m_pUpdateIndirectArgumentsPSO.get());
            context.SetComputeRootSignature(m_pUpdateIndirectArgumentsRS.get());

            context.SetDynamicDescriptor(0, 0, m_pCompactedClusterBuffer->GetUAV()->GetCounter()->GetSRV());
            context.SetDynamicDescriptor(1, 0, m_pIndirectArguments->GetUAV());

            context.Dispatch(1, 1, 1);
        });

    RGPassBuilder lightCulling = graph.AddPass("Clustered Light Culling");
    lightCulling.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
            context.SetPipelineState(m_pLightCullingPSO.get());
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
            constantBuffer.LightCount = (int)inputResource.pLightBuffer->GetDesc().ElementCount;

            context.SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));
            context.SetDynamicDescriptor(1, 0, inputResource.pLightBuffer->GetSRV());
            context.SetDynamicDescriptor(1, 1, m_pAabbBuffer->GetSRV());
            context.SetDynamicDescriptor(1, 2, m_pCompactedClusterBuffer->GetSRV());
            context.SetDynamicDescriptor(2, 0, m_pLightIndexCounter->GetUAV());
            context.SetDynamicDescriptor(2, 1, m_pLightIndexGrid->GetUAV());
            context.SetDynamicDescriptor(2, 2, m_pLightGrid->GetUAV());

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

            context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(inputResource.pAO, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(inputResource.pPreviousColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(inputResource.pResolvedDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(inputResource.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
            context.InsertResourceBarrier(inputResource.pNormals, D3D12_RESOURCE_STATE_RENDER_TARGET);

            RenderPassInfo renderPass;
            renderPass.DepthStencilTarget.Access = RenderPassAccess::Load_DontCare;
            renderPass.DepthStencilTarget.StencilAccess = RenderPassAccess::DontCare_DontCare;
            renderPass.DepthStencilTarget.Target = inputResource.pDepthBuffer;
            renderPass.RenderTargetCount = 2;
            renderPass.RenderTargets[0].Access = RenderPassAccess::Clear_Store;
            renderPass.RenderTargets[0].Target = inputResource.pRenderTarget;
            renderPass.RenderTargets[1].Access = RenderPassAccess::Clear_Resolve;
            renderPass.RenderTargets[1].Target = inputResource.pNormals;
            renderPass.RenderTargets[1].ResolveTarget = inputResource.pResolvedNormals;

            context.BeginRenderPass(renderPass);

            context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context.SetGraphicsRootSignature(m_pDiffuseRS.get());

            context.SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
            context.SetDynamicConstantBufferView(2, inputResource.pShadowData, sizeof(ShadowData));
            context.SetDynamicDescriptors(3, 0, inputResource.MaterialTextures.data(), (int)inputResource.MaterialTextures.size());
            context.SetDynamicDescriptor(4, 0, m_pLightGrid->GetSRV());
            context.SetDynamicDescriptor(4, 1, m_pLightIndexGrid->GetSRV());
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
                        objectData.World = Matrix::Identity;
                        objectData.Material = b.Material;
                        context.SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));
                        b.pMesh->Draw(&context);
                    }
                }
			};

            {
                GPU_PROFILE_SCOPE("Opaque", &context);
                context.SetPipelineState(m_pDiffusePSO.get());
                DrawBatches(Batch::Blending::Opaque | Batch::Blending::AlphaMask);
            }

            {
                GPU_PROFILE_SCOPE("Transparent", &context);
                context.SetPipelineState(m_pDiffuseTransparencyPSO.get());
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

                context.BeginRenderPass(RenderPassInfo(inputResource.pRenderTarget, RenderPassAccess::Load_Store, inputResource.pDepthBuffer, RenderPassAccess::Load_DontCare));

                context.SetPipelineState(m_pDebugClusterPSO.get());
                context.SetGraphicsRootSignature(m_pDebugClusterRS.get());

                Matrix p = m_DebugClusterViewMatrix * inputResource.pCamera->GetViewProjection();
                context.SetDynamicConstantBufferView(0, &p, sizeof(Matrix));
                context.SetDynamicDescriptor(1, 0, m_pAabbBuffer->GetSRV());
                context.SetDynamicDescriptor(1, 1, m_pDebugCompactedClusterBuffer->GetSRV());
                context.SetDynamicDescriptor(1, 2, m_pDebugLightGrid->GetSRV());
                context.SetDynamicDescriptor(1, 3, m_pHeatMapTexture->GetSRV());

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

            context.SetPipelineState(m_pVisualizeLightsPSO.get());
            context.SetComputeRootSignature(m_pVisualizeLightsRS.get());

            context.SetComputeDynamicConstantBufferView(0, &constantData, sizeof(Data));

            context.SetDynamicDescriptor(1, 0, pTarget->GetSRV());
            context.SetDynamicDescriptor(1, 1, pDepth->GetSRV());
            context.SetDynamicDescriptor(1, 2, m_pLightGrid->GetSRV());
            
            context.SetDynamicDescriptor(2, 0, m_pVisualizeIntermediateTexture->GetUAV());

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
        Shader computeShader = Shader("ClusterAABBGeneration.hlsl", ShaderType::Compute, "GenerateAABBs");

        m_pCreateAabbRS = std::make_unique<RootSignature>(pGraphics);
        m_pCreateAabbRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        m_pCreateAabbRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pCreateAabbRS->Finalize("Create AABBs", D3D12_ROOT_SIGNATURE_FLAG_NONE);

        m_pCreateAabbPSO = std::make_unique<PipelineState>(pGraphics);
        m_pCreateAabbPSO->SetComputeShader(computeShader);
        m_pCreateAabbPSO->SetRootSignature(m_pCreateAabbRS->GetRootSignature());
        m_pCreateAabbPSO->Finalize("Create AABBs");
    }

    // mark unique clusters
    {
        CD3DX12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            CD3DX12_INPUT_ELEMENT_DESC("POSITION", DXGI_FORMAT_R32G32B32_FLOAT),
            CD3DX12_INPUT_ELEMENT_DESC("TEXCOORD", DXGI_FORMAT_R32G32_FLOAT),
        };

        Shader vertexShader = Shader("ClusterMarking.hlsl", ShaderType::Vertex, "MarkClusters_VS");
		Shader pixelShaderOpaque = Shader("ClusterMarking.hlsl", ShaderType::Pixel, "MarkClusters_PS");

        m_pMarkUniqueClustersRS = std::make_unique<RootSignature>(pGraphics);
        m_pMarkUniqueClustersRS->FinalizeFromShader("Mark Unique Clusters", vertexShader);

        m_pMarkUniqueClustersOpaquePSO = std::make_unique<PipelineState>(pGraphics);
        m_pMarkUniqueClustersOpaquePSO->SetInputLayout(inputElementDescs, ARRAYSIZE(inputElementDescs));
        m_pMarkUniqueClustersOpaquePSO->SetVertexShader(vertexShader);
        m_pMarkUniqueClustersOpaquePSO->SetPixelShader(pixelShaderOpaque);
        m_pMarkUniqueClustersOpaquePSO->SetBlendMode(BlendMode::Replace, false);
        m_pMarkUniqueClustersOpaquePSO->SetRootSignature(m_pMarkUniqueClustersRS->GetRootSignature());
        m_pMarkUniqueClustersOpaquePSO->SetRenderTargetFormats(nullptr, 0, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount());
        m_pMarkUniqueClustersOpaquePSO->SetDepthWrite(false);
		m_pMarkUniqueClustersOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);  // mark cluster must need GREATER_EQUAL on reverse-Z depth
        m_pMarkUniqueClustersOpaquePSO->Finalize("Mark Unique Clusters");

		m_pMarkUniqueClustersTransparentPSO = std::make_unique<PipelineState>(*m_pMarkUniqueClustersOpaquePSO);
		m_pMarkUniqueClustersTransparentPSO->Finalize("Mark Unique Clusters");
    }

    // compact cluster list
    {
        Shader computeShader = Shader("ClusterCompaction.hlsl", ShaderType::Compute, "CompactClusters");

        m_pCompactClusterListRS = std::make_unique<RootSignature>(pGraphics);
        m_pCompactClusterListRS->FinalizeFromShader("Compact Cluster List", computeShader);

        m_pCompactClusterListPSO = std::make_unique<PipelineState>(pGraphics);
        m_pCompactClusterListPSO->SetComputeShader(computeShader);
        m_pCompactClusterListPSO->SetRootSignature(m_pCompactClusterListRS->GetRootSignature());
        m_pCompactClusterListPSO->Finalize("Compact Cluster List");
    }

    // prepare indirect dispatch buffer
    {
        Shader computeShader = Shader("ClusterLightCullingArguments.hlsl", ShaderType::Compute, "UpdateIndirectArguments");

        m_pUpdateIndirectArgumentsRS = std::make_unique<RootSignature>(pGraphics);
        m_pUpdateIndirectArgumentsRS->FinalizeFromShader("Update Indirect Dispatch Buffer", computeShader);

        m_pUpdateIndirectArgumentsPSO = std::make_unique<PipelineState>(pGraphics);
        m_pUpdateIndirectArgumentsPSO->SetComputeShader(computeShader);
        m_pUpdateIndirectArgumentsPSO->SetRootSignature(m_pUpdateIndirectArgumentsRS->GetRootSignature());
        m_pUpdateIndirectArgumentsPSO->Finalize("Update Indirect Dispatch Buffer");
    }

    // light culling
    {
        Shader computeShader = Shader("ClusterLightCulling.hlsl", ShaderType::Compute, "LightCulling");

        m_pLightCullingRS = std::make_unique<RootSignature>(pGraphics);
        m_pLightCullingRS->FinalizeFromShader("Light Culling", computeShader);

        m_pLightCullingPSO = std::make_unique<PipelineState>(pGraphics);
        m_pLightCullingPSO->SetComputeShader(computeShader);
        m_pLightCullingPSO->SetRootSignature(m_pLightCullingRS->GetRootSignature());
        m_pLightCullingPSO->Finalize("Light Culling");

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

        Shader vertexShader = Shader("Diffuse.hlsl", ShaderType::Vertex, "VSMain", { "CLUSTERED_FORWARD" });
        Shader pixelShader = Shader("Diffuse.hlsl", ShaderType::Pixel, "PSMain", { "CLUSTERED_FORWARD" });

        m_pDiffuseRS = std::make_unique<RootSignature>(pGraphics);
        m_pDiffuseRS->FinalizeFromShader("Diffuse", vertexShader);

        DXGI_FORMAT formats[] = { Graphics::RENDER_TARGET_FORMAT, DXGI_FORMAT_R16G16B16A16_FLOAT };

        // opaque
        m_pDiffusePSO = std::make_unique<PipelineState>(pGraphics);
        m_pDiffusePSO->SetInputLayout(inputElementDescs, ARRAYSIZE(inputElementDescs));
        m_pDiffusePSO->SetVertexShader(vertexShader);
        m_pDiffusePSO->SetPixelShader(pixelShader);
        m_pDiffusePSO->SetBlendMode(BlendMode::Replace, false);
        m_pDiffusePSO->SetRootSignature(m_pDiffuseRS->GetRootSignature());
        m_pDiffusePSO->SetRenderTargetFormats(formats, std::size(formats), Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount());
        m_pDiffusePSO->SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
        m_pDiffusePSO->SetDepthWrite(false);
        m_pDiffusePSO->Finalize("Diffuse (Opaque)");

        // transparent
		m_pDiffuseTransparencyPSO = std::make_unique<PipelineState>(*m_pDiffusePSO.get());
		m_pDiffuseTransparencyPSO->SetBlendMode(BlendMode::Alpha, false);
		m_pDiffuseTransparencyPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pDiffuseTransparencyPSO->Finalize("Diffuse (Transparent)");
    }

    // cluster debug rendering
    {
        Shader pixelShader = Shader("ClusterDebugDrawing.hlsl", ShaderType::Pixel, "PSMain");

        m_pDebugClusterRS = std::make_unique<RootSignature>(pGraphics);
        m_pDebugClusterPSO = std::make_unique<PipelineState>(pGraphics);

        m_pDebugClusterPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
        m_pDebugClusterPSO->SetDepthWrite(false);
        m_pDebugClusterPSO->SetPixelShader(pixelShader);
        m_pDebugClusterPSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount());
        m_pDebugClusterPSO->SetBlendMode(BlendMode::Add, false);

        if (m_pGraphics->SupportMeshShaders())
        {
		    Shader meshShader = Shader("ClusterDebugDrawing.hlsl", ShaderType::Mesh, "MSMain");

		    m_pDebugClusterRS->FinalizeFromShader("Debug Cluster", meshShader);
            
            m_pDebugClusterPSO->SetRootSignature(m_pDebugClusterRS->GetRootSignature());
            m_pDebugClusterPSO->SetMeshShader(meshShader);
			m_pDebugClusterPSO->Finalize("Debug Cluster PSO");
        }
        else
        {
		    Shader vertexShader = Shader("ClusterDebugDrawing.hlsl", ShaderType::Vertex, "VSMain");
            Shader geometryShader = Shader("ClusterDebugDrawing.hlsl", ShaderType::Geometry, "GSMain");

		    m_pDebugClusterRS->FinalizeFromShader("Debug Cluster", vertexShader);

            m_pDebugClusterPSO->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
            m_pDebugClusterPSO->SetVertexShader(vertexShader);
            m_pDebugClusterPSO->SetGeometryShader(geometryShader);
            m_pDebugClusterPSO->SetRootSignature(m_pDebugClusterRS->GetRootSignature());
            m_pDebugClusterPSO->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
            m_pDebugClusterPSO->Finalize("Debug Cluster PSO");
        }
    }

    {
        Shader computeShader("VisualizeLightCount.hlsl", ShaderType::Compute, "DebugLightDensityCS", { "CLUSTERED_FORWARD" });

        m_pVisualizeLightsRS = std::make_unique<RootSignature>(pGraphics);
        m_pVisualizeLightsRS->FinalizeFromShader("Visualize Light Density RS", computeShader);

        m_pVisualizeLightsPSO = std::make_unique<PipelineState>(pGraphics);
        m_pVisualizeLightsPSO->SetRootSignature(m_pVisualizeLightsRS->GetRootSignature());
        m_pVisualizeLightsPSO->SetComputeShader(computeShader);
        m_pVisualizeLightsPSO->Finalize("Visualize Light Density PSO");
    }
}

