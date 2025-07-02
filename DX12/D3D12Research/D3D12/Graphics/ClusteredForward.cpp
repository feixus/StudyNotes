#include "stdafx.h"
#include "ClusteredForward.h"
#include "Graphics/CommandSignature.h"
#include "Graphics/Shader.h"
#include "Graphics/PipelineState.h"
#include "Graphics/RootSignature.h"
#include "Graphics/GraphicsBuffer.h"
#include "Graphics/GraphicsTexture.h"
#include "Graphics/CommandContext.h"
#include "Graphics/CommandQueue.h"
#include "Graphics/Mesh.h"
#include "Graphics/Light.h"
#include "Graphics/Profiler.h"
#include "Scene/Camera.h"
#include "GpuParticles.h"
#include "ResourceViews.h"

static constexpr int cClusterSize = 64;
static constexpr int cClusterCountZ = 32;

bool gUseAlternativeLightCulling = false;
bool gVisualizeClusters = false;

float tFovAngle = Math::ToRadians * 60.0f;

ClusteredForward::ClusteredForward(Graphics* pGraphics)
    : m_pGraphics(pGraphics)
{
    SetupResources(pGraphics);
    SetupPipelines(pGraphics);

    m_pGpuParticles = std::make_unique<GpuParticles>(pGraphics);
    m_pGpuParticles->Initialize();
}

ClusteredForward::~ClusteredForward()
{
}

