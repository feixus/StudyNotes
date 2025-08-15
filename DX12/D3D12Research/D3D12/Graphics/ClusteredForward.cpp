#include "stdafx.h"
#include "ClusteredForward.h"
#include "RenderGraph/RenderGraph.h"
#include "Scene/Camera.h"
#include "Core/CommandSignature.h"
#include "Core/Shader.h"
#include "Core/PipelineState.h"
#include "Core/RootSignature.h"
#include "Core/GraphicsBuffer.h"
#include "Core/GraphicsTexture.h"
#include "Core/CommandContext.h"
#include "Core/CommandQueue.h"
#include "Core/ResourceViews.h"
#include "Graphics/Mesh.h"
#include "Graphics/Light.h"
#include "Graphics/Profiler.h"

static constexpr int cClusterSize = 64;
static constexpr int cClusterCountZ = 32;

bool g_VisualizeClusters = false;

ClusteredForward::ClusteredForward(Graphics* pGraphics)
    : m_pGraphics(pGraphics)
{
    SetupResources(pGraphics);
    SetupPipelines(pGraphics);
}

ClusteredForward::~ClusteredForward()
{}

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

	CommandContext* pContext = m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	// create AABBs
	{
		GPU_PROFILE_SCOPE("CreateAABBs", pContext);

        pContext->InsertResourceBarrier(m_pAabbBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		pContext->SetPipelineState(m_pCreateAabbPSO.get());
		pContext->SetComputeRootSignature(m_pCreateAabbRS.get());

		struct ConstantBuffer
		{
			Matrix ProjectionInverse;
			Vector2 ScreenDimensionsInv;
			Vector2 ClusterSize;
			int ClusterDimensions[3]{};
			float NearZ{0};
			float FarZ{0};
		} constantBuffer;

		constantBuffer.ScreenDimensionsInv = Vector2(1.0f / windowWidth, 1.0f / windowHeight);
		constantBuffer.NearZ = m_pGraphics->GetCamera()->GetFar();
        constantBuffer.FarZ = m_pGraphics->GetCamera()->GetNear();
		constantBuffer.ProjectionInverse = m_pGraphics->GetCamera()->GetProjectionInverse();
		constantBuffer.ClusterSize.x = cClusterSize;
		constantBuffer.ClusterSize.y = cClusterSize;
		constantBuffer.ClusterDimensions[0] = m_ClusterCountX;
		constantBuffer.ClusterDimensions[1] = m_ClusterCountY;
		constantBuffer.ClusterDimensions[2] = cClusterCountZ;

		pContext->SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(constantBuffer));
		pContext->SetDynamicDescriptor(1, 0, m_pAabbBuffer->GetUAV());

		pContext->Dispatch(m_ClusterCountX, m_ClusterCountY, cClusterCountZ);
	}
	pContext->Execute(true);
}

void ClusteredForward::Execute(RGGraph& graph, const ClusteredForwardInputResource& inputResource)
{
    Vector2 screenDimensions((float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight());
    float nearZ = m_pGraphics->GetCamera()->GetNear();
    float farZ = m_pGraphics->GetCamera()->GetFar();

    float sliceMagicA = (float)cClusterCountZ / log(nearZ / farZ);
    float sliceMagicB = (float)cClusterCountZ * log(farZ) / log(nearZ / farZ);

    graph.AddPass("Mark Clusters", [&](RGPassBuilder& builder)
        {
            builder.Read(inputResource.DepthBuffer);
            return [=](CommandContext& context, const RGPassResource& passResources)
                {
                    GraphicsTexture* depthBuffer = passResources.GetTexture(inputResource.DepthBuffer);
					context.InsertResourceBarrier(inputResource.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
					context.InsertResourceBarrier(m_pUniqueClusterBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    context.InsertResourceBarrier(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);

                    context.ClearUavUInt(m_pUniqueClusterBuffer.get(), m_pUniqueClusterBufferRawUAV);

                    context.BeginRenderPass(RenderPassInfo(depthBuffer, RenderPassAccess::Load_DontCare, true));

                    context.SetPipelineState(m_pMarkUniqueClustersOpaquePSO.get());
                    context.SetGraphicsRootSignature(m_pMarkUniqueClustersRS.get());
                    context.SetViewport(FloatRect(0.0f, 0.0f, screenDimensions.x, screenDimensions.y));
                    context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

					struct PerFrameParameters
					{
						uint32_t ClusterDimensions[4]{};
						float ClusterSize[2]{};
						float SliceMagicA{ 0 };
						float SliceMagicB{ 0 };
					} perFrameParameters;

                    struct PerObjectParameters
                    {
                        Matrix WorldView;
                        Matrix WorldViewProjection;
                    } perObjectParameters;

					perFrameParameters.SliceMagicA = sliceMagicA;
					perFrameParameters.SliceMagicB = sliceMagicB;
					perFrameParameters.ClusterDimensions[0] = m_ClusterCountX;
					perFrameParameters.ClusterDimensions[1] = m_ClusterCountY;
					perFrameParameters.ClusterDimensions[2] = cClusterCountZ;
					perFrameParameters.ClusterDimensions[3] = 0;
					perFrameParameters.ClusterSize[0] = cClusterSize;
					perFrameParameters.ClusterSize[1] = cClusterSize;

                    context.SetDynamicConstantBufferView(1, &perFrameParameters, sizeof(perFrameParameters));
                    context.SetDynamicDescriptor(2, 0, m_pUniqueClusterBuffer->GetUAV());
					{
						GPU_PROFILE_SCOPE("Opaque", &context);

						for (const Batch& b : *inputResource.pOpaqueBatches)
						{
                            perObjectParameters.WorldView = b.WorldMatrix * inputResource.pCamera->GetView();
                            perObjectParameters.WorldViewProjection = b.WorldMatrix * inputResource.pCamera->GetViewProjection();

                            context.SetDynamicConstantBufferView(0, &perObjectParameters, sizeof(perObjectParameters));
							b.pMesh->Draw(&context);
						}
					}

					{
						GPU_PROFILE_SCOPE("Transparent", &context);

                        context.SetPipelineState(m_pMarkUniqueClustersTransparentPSO.get());
                        for (const Batch& b : *inputResource.pTransparentBatches)
                        {
                            perObjectParameters.WorldView = b.WorldMatrix * inputResource.pCamera->GetView();
                            perObjectParameters.WorldViewProjection = b.WorldMatrix * inputResource.pCamera->GetViewProjection();
                            context.SetDynamicConstantBufferView(0, &perObjectParameters, sizeof(perObjectParameters));

                            context.SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
							b.pMesh->Draw(&context);
						}
					}

                    context.EndRenderPass();
                };
        });

	graph.AddPass("Compact Clusters", [&](RGPassBuilder& builder)
		{
			return [=](CommandContext& context, const RGPassResource& passResources)
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

                    context.Dispatch(Math::RoundUp(m_MaxClusters / 64.f), 1, 1);
            };
        });

	graph.AddPass("Update Indirect Arguments", [&](RGPassBuilder& builder)
		{
			return [=](CommandContext& context, const RGPassResource& passResources)
            {
				UnorderedAccessView* pCompactedClusterUav = m_pCompactedClusterBuffer->GetUAV();
                context.InsertResourceBarrier(pCompactedClusterUav->GetCounter(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                context.InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                context.SetPipelineState(m_pUpdateIndirectArgumentsPSO.get());
                context.SetComputeRootSignature(m_pUpdateIndirectArgumentsRS.get());

                context.SetDynamicDescriptor(0, 0, m_pCompactedClusterBuffer->GetUAV()->GetCounter()->GetSRV());
                context.SetDynamicDescriptor(1, 0, m_pIndirectArguments->GetUAV());

                context.Dispatch(1, 1, 1);
            };
        });

    graph.AddPass("Clustered Light Culling", [&](RGPassBuilder& builder)
        {
            return[=](CommandContext& context, const RGPassResource& passResources)
            {
                    context.SetPipelineState(m_pLightCullingPSO.get());
                    context.SetComputeRootSignature(m_pLightCullingRS.get());

                    context.InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
                    context.InsertResourceBarrier(m_pCompactedClusterBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    context.InsertResourceBarrier(m_pAabbBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    context.InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                    context.ClearUavUInt(m_pLightGrid.get(), m_pLightGridRawUAV);
                    context.ClearUavUInt(m_pLightIndexCounter.get(), m_pLightIndexCounter->GetUAV());

                    struct ConstantBuffer
                    {
                        Matrix View;
                        int LightCount{ 0 };
                    } constantBuffer;

                    constantBuffer.View = inputResource.pCamera->GetView();
                    constantBuffer.LightCount = (int)inputResource.pLightBuffer->GetDesc().ElementCount;

                    context.SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));
                    context.SetDynamicDescriptor(1, 0, inputResource.pLightBuffer->GetSRV());
                    context.SetDynamicDescriptor(1, 1, m_pAabbBuffer->GetSRV());
                    context.SetDynamicDescriptor(1, 2, m_pCompactedClusterBuffer->GetSRV());
                    context.SetDynamicDescriptor(2, 0, m_pLightIndexCounter->GetUAV());
                    context.SetDynamicDescriptor(2, 1, m_pLightIndexGrid->GetUAV());
                    context.SetDynamicDescriptor(2, 2, m_pLightGrid->GetUAV());

                    context.ExecuteIndirect(m_pLightCullingCommandSignature->GetCommandSignature(), m_pIndirectArguments.get());

            };
        });
    
	graph.AddPass("Base Pass", [&](RGPassBuilder& builder)
		{
            builder.Read(inputResource.DepthBuffer);
			return[=](CommandContext& context, const RGPassResource& passResources)
            {
				struct PerObjectData
				{
					Matrix World;
                    Matrix WorldViewProjection;
				} objectData;

				struct PerFrameData
				{
					Matrix View;
					Matrix Projection;
					Matrix ViewInverse;
					uint32_t ClusterDimensions[4]{};
					Vector2 ScreenDimensions;
					float NearZ{ 0 };
					float FarZ{ 0 };
					float ClusterSize[2]{};
					float SliceMagicA{ 0 };
					float SliceMagicB{ 0 };
				} frameData;

				frameData.View = inputResource.pCamera->GetView();
				frameData.ViewInverse = inputResource.pCamera->GetViewInverse();
				frameData.Projection = inputResource.pCamera->GetProjection();
				frameData.ClusterDimensions[0] = m_ClusterCountX;
				frameData.ClusterDimensions[1] = m_ClusterCountY;
				frameData.ClusterDimensions[2] = cClusterCountZ;
				frameData.ClusterDimensions[3] = m_MaxClusters;
				frameData.ScreenDimensions = screenDimensions;
				frameData.NearZ = nearZ;
				frameData.FarZ = farZ;
				frameData.ClusterSize[0] = cClusterSize;
				frameData.ClusterSize[1] = cClusterSize;
				frameData.SliceMagicA = sliceMagicA;
				frameData.SliceMagicB = sliceMagicB;

                context.InsertResourceBarrier(inputResource.pShadowMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                context.InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                context.InsertResourceBarrier(inputResource.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
                context.InsertResourceBarrier(inputResource.pAO, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

                context.BeginRenderPass(RenderPassInfo(inputResource.pRenderTarget, RenderPassAccess::Clear_Store, passResources.GetTexture(inputResource.DepthBuffer), RenderPassAccess::Load_DontCare));
                context.SetViewport(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));

                context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                context.SetGraphicsRootSignature(m_pDiffuseRS.get());
                {
                    GPU_PROFILE_SCOPE("Opaque", &context);

                    context.SetPipelineState(m_pDiffusePSO.get());

                    context.SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
                    context.SetDynamicConstantBufferView(2, inputResource.pShadowData, sizeof(ShadowData));
                    context.SetDynamicDescriptor(4, 0, inputResource.pShadowMap->GetSRV());
                    context.SetDynamicDescriptor(4, 1, m_pLightGrid->GetSRV());
                    context.SetDynamicDescriptor(4, 2, m_pLightIndexGrid->GetSRV());
                    context.SetDynamicDescriptor(4, 3, inputResource.pLightBuffer->GetSRV());
                    context.SetDynamicDescriptor(4, 4, inputResource.pAO->GetSRV());

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
                    context.SetPipelineState(m_pDiffuseTransparencyPSO.get());

                    for (const Batch& b : *inputResource.pTransparentBatches)
                    {
                        objectData.World = Matrix::Identity;
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
 

    if (g_VisualizeClusters)
    {
		graph.AddPass("Visualize Clusters", [&](RGPassBuilder& builder)
			{
				return[=](CommandContext& context, const RGPassResource& passResources)
                {
					if (m_DidCopyDebugClusterData == false)
					{
                        context.CopyResource(m_pCompactedClusterBuffer.get(), m_pDebugCompactedClusterBuffer.get());
                        context.CopyResource(m_pLightGrid.get(), m_pDebugLightGrid.get());
                        m_DebugClusterViewMatrix = inputResource.pCamera->GetView();
                        m_DebugClusterViewMatrix.Invert(m_DebugClusterViewMatrix);
                        m_DidCopyDebugClusterData = true;
                    }

                    context.BeginRenderPass(RenderPassInfo(inputResource.pRenderTarget, RenderPassAccess::Load_Store, passResources.GetTexture(inputResource.DepthBuffer), RenderPassAccess::Load_DontCare));

                    context.SetPipelineState(m_pDebugClusterPSO.get());
                    context.SetGraphicsRootSignature(m_pDebugClusterRS.get());

                    context.SetViewport(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));
                    context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

                    Matrix p = m_DebugClusterViewMatrix * inputResource.pCamera->GetViewProjection();

                    context.SetDynamicConstantBufferView(0, &p, sizeof(Matrix));
                    context.SetDynamicDescriptor(1, 0, m_pAabbBuffer->GetSRV());
                    context.SetDynamicDescriptor(1, 1, m_pDebugCompactedClusterBuffer->GetSRV());
                    context.SetDynamicDescriptor(1, 2, m_pDebugLightGrid->GetSRV());
                    context.SetDynamicDescriptor(1, 3, m_pHeatMapTexture->GetSRV());
                    context.Draw(0, m_MaxClusters);

                    context.EndRenderPass();
                };
             });
    }
    else
    {
        m_DidCopyDebugClusterData = false;
    }
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

    CommandContext* pCopyContext = m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COPY);
    m_pHeatMapTexture = std::make_unique<GraphicsTexture>(pGraphics, "Heatmap texture");
    m_pHeatMapTexture->Create(pCopyContext, "Resources/textures/HeatMap.png");
    pCopyContext->Execute(true);
}

void ClusteredForward::SetupPipelines(Graphics* pGraphics)
{
    // AABB
    {
        Shader computeShader = Shader("Resources/Shaders/CL_GenerateAABBs.hlsl", Shader::Type::Compute, "GenerateAABBs");

        m_pCreateAabbRS = std::make_unique<RootSignature>();
        m_pCreateAabbRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        m_pCreateAabbRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pCreateAabbRS->Finalize("Create AABBs", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

        m_pCreateAabbPSO = std::make_unique<PipelineState>();
        m_pCreateAabbPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
        m_pCreateAabbPSO->SetRootSignature(m_pCreateAabbRS->GetRootSignature());
        m_pCreateAabbPSO->Finalize("Create AABBs", pGraphics->GetDevice());
    }

    // mark unique clusters
    {
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        Shader vertexShader = Shader("Resources/Shaders/CL_MarkUniqueClusters.hlsl", Shader::Type::Vertex, "MarkClusters_VS");
		Shader pixelShaderOpaque = Shader("Resources/Shaders/CL_MarkUniqueClusters.hlsl", Shader::Type::Pixel, "MarkClusters_PS");

        m_pMarkUniqueClustersRS = std::make_unique<RootSignature>();
        m_pMarkUniqueClustersRS->FinalizeFromShader("Mark Unique Clusters", vertexShader, pGraphics->GetDevice());

        m_pMarkUniqueClustersOpaquePSO = std::make_unique<PipelineState>();
        m_pMarkUniqueClustersOpaquePSO->SetInputLayout(inputElementDescs, ARRAYSIZE(inputElementDescs));
        m_pMarkUniqueClustersOpaquePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
        m_pMarkUniqueClustersOpaquePSO->SetPixelShader(pixelShaderOpaque.GetByteCode(), pixelShaderOpaque.GetByteCodeSize());
        m_pMarkUniqueClustersOpaquePSO->SetBlendMode(BlendMode::Replace, false);
        m_pMarkUniqueClustersOpaquePSO->SetRootSignature(m_pMarkUniqueClustersRS->GetRootSignature());
        m_pMarkUniqueClustersOpaquePSO->SetRenderTargetFormats(nullptr, 0, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
        m_pMarkUniqueClustersOpaquePSO->SetDepthWrite(false);
        m_pMarkUniqueClustersOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
        m_pMarkUniqueClustersOpaquePSO->Finalize("Mark Unique Clusters", pGraphics->GetDevice());

		m_pMarkUniqueClustersTransparentPSO = std::make_unique<PipelineState>(*m_pMarkUniqueClustersOpaquePSO);
		m_pMarkUniqueClustersTransparentPSO->Finalize("Mark Unique Clusters", pGraphics->GetDevice());
    }

    // compact cluster list
    {
        Shader computeShader = Shader("Resources/Shaders/CL_CompactClusters.hlsl", Shader::Type::Compute, "CompactClusters");

        m_pCompactClusterListRS = std::make_unique<RootSignature>();
        m_pCompactClusterListRS->FinalizeFromShader("Compact Cluster List", computeShader, pGraphics->GetDevice());

        m_pCompactClusterListPSO = std::make_unique<PipelineState>();
        m_pCompactClusterListPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
        m_pCompactClusterListPSO->SetRootSignature(m_pCompactClusterListRS->GetRootSignature());
        m_pCompactClusterListPSO->Finalize("Compact Cluster List", pGraphics->GetDevice());
    }

    // prepare indirect dispatch buffer
    {
        Shader computeShader = Shader("Resources/Shaders/CL_UpdateIndirectArguments.hlsl", Shader::Type::Compute, "UpdateIndirectArguments");

        m_pUpdateIndirectArgumentsRS = std::make_unique<RootSignature>();
        m_pUpdateIndirectArgumentsRS->FinalizeFromShader("Update Indirect Dispatch Buffer", computeShader, pGraphics->GetDevice());

        m_pUpdateIndirectArgumentsPSO = std::make_unique<PipelineState>();
        m_pUpdateIndirectArgumentsPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
        m_pUpdateIndirectArgumentsPSO->SetRootSignature(m_pUpdateIndirectArgumentsRS->GetRootSignature());
        m_pUpdateIndirectArgumentsPSO->Finalize("Update Indirect Dispatch Buffer", pGraphics->GetDevice());
    }

    // light culling
    {
        Shader computeShader = Shader("Resources/Shaders/CL_LightCulling.hlsl", Shader::Type::Compute, "LightCulling");

        m_pLightCullingRS = std::make_unique<RootSignature>();
        m_pLightCullingRS->FinalizeFromShader("Light Culling", computeShader, pGraphics->GetDevice());

        m_pLightCullingPSO = std::make_unique<PipelineState>();
        m_pLightCullingPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
        m_pLightCullingPSO->SetRootSignature(m_pLightCullingRS->GetRootSignature());
        m_pLightCullingPSO->Finalize("Light Culling", pGraphics->GetDevice());

        m_pLightCullingCommandSignature = std::make_unique<CommandSignature>();
        m_pLightCullingCommandSignature->AddDispatch();
        m_pLightCullingCommandSignature->Finalize("Light Culling Command Signature", pGraphics->GetDevice());
    }

    // diffuse
    {
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        Shader vertexShader = Shader("Resources/Shaders/CL_Diffuse.hlsl", Shader::Type::Vertex, "VSMain", { });
        Shader pixelShader = Shader("Resources/Shaders/CL_Diffuse.hlsl", Shader::Type::Pixel, "PSMain", { });

        m_pDiffuseRS = std::make_unique<RootSignature>();
        m_pDiffuseRS->FinalizeFromShader("Diffuse", vertexShader, pGraphics->GetDevice());

        // opaque
        m_pDiffusePSO = std::make_unique<PipelineState>();
        m_pDiffusePSO->SetInputLayout(inputElementDescs, ARRAYSIZE(inputElementDescs));
        m_pDiffusePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
        m_pDiffusePSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
        m_pDiffusePSO->SetBlendMode(BlendMode::Replace, false);
        m_pDiffusePSO->SetRootSignature(m_pDiffuseRS->GetRootSignature());
        m_pDiffusePSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
        m_pDiffusePSO->SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
        m_pDiffusePSO->SetDepthWrite(false);
        m_pDiffusePSO->Finalize("Diffuse (Opaque)", pGraphics->GetDevice());

        // transparent
		m_pDiffuseTransparencyPSO = std::make_unique<PipelineState>(*m_pDiffusePSO.get());
		m_pDiffuseTransparencyPSO->SetBlendMode(BlendMode::Alpha, false);
		m_pDiffuseTransparencyPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pDiffuseTransparencyPSO->Finalize("Diffuse (Transparent)", pGraphics->GetDevice());
    }

    // cluster debug rendering
    {
        Shader vertexShader = Shader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::Vertex, "VSMain");
        Shader geometryShader = Shader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::Geometry, "GSMain");
        Shader pixelShader = Shader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::Pixel, "PSMain");

        m_pDebugClusterRS = std::make_unique<RootSignature>();
        m_pDebugClusterRS->FinalizeFromShader("Debug Cluster", vertexShader, pGraphics->GetDevice());

        m_pDebugClusterPSO = std::make_unique<PipelineState>();
        m_pDebugClusterPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
        m_pDebugClusterPSO->SetDepthWrite(false);
        m_pDebugClusterPSO->SetInputLayout(nullptr, 0);
        m_pDebugClusterPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
        m_pDebugClusterPSO->SetGeometryShader(geometryShader.GetByteCode(), geometryShader.GetByteCodeSize());
        m_pDebugClusterPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
        m_pDebugClusterPSO->SetRootSignature(m_pDebugClusterRS->GetRootSignature());
        m_pDebugClusterPSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
        m_pDebugClusterPSO->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
        m_pDebugClusterPSO->SetBlendMode(BlendMode::Add, false);
        m_pDebugClusterPSO->Finalize("Debug Cluster PSO", pGraphics->GetDevice());
    }
}