void ClusteredForward::OnSwapchainCreated(int windowWidth, int windowHeight)
{
    m_pDepthTexture->Create(TextureDesc::CreateDepth(windowWidth, windowHeight, Graphics::DEPTH_STENCIL_FORMAT, TextureFlag::DepthStencil, m_pGraphics->GetMultiSampleCount(), ClearBinding(0.0f, 0)));

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

		pContext->SetComputePipelineState(m_pCreateAabbPSO.get());
		pContext->SetComputeRootSignature(m_pCreateAabbRS.get());

		struct ConstantBuffer
		{
			Matrix ProjectionInverse;
			Vector2 ScreenDimensions;
			Vector2 ClusterSize;
			int ClusterDimensions[3]{};
			float NearZ{0};
			float FarZ{0};
		} constantBuffer;

		constantBuffer.ScreenDimensions = Vector2((float)windowWidth, (float)windowHeight);
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

void ClusteredForward::Execute(CommandContext* pContext, const ClusteredForwardInputResource& inputResource)
{
    GPU_PROFILE_SCOPE("ClusteredForward", pContext);

    Vector2 screenDimensions((float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight());
    float nearZ = m_pGraphics->GetCamera()->GetNear();
    float farZ = m_pGraphics->GetCamera()->GetFar();

    float sliceMagicA = (float)cClusterCountZ / log(nearZ / farZ);
    float sliceMagicB = (float)cClusterCountZ * log(farZ) / log(nearZ / farZ);

    // mark unique clusters
    {
        GPU_PROFILE_SCOPE("MarkUniqueClusters", pContext);

        pContext->InsertResourceBarrier(m_pDepthTexture.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
        pContext->InsertResourceBarrier(inputResource.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
        pContext->InsertResourceBarrier(m_pUniqueClusterBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        pContext->InsertResourceBarrier(m_pCompactedClusterBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        pContext->InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        pContext->ClearUavUInt(m_pUniqueClusterBuffer.get(), m_pUniqueClusterBufferRawUAV);
        pContext->ClearUavUInt(m_pCompactedClusterBuffer.get(), m_pCompactedClusterBufferRawUAV);
        pContext->ClearUavUInt(m_pLightGrid.get(), m_pLightGridRawUAV);

        pContext->BeginRenderPass(RenderPassInfo(m_pDepthTexture.get(), RenderPassAccess::Clear_Store, true));

        pContext->SetGraphicsPipelineState(m_pMarkUniqueClustersOpaquePSO.get());
        pContext->SetGraphicsRootSignature(m_pMarkUniqueClustersRS.get());
        pContext->SetViewport(FloatRect(0.0f, 0.0f, screenDimensions.x, screenDimensions.y));
        pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        
        struct ConstantBuffer
        {
            Matrix WorldView;
            Matrix Projection;
            uint32_t ClusterDimensions[4]{};
            float ClusterSize[2]{};
            float SliceMagicA{0};
            float SliceMagicB{0};
        } constantBuffer;

        constantBuffer.WorldView = inputResource.pCamera->GetView();
        constantBuffer.Projection = inputResource.pCamera->GetProjection();
        constantBuffer.SliceMagicA = sliceMagicA;
        constantBuffer.SliceMagicB = sliceMagicB;
        constantBuffer.ClusterDimensions[0] = m_ClusterCountX;
        constantBuffer.ClusterDimensions[1] = m_ClusterCountY;
        constantBuffer.ClusterDimensions[2] = cClusterCountZ;
        constantBuffer.ClusterDimensions[3] = 0;
        constantBuffer.ClusterSize[0] = cClusterSize;
        constantBuffer.ClusterSize[1] = cClusterSize;
        
        {
            GPU_PROFILE_SCOPE("Opaque", pContext);
            pContext->SetDynamicConstantBufferView(0, &constantBuffer, sizeof(constantBuffer));
            pContext->SetDynamicDescriptor(1, 0, m_pUniqueClusterBuffer->GetUAV());

            for (const Batch& b : *inputResource.pOpaqueBatches)
            {
                b.pMesh->Draw(pContext);
            }
        }

        {
			GPU_PROFILE_SCOPE("Transparent", pContext);
			
            pContext->SetGraphicsPipelineState(m_pMarkUniqueClustersTransparentPSO.get());
			for (const Batch& b : *inputResource.pTransparentBatches)
			{
                pContext->SetDynamicDescriptor(2, 0, b.pMaterial->pDiffuseTexture->GetSRV());
				b.pMesh->Draw(pContext);
			}
        }

        pContext->EndRenderPass();
    }
    
    {
        // compact clusters
        {
            GPU_PROFILE_SCOPE("CompactClusters", pContext);

            pContext->SetComputePipelineState(m_pCompactClusterListPSO.get());
            pContext->SetComputeRootSignature(m_pCompactClusterListRS.get());

            UnorderedAccessView* pCompactedClusterUav = m_pCompactedClusterBuffer->GetUAV();
            pContext->InsertResourceBarrier(m_pUniqueClusterBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            pContext->InsertResourceBarrier(pCompactedClusterUav->GetCounter(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            pContext->ClearUavUInt(pCompactedClusterUav->GetCounter(), pCompactedClusterUav->GetCounterUAV());

            pContext->SetDynamicDescriptor(0, 0, m_pUniqueClusterBuffer->GetSRV());
            pContext->SetDynamicDescriptor(1, 0, m_pCompactedClusterBuffer->GetUAV());

            pContext->Dispatch(Math::RoundUp(m_MaxClusters / 64.f), 1, 1);
        }

        // update indirect arguments
        {
            GPU_PROFILE_SCOPE("UpdateIndirectArguments", pContext);

            UnorderedAccessView* pCompactedClusterUav = m_pCompactedClusterBuffer->GetUAV();
            pContext->InsertResourceBarrier(pCompactedClusterUav->GetCounter(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            pContext->InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            pContext->SetComputePipelineState(m_pUpdateIndirectArgumentsPSO.get());
		    pContext->SetComputeRootSignature(m_pUpdateIndirectArgumentsRS.get());

            pContext->SetDynamicDescriptor(0, 0, m_pCompactedClusterBuffer->GetUAV()->GetCounter()->GetSRV());
		    pContext->SetDynamicDescriptor(1, 0, m_pIndirectArguments->GetUAV());

            pContext->Dispatch(1, 1, 1);
        }

        // light culling
        if (gUseAlternativeLightCulling)
        {
			GPU_PROFILE_SCOPE("AlternativeLightCulling", pContext);

			pContext->SetComputePipelineState(m_pAlternativeLightCullingPSO.get());
			pContext->SetComputeRootSignature(m_pLightCullingRS.get());

			pContext->InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            pContext->InsertResourceBarrier(m_pCompactedClusterBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            pContext->InsertResourceBarrier(m_pAabbBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            pContext->InsertResourceBarrier(m_pLightIndexCounter.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            pContext->ClearUavUInt(m_pLightIndexCounter.get(), m_pLightIndexCounter->GetUAV());
            pContext->ClearUavUInt(m_pLightGrid.get(), m_pLightGrid->GetUAV());

			struct ConstantBuffer
			{
				Matrix View;
                uint32_t ClusterDimensions[3]{};
                int LightCount{0};
			} constantBuffer;

            constantBuffer.View = inputResource.pCamera->GetView();
			constantBuffer.ClusterDimensions[0] = m_ClusterCountX;
			constantBuffer.ClusterDimensions[1] = m_ClusterCountY;
			constantBuffer.ClusterDimensions[2] = cClusterCountZ;
            constantBuffer.LightCount = (int)inputResource.pLightBuffer->GetDesc().ElementCount;

			pContext->SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));
			pContext->SetDynamicDescriptor(1, 0, inputResource.pLightBuffer->GetSRV());
			pContext->SetDynamicDescriptor(1, 1, m_pAabbBuffer->GetSRV());
			pContext->SetDynamicDescriptor(1, 2, m_pCompactedClusterBuffer->GetSRV());
			pContext->SetDynamicDescriptor(2, 0, m_pLightIndexCounter->GetUAV());
			pContext->SetDynamicDescriptor(2, 1, m_pLightIndexGrid->GetUAV());
			pContext->SetDynamicDescriptor(2, 2, m_pLightGrid->GetUAV());

            pContext->Dispatch(Math::RoundUp(m_ClusterCountX / 4.f), Math::RoundUp(m_ClusterCountY / 4.f), Math::RoundUp(cClusterCountZ / 4.f));
        }
        else
        {
            GPU_PROFILE_SCOPE("LightCulling", pContext);

            pContext->SetComputePipelineState(m_pLightCullingPSO.get());
            pContext->SetComputeRootSignature(m_pLightCullingRS.get());

			pContext->InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			pContext->InsertResourceBarrier(m_pCompactedClusterBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			pContext->InsertResourceBarrier(m_pAabbBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			pContext->InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			pContext->InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            pContext->ClearUavUInt(m_pLightIndexCounter.get(), m_pLightIndexCounter->GetUAV());

            struct ConstantBuffer
            {
                Matrix View;
                int LightCount{0};
            } constantBuffer;

            constantBuffer.View = inputResource.pCamera->GetView();
            constantBuffer.LightCount = (int)inputResource.pLightBuffer->GetDesc().ElementCount;

            pContext->SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));
            pContext->SetDynamicDescriptor(1, 0, inputResource.pLightBuffer->GetSRV());
            pContext->SetDynamicDescriptor(1, 1, m_pAabbBuffer->GetSRV());
            pContext->SetDynamicDescriptor(1, 2, m_pCompactedClusterBuffer->GetSRV());
            pContext->SetDynamicDescriptor(2, 0, m_pLightIndexCounter->GetUAV());
            pContext->SetDynamicDescriptor(2, 1, m_pLightIndexGrid->GetUAV());
            pContext->SetDynamicDescriptor(2, 2, m_pLightGrid->GetUAV());

            pContext->ExecuteIndirect(m_pLightCullingCommandSignature->GetCommandSignature(), m_pIndirectArguments.get());
        }
    }

    // base pass
    {        
        struct PerObjectData
        {
            Matrix World;
		} objectData;

        struct PerFrameData
        {
			Matrix View;
			Matrix Projection;
            Matrix ViewInverse;
            uint32_t ClusterDimensions[4]{};
            Vector2 ScreenDimensions;
            float NearZ{0};
            float FarZ{0};
            float ClusterSize[2]{};
            float SliceMagicA{0};
            float SliceMagicB{0};
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

        GPU_PROFILE_SCOPE("LightingPass", pContext);

        pContext->InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        pContext->InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        pContext->InsertResourceBarrier(inputResource.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
        pContext->InsertResourceBarrier(m_pDepthTexture.get(), D3D12_RESOURCE_STATE_DEPTH_READ);

        pContext->BeginRenderPass(RenderPassInfo(inputResource.pRenderTarget, RenderPassAccess::Clear_Store, m_pDepthTexture.get(), RenderPassAccess::Load_DontCare));
        pContext->SetViewport(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));

		pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pContext->SetGraphicsRootSignature(m_pDiffuseRS.get());
        {
            GPU_PROFILE_SCOPE("Opaque", pContext);
			
            pContext->SetGraphicsPipelineState(m_pDiffusePSO.get());

			pContext->SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
			pContext->SetDynamicDescriptor(3, 0, m_pLightGrid->GetSRV());
			pContext->SetDynamicDescriptor(3, 1, m_pLightIndexGrid->GetSRV());
			pContext->SetDynamicDescriptor(3, 2, inputResource.pLightBuffer->GetSRV());

		    for (const Batch& b : *inputResource.pOpaqueBatches)
		    {
                objectData.World = XMMatrixIdentity();
		        pContext->SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));

			    pContext->SetDynamicDescriptor(2, 0, b.pMaterial->pDiffuseTexture->GetSRV());
			    pContext->SetDynamicDescriptor(2, 1, b.pMaterial->pNormalTexture->GetSRV());
			    pContext->SetDynamicDescriptor(2, 2, b.pMaterial->pSpecularTexture->GetSRV());
			    b.pMesh->Draw(pContext);
		    }
        }

        {
            GPU_PROFILE_SCOPE("Transparent", pContext);
            pContext->SetGraphicsPipelineState(m_pDiffuseTransparencyPSO.get());

			for (const Batch& b : *inputResource.pTransparentBatches)
			{
				objectData.World = XMMatrixIdentity();
				pContext->SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));

				pContext->SetDynamicDescriptor(2, 0, b.pMaterial->pDiffuseTexture->GetSRV());
				pContext->SetDynamicDescriptor(2, 1, b.pMaterial->pNormalTexture->GetSRV());
				pContext->SetDynamicDescriptor(2, 2, b.pMaterial->pSpecularTexture->GetSRV());
				b.pMesh->Draw(pContext);
			}
        }

        pContext->EndRenderPass();
	}

    if (gVisualizeClusters)
    {
        GPU_PROFILE_SCOPE("VisualizeClusters", pContext);

        if (m_DidCopyDebugClusterData == false)
        {
            pContext->CopyResource(m_pCompactedClusterBuffer.get(), m_pDebugCompactedClusterBuffer.get());
            pContext->CopyResource(m_pLightGrid.get(), m_pDebugLightGrid.get());
            m_DebugClusterViewMatrix = inputResource.pCamera->GetView();
            m_DebugClusterViewMatrix.Invert(m_DebugClusterViewMatrix);
            pContext->ExecuteAndReset(true);
            m_DidCopyDebugClusterData = true;
        }

        pContext->BeginRenderPass(RenderPassInfo(inputResource.pRenderTarget, RenderPassAccess::Load_Store, m_pDepthTexture.get(), RenderPassAccess::Load_DontCare));

        pContext->SetGraphicsPipelineState(m_pDebugClusterPSO.get());
        pContext->SetGraphicsRootSignature(m_pDebugClusterRS.get());

        pContext->SetViewport(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));
        pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

        Matrix p = m_DebugClusterViewMatrix * inputResource.pCamera->GetViewProjection();

        pContext->SetDynamicConstantBufferView(0, &p, sizeof(Matrix));
        pContext->SetDynamicDescriptor(1, 0, m_pAabbBuffer->GetSRV());
        pContext->SetDynamicDescriptor(1, 1, m_pDebugCompactedClusterBuffer->GetSRV());
        pContext->SetDynamicDescriptor(1, 2, m_pDebugLightGrid->GetSRV());
        pContext->SetDynamicDescriptor(1, 3, m_pHeatMapTexture->GetSRV());
        pContext->Draw(0, m_MaxClusters);

        pContext->EndRenderPass();
    }
    else
    {
        m_DidCopyDebugClusterData = false;
    }

    m_pGpuParticles->Simulate();
}

void ClusteredForward::SetupResources(Graphics* pGraphics)
{
    m_pDepthTexture = std::make_unique<GraphicsTexture>(pGraphics, "Depth Texture");
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
        Shader computeShader = Shader("Resources/Shaders/CL_GenerateAABBs.hlsl", Shader::Type::ComputeShader, "GenerateAABBs");

        m_pCreateAabbRS = std::make_unique<RootSignature>();
        m_pCreateAabbRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        m_pCreateAabbRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pCreateAabbRS->Finalize("Create AABBs", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

        m_pCreateAabbPSO = std::make_unique<ComputePipelineState>();
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

        Shader vertexShader = Shader("Resources/Shaders/CL_MarkUniqueClusters.hlsl", Shader::Type::VertexShader, "MarkClusters_VS");
		Shader pixelShaderOpaque = Shader("Resources/Shaders/CL_MarkUniqueClusters.hlsl", Shader::Type::PixelShader, "MarkClusters_PS");
		Shader pixelShaderTransparent = Shader("Resources/Shaders/CL_MarkUniqueClusters.hlsl", Shader::Type::PixelShader, "MarkClusters_PS", {"ALPHA_BLEND"});

        m_pMarkUniqueClustersRS = std::make_unique<RootSignature>();
        m_pMarkUniqueClustersRS->FinalizeFromShader("Mark Unique Clusters", vertexShader, pGraphics->GetDevice());

        m_pMarkUniqueClustersOpaquePSO = std::make_unique<GraphicsPipelineState>();
        m_pMarkUniqueClustersOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
        m_pMarkUniqueClustersOpaquePSO->SetInputLayout(inputElementDescs, ARRAYSIZE(inputElementDescs));
        m_pMarkUniqueClustersOpaquePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
        m_pMarkUniqueClustersOpaquePSO->SetPixelShader(pixelShaderOpaque.GetByteCode(), pixelShaderOpaque.GetByteCodeSize());
        m_pMarkUniqueClustersOpaquePSO->SetBlendMode(BlendMode::Replace, false);
        m_pMarkUniqueClustersOpaquePSO->SetRootSignature(m_pMarkUniqueClustersRS->GetRootSignature());
        m_pMarkUniqueClustersOpaquePSO->SetRenderTargetFormats(nullptr, 0, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
        m_pMarkUniqueClustersOpaquePSO->Finalize("Mark Unique Clusters", pGraphics->GetDevice());

		m_pMarkUniqueClustersTransparentPSO = std::make_unique<GraphicsPipelineState>(*m_pMarkUniqueClustersOpaquePSO);
		m_pMarkUniqueClustersTransparentPSO->SetPixelShader(pixelShaderTransparent.GetByteCode(), pixelShaderTransparent.GetByteCodeSize());
		m_pMarkUniqueClustersTransparentPSO->SetBlendMode(BlendMode::Alpha, false);
        m_pMarkUniqueClustersTransparentPSO->SetDepthWrite(false);
		m_pMarkUniqueClustersTransparentPSO->Finalize("Mark Unique Clusters", pGraphics->GetDevice());
    }

    // compact cluster list
    {
        Shader computeShader = Shader("Resources/Shaders/CL_CompactClusters.hlsl", Shader::Type::ComputeShader, "CompactClusters");

        m_pCompactClusterListRS = std::make_unique<RootSignature>();
        m_pCompactClusterListRS->FinalizeFromShader("Compact Cluster List", computeShader, pGraphics->GetDevice());

        m_pCompactClusterListPSO = std::make_unique<ComputePipelineState>();
        m_pCompactClusterListPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
        m_pCompactClusterListPSO->SetRootSignature(m_pCompactClusterListRS->GetRootSignature());
        m_pCompactClusterListPSO->Finalize("Compact Cluster List", pGraphics->GetDevice());
    }

    // prepare indirect dispatch buffer
    {
        Shader computeShader = Shader("Resources/Shaders/CL_UpdateIndirectArguments.hlsl", Shader::Type::ComputeShader, "UpdateIndirectArguments");

        m_pUpdateIndirectArgumentsRS = std::make_unique<RootSignature>();
        m_pUpdateIndirectArgumentsRS->FinalizeFromShader("Update Indirect Dispatch Buffer", computeShader, pGraphics->GetDevice());

        m_pUpdateIndirectArgumentsPSO = std::make_unique<ComputePipelineState>();
        m_pUpdateIndirectArgumentsPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
        m_pUpdateIndirectArgumentsPSO->SetRootSignature(m_pUpdateIndirectArgumentsRS->GetRootSignature());
        m_pUpdateIndirectArgumentsPSO->Finalize("Update Indirect Dispatch Buffer", pGraphics->GetDevice());
    }

    // light culling
    {
        Shader computeShader = Shader("Resources/Shaders/CL_LightCulling.hlsl", Shader::Type::ComputeShader, "LightCulling");

        m_pLightCullingRS = std::make_unique<RootSignature>();
        m_pLightCullingRS->FinalizeFromShader("Light Culling", computeShader, pGraphics->GetDevice());

        m_pLightCullingPSO = std::make_unique<ComputePipelineState>();
        m_pLightCullingPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
        m_pLightCullingPSO->SetRootSignature(m_pLightCullingRS->GetRootSignature());
        m_pLightCullingPSO->Finalize("Light Culling", pGraphics->GetDevice());

        m_pLightCullingCommandSignature = std::make_unique<CommandSignature>();
        m_pLightCullingCommandSignature->AddDispatch();
        m_pLightCullingCommandSignature->Finalize("Light Culling Command Signature", pGraphics->GetDevice());
    }

    // alternative light culling
    {
        Shader computeShader = Shader("Resources/Shaders/CL_LightCullingUnreal.hlsl", Shader::Type::ComputeShader, "LightCulling");

        m_pAlternativeLightCullingPSO = std::make_unique<ComputePipelineState>();
        m_pAlternativeLightCullingPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
        m_pAlternativeLightCullingPSO->SetRootSignature(m_pLightCullingRS->GetRootSignature());
        m_pAlternativeLightCullingPSO->Finalize("Light Culling", pGraphics->GetDevice());
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

        Shader vertexShader = Shader("Resources/Shaders/CL_Diffuse.hlsl", Shader::Type::VertexShader, "VSMain");
        Shader pixelShader = Shader("Resources/Shaders/CL_Diffuse.hlsl", Shader::Type::PixelShader, "PSMain");

        m_pDiffuseRS = std::make_unique<RootSignature>();
        m_pDiffuseRS->FinalizeFromShader("Diffuse", vertexShader, pGraphics->GetDevice());

        // opaque
        m_pDiffusePSO = std::make_unique<GraphicsPipelineState>();
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
		m_pDiffuseTransparencyPSO = std::make_unique<GraphicsPipelineState>(*m_pDiffusePSO.get());
		m_pDiffuseTransparencyPSO->SetBlendMode(BlendMode::Alpha, false);
		m_pDiffuseTransparencyPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pDiffuseTransparencyPSO->Finalize("Diffuse (Transparent)", pGraphics->GetDevice());
    }

    // cluster debug rendering
    {
        Shader vertexShader = Shader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::VertexShader, "VSMain");
        Shader geometryShader = Shader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::GeometryShader, "GSMain");
        Shader pixelShader = Shader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::PixelShader, "PSMain");

        m_pDebugClusterRS = std::make_unique<RootSignature>();
        m_pDebugClusterRS->FinalizeFromShader("Debug Cluster", vertexShader, pGraphics->GetDevice());

        m_pDebugClusterPSO = std::make_unique<GraphicsPipelineState>();
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

