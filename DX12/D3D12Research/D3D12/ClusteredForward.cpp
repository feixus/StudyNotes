#include "stdafx.h"
#include "ClusteredForward.h"
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
}

void ClusteredForward::OnSwapchainCreated(int windowWidth, int windowHeight)
{
	m_ClusterCountX = (uint32_t)ceil((float)windowWidth / cClusterSize);
	m_ClusterCountY = (uint32_t)ceil((float)windowHeight / cClusterSize);
    m_MaxClusters = m_ClusterCountX * m_ClusterCountY * cClusterCountZ;

    struct AABB { Vector4 Min; Vector4 Max; };
    m_pAabbBuffer->Create(m_pGraphics, sizeof(AABB), m_MaxClusters, false);
    m_pAabbBuffer->SetName("AABBs");

    m_pUniqueClusterBuffer->Create(m_pGraphics, sizeof(uint32_t), m_MaxClusters, false);
    m_pUniqueClusterBuffer->SetName("Unique Clusters");

    m_pCompactedClusterBuffer->Create(m_pGraphics, sizeof(uint32_t), m_MaxClusters, false);
    m_pCompactedClusterBuffer->SetName("Compacted Cluster");

    m_pDebugCompactedClusterBuffer->Create(m_pGraphics, sizeof(uint32_t), m_MaxClusters, false);
    m_pDebugCompactedClusterBuffer->SetName("Debug Compacted Cluster");

    m_pLightIndexGrid->Create(m_pGraphics, sizeof(uint32_t), m_MaxClusters * 32);
    m_pLightIndexGrid->SetName("Light Index Grid");
    m_pLightGrid->Create(m_pGraphics, 2 * sizeof(uint32_t), m_MaxClusters);
    m_pLightGrid->SetName("Light Grid");
    m_pDebugLightGrid->Create(m_pGraphics, 2 * sizeof(uint32_t), m_MaxClusters);
    m_pDebugLightGrid->SetName("Debug Light Grid");

	float nearZ = 2.0f;
	float farZ = 500.0f;
	Matrix projection = XMMatrixPerspectiveFovLH(tFovAngle, (float)windowWidth / windowHeight, nearZ, farZ);

	// create AABBs
	{
		ComputeCommandContext* pContext = (ComputeCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE);
		Profiler::Instance()->Begin("Create AABBs", pContext);

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
		constantBuffer.NearZ = nearZ;
		constantBuffer.FarZ = farZ;
		projection.Invert(constantBuffer.ProjectionInverse);
		constantBuffer.ClusterSize.x = cClusterSize;
		constantBuffer.ClusterSize.y = cClusterSize;
		constantBuffer.ClusterDimensions[0] = m_ClusterCountX;
		constantBuffer.ClusterDimensions[1] = m_ClusterCountY;
		constantBuffer.ClusterDimensions[2] = cClusterCountZ;

		pContext->SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(constantBuffer));
		pContext->SetDynamicDescriptor(1, 0, m_pAabbBuffer->GetUAV());

		pContext->Dispatch(m_ClusterCountX, m_ClusterCountY, cClusterCountZ);

		Profiler::Instance()->End(pContext);
		pContext->Execute(true);
	}
}

void ClusteredForward::Execute(const ClusteredForwardInputResource& inputResource)
{
    Vector2 screenDimensions((float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight());
    float nearZ = 2.0f;
    float farZ = 500.0f;
    Matrix projection = XMMatrixPerspectiveFovLH(tFovAngle, screenDimensions.x / screenDimensions.y, nearZ, farZ);

    float sliceMagicA = (float)cClusterCountZ / log(farZ / nearZ);
    float sliceMagicB = (float)cClusterCountZ * log(nearZ) / log(farZ / nearZ);

    // mark unique clusters
    {
        GraphicsCommandContext* pContext = (GraphicsCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
        Profiler::Instance()->Begin("Mark Unique Clusters", pContext);

        pContext->InsertResourceBarrier(m_pUniqueClusterBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);
        pContext->InsertResourceBarrier(inputResource.pDepthPrepassBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
        pContext->ClearDepth(inputResource.pDepthPrepassBuffer->GetDSV(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

        Profiler::Instance()->Begin("Update Data", pContext);
        std::vector<uint32_t> zero(m_MaxClusters);
        m_pUniqueClusterBuffer->SetData(pContext, zero.data(), sizeof(uint32_t) * zero.size());
        Profiler::Instance()->End(pContext);

        pContext->SetGraphicsRootSignature(m_pMarkUniqueClustersRS.get());
        pContext->SetGraphicsPipelineState(m_pMarkUniqueClustersOpaquePSO.get());
        pContext->SetViewport(FloatRect(0.0f, 0.0f, screenDimensions.x, screenDimensions.y));
        pContext->SetScissorRect(FloatRect(0, 0, screenDimensions.x, screenDimensions.y));
        pContext->SetRenderTargets(nullptr, inputResource.pDepthPrepassBuffer->GetDSV());
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

        constantBuffer.WorldView = m_pGraphics->GetViewMatrix();
        constantBuffer.Projection = projection;
        constantBuffer.SliceMagicA = sliceMagicA;
        constantBuffer.SliceMagicB = sliceMagicB;
        constantBuffer.ClusterDimensions[0] = m_ClusterCountX;
        constantBuffer.ClusterDimensions[1] = m_ClusterCountY;
        constantBuffer.ClusterDimensions[2] = cClusterCountZ;
        constantBuffer.ClusterDimensions[3] = 0;
        constantBuffer.ClusterSize[0] = cClusterSize;
        constantBuffer.ClusterSize[1] = cClusterSize;
        
        {
            Profiler::Instance()->Begin("Opaque", pContext);
            pContext->SetDynamicConstantBufferView(0, &constantBuffer, sizeof(constantBuffer));
            pContext->SetDynamicDescriptor(1, 0, m_pUniqueClusterBuffer->GetUAV());

            for (const Batch& b : *inputResource.pOpaqueBatches)
            {
                b.pMesh->Draw(pContext);
            }

            Profiler::Instance()->End(pContext);
        }

        {
			Profiler::Instance()->Begin("Transparent", pContext);
			
            pContext->SetGraphicsPipelineState(m_pMarkUniqueClustersTransparentPSO.get());
			for (const Batch& b : *inputResource.pTransparentBatches)
			{
                pContext->SetDynamicDescriptor(2, 0, b.pMaterial->pDiffuseTexture->GetSRV());
				b.pMesh->Draw(pContext);
			}

			Profiler::Instance()->End(pContext);
        }

        Profiler::Instance()->End(pContext);
        uint64_t fence = pContext->Execute(false);

        //m_pGraphics->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE)->InsertWaitForFence(fence);
    }
    
    {
        ComputeCommandContext* pContext = (ComputeCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
        // compact clusters
        {
            Profiler::Instance()->Begin("Compact Clusters", pContext);

            pContext->SetComputePipelineState(m_pCompactClusterListPSO.get());
            pContext->SetComputeRootSignature(m_pCompactClusterListRS.get());

            uint32_t values[] = {0, 0, 0, 0};
            pContext->ClearUavUInt(m_pCompactedClusterBuffer->GetCounter(), values);

            pContext->SetDynamicDescriptor(0, 0, m_pUniqueClusterBuffer->GetSRV());
            pContext->SetDynamicDescriptor(1, 0, m_pCompactedClusterBuffer->GetUAV());

            pContext->Dispatch((int)ceil(m_MaxClusters / 64.f), 1, 1);
        
            Profiler::Instance()->End(pContext);
            pContext->ExecuteAndReset(false);
        }

        // update indirect arguments
        {
            Profiler::Instance()->Begin("Update Indirect Arguments", pContext);

            pContext->InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

            pContext->SetComputePipelineState(m_pUpdateIndirectArgumentsPSO.get());
		    pContext->SetComputeRootSignature(m_pUpdateIndirectArgumentsRS.get());

            pContext->SetDynamicDescriptor(0, 0, m_pCompactedClusterBuffer->GetCounter()->GetSRV());
		    pContext->SetDynamicDescriptor(1, 0, m_pIndirectArguments->GetUAV());

            pContext->Dispatch(1, 1, 1);
            Profiler::Instance()->End(pContext);
            pContext->ExecuteAndReset(false);
        }

        // light culling
        if (gUseAlternativeLightCulling)
        {
			Profiler::Instance()->Begin("Alternative Light Culling", pContext);

			pContext->SetComputePipelineState(m_pAlternativeLightCullingPSO.get());
			pContext->SetComputeRootSignature(m_pLightCullingRS.get());

			pContext->InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, false);
            pContext->InsertResourceBarrier(m_pCompactedClusterBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
            pContext->InsertResourceBarrier(m_pAabbBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);

            Profiler::Instance()->Begin("Set Data", pContext);
			uint32_t zero = 0;
			m_pLightIndexCounter->SetData(pContext, &zero, sizeof(uint32_t));
            std::vector<char> zeros(2 * sizeof(uint32_t) * m_MaxClusters);
            m_pLightGrid->SetData(pContext, zeros.data(), sizeof(char) * zeros.size());
            Profiler::Instance()->End(pContext);

			struct ConstantBuffer
			{
				Matrix View;
                uint32_t ClusterDimensions[3]{};
                int LightCount{0};
			} constantBuffer;

            constantBuffer.View = m_pGraphics->GetViewMatrix();
			constantBuffer.ClusterDimensions[0] = m_ClusterCountX;
			constantBuffer.ClusterDimensions[1] = m_ClusterCountY;
			constantBuffer.ClusterDimensions[2] = cClusterCountZ;
            constantBuffer.LightCount = (int)inputResource.pLightBuffer->GetElementCount();

			pContext->SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));
			pContext->SetDynamicDescriptor(1, 0, inputResource.pLightBuffer->GetSRV());
			pContext->SetDynamicDescriptor(1, 1, m_pAabbBuffer->GetSRV());
			pContext->SetDynamicDescriptor(1, 2, m_pCompactedClusterBuffer->GetSRV());
			pContext->SetDynamicDescriptor(2, 0, m_pLightIndexCounter->GetUAV());
			pContext->SetDynamicDescriptor(2, 1, m_pLightIndexGrid->GetUAV());
			pContext->SetDynamicDescriptor(2, 2, m_pLightGrid->GetUAV());

            pContext->Dispatch((int)ceil((float)m_ClusterCountX / 4), (int)ceil((float)m_ClusterCountY / 4), (int)ceil((float)cClusterCountZ / 4));
            
            Profiler::Instance()->End(pContext);
            uint64_t fence = pContext->Execute(false);

            // m_pGraphics->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->InsertWaitForFence(fence);
        }
        else
        {
            Profiler::Instance()->Begin("Light Culling", pContext);

            pContext->SetComputePipelineState(m_pLightCullingPSO.get());
            pContext->SetComputeRootSignature(m_pLightCullingRS.get());

			pContext->InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, false);
			pContext->InsertResourceBarrier(m_pCompactedClusterBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
			pContext->InsertResourceBarrier(m_pAabbBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
			pContext->InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);
			pContext->InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

            uint32_t zero = 0;
            m_pLightIndexCounter->SetData(pContext, &zero, sizeof(uint32_t));

            struct ConstantBuffer
            {
                Matrix View;
                int LightCount{0};
            } constantBuffer;

            constantBuffer.View = m_pGraphics->GetViewMatrix();
            constantBuffer.LightCount = (int)inputResource.pLightBuffer->GetElementCount();

            pContext->SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));
            pContext->SetDynamicDescriptor(1, 0, inputResource.pLightBuffer->GetSRV());
            pContext->SetDynamicDescriptor(1, 1, m_pAabbBuffer->GetSRV());
            pContext->SetDynamicDescriptor(1, 2, m_pCompactedClusterBuffer->GetSRV());
            pContext->SetDynamicDescriptor(2, 0, m_pLightIndexCounter->GetUAV());
            pContext->SetDynamicDescriptor(2, 1, m_pLightIndexGrid->GetUAV());
            pContext->SetDynamicDescriptor(2, 2, m_pLightGrid->GetUAV());

            pContext->ExecuteIndirect(m_pLightCullingCommandSignature.Get(), m_pIndirectArguments.get());

            Profiler::Instance()->End(pContext);
            uint64_t fence = pContext->Execute(false);

            // m_pGraphics->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->InsertWaitForFence(fence);
        }
    }

    // base pass
    {
		GraphicsCommandContext* pContext = (GraphicsCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
        Profiler::Instance()->Begin("Lighting Pass", pContext);
        
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

        Matrix view = m_pGraphics->GetViewMatrix();
        view.Invert(frameData.ViewInverse);
        frameData.View = view;
        frameData.Projection = projection;
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

        {
            Profiler::Instance()->Begin("Opaque", pContext);
			
            pContext->SetGraphicsPipelineState(m_pDiffusePSO.get());
			pContext->SetGraphicsRootSignature(m_pDiffuseRS.get());

			pContext->SetViewport(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));
			pContext->SetScissorRect(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));

			pContext->InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
			pContext->InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
			pContext->InsertResourceBarrier(inputResource.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, true);

			pContext->SetRenderTarget(inputResource.pRenderTarget->GetRTV(), inputResource.pDepthPrepassBuffer->GetDSV());
			pContext->ClearRenderTarget(inputResource.pRenderTarget->GetRTV());

			pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			pContext->SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
			pContext->SetDynamicDescriptor(3, 0, m_pLightGrid->GetSRV());
			pContext->SetDynamicDescriptor(3, 1, m_pLightIndexGrid->GetSRV());
			pContext->SetDynamicDescriptor(3, 2, inputResource.pLightBuffer->GetSRV());
			pContext->SetDynamicDescriptor(4, 0, m_pHeatMapTexture->GetSRV());

		    for (const Batch& b : *inputResource.pOpaqueBatches)
		    {
                objectData.World = XMMatrixIdentity();
		        pContext->SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));

			    pContext->SetDynamicDescriptor(2, 0, b.pMaterial->pDiffuseTexture->GetSRV());
			    pContext->SetDynamicDescriptor(2, 1, b.pMaterial->pNormalTexture->GetSRV());
			    pContext->SetDynamicDescriptor(2, 2, b.pMaterial->pSpecularTexture->GetSRV());
			    b.pMesh->Draw(pContext);
		    }

            Profiler::Instance()->End(pContext);
        }

        {
            Profiler::Instance()->Begin("Transparent", pContext);
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

            Profiler::Instance()->End(pContext);
        }

        Profiler::Instance()->End(pContext);
        pContext->Execute(false);
	}

    if (gVisualizeClusters)
    {
        GraphicsCommandContext* pContext = (GraphicsCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);

        if (m_DidCopyDebugClusterData == false)
        {
            pContext->CopyResource(m_pCompactedClusterBuffer.get(), m_pDebugCompactedClusterBuffer.get());
            pContext->CopyResource(m_pLightGrid.get(), m_pDebugLightGrid.get());
            m_DebugClusterViewMatrix = m_pGraphics->GetViewMatrix();
            m_DebugClusterViewMatrix.Invert(m_DebugClusterViewMatrix);
            pContext->ExecuteAndReset(true);
            m_DidCopyDebugClusterData = true;
        }

        pContext->SetGraphicsPipelineState(m_pDebugClusterPSO.get());
        pContext->SetGraphicsRootSignature(m_pDebugClusterRS.get());

        pContext->SetViewport(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));
        pContext->SetScissorRect(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));
        pContext->SetRenderTarget(inputResource.pRenderTarget->GetRTV(), inputResource.pDepthPrepassBuffer->GetDSV());
        pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

        Matrix p = m_DebugClusterViewMatrix * m_pGraphics->GetViewMatrix() * projection;

        Profiler::Instance()->Begin("Cluster Visualization", pContext);
        pContext->SetDynamicConstantBufferView(0, &p, sizeof(Matrix));
        pContext->SetDynamicDescriptor(1, 0, m_pAabbBuffer->GetSRV());
        pContext->SetDynamicDescriptor(1, 1, m_pDebugCompactedClusterBuffer->GetSRV());
        pContext->SetDynamicDescriptor(1, 2, m_pDebugLightGrid->GetSRV());
        pContext->SetDynamicDescriptor(1, 3, m_pHeatMapTexture->GetSRV());
        pContext->Draw(0, m_MaxClusters);
        Profiler::Instance()->End(pContext);
        pContext->Execute(false);
    }
    else
    {
        m_DidCopyDebugClusterData = false;
    }
}

void ClusteredForward::SetupResources(Graphics* pGraphics)
{
    m_pAabbBuffer = std::make_unique<StructuredBuffer>(pGraphics);
    m_pUniqueClusterBuffer = std::make_unique<StructuredBuffer>(pGraphics);
    m_pCompactedClusterBuffer = std::make_unique<StructuredBuffer>(pGraphics);
    m_pDebugCompactedClusterBuffer = std::make_unique<StructuredBuffer>(pGraphics);
	m_pIndirectArguments = std::make_unique<ByteAddressBuffer>(pGraphics);
    m_pIndirectArguments->Create(m_pGraphics, sizeof(uint32_t), 3, false);

	m_pLightIndexCounter = std::make_unique<StructuredBuffer>(pGraphics);
	m_pLightIndexCounter->Create(m_pGraphics, sizeof(uint32_t), 1);
	m_pLightIndexGrid = std::make_unique<StructuredBuffer>(pGraphics);
	m_pLightGrid = std::make_unique<StructuredBuffer>(pGraphics);
    m_pDebugLightGrid = std::make_unique<StructuredBuffer>(pGraphics);

    CopyCommandContext* pCopyContext = (CopyCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COPY);
    m_pHeatMapTexture = std::make_unique<GraphicsTexture2D>();
    m_pHeatMapTexture->Create(pGraphics, pCopyContext, "Resources/textures/HeatMap.png", TextureUsage::ShaderResource);
    m_pHeatMapTexture->SetName("Heatmap texture");
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
        m_pMarkUniqueClustersRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pMarkUniqueClustersRS->SetDescriptorTableSimple(1, 1, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pMarkUniqueClustersRS->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);
        
		D3D12_SAMPLER_DESC samplerDesc{};
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        m_pMarkUniqueClustersRS->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        
        m_pMarkUniqueClustersRS->Finalize("Mark Unique Clusters", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        m_pMarkUniqueClustersOpaquePSO = std::make_unique<GraphicsPipelineState>();
        m_pMarkUniqueClustersOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_LESS_EQUAL);
        m_pMarkUniqueClustersOpaquePSO->SetInputLayout(inputElementDescs, _countof(inputElementDescs));
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
        m_pCompactClusterListRS->SetDescriptorTableSimple(0, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pCompactClusterListRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pCompactClusterListRS->Finalize("Compact Cluster List", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

        m_pCompactClusterListPSO = std::make_unique<ComputePipelineState>();
        m_pCompactClusterListPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
        m_pCompactClusterListPSO->SetRootSignature(m_pCompactClusterListRS->GetRootSignature());
        m_pCompactClusterListPSO->Finalize("Compact Cluster List", pGraphics->GetDevice());
    }

    // prepare indirect dispatch buffer
    {
        Shader computeShader = Shader("Resources/Shaders/CL_UpdateIndirectArguments.hlsl", Shader::Type::ComputeShader, "UpdateIndirectArguments");

        m_pUpdateIndirectArgumentsRS = std::make_unique<RootSignature>();
        m_pUpdateIndirectArgumentsRS->SetDescriptorTableSimple(0, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pUpdateIndirectArgumentsRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pUpdateIndirectArgumentsRS->Finalize("Update Indirect Dispatch Buffer", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

        m_pUpdateIndirectArgumentsPSO = std::make_unique<ComputePipelineState>();
        m_pUpdateIndirectArgumentsPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
        m_pUpdateIndirectArgumentsPSO->SetRootSignature(m_pUpdateIndirectArgumentsRS->GetRootSignature());
        m_pUpdateIndirectArgumentsPSO->Finalize("Update Indirect Dispatch Buffer", pGraphics->GetDevice());
    }

    // light culling
    {
        Shader computeShader = Shader("Resources/Shaders/CL_LightCulling.hlsl", Shader::Type::ComputeShader, "LightCulling");

        m_pLightCullingRS = std::make_unique<RootSignature>();
        m_pLightCullingRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        m_pLightCullingRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3D12_SHADER_VISIBILITY_ALL);
        m_pLightCullingRS->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, D3D12_SHADER_VISIBILITY_ALL);
        m_pLightCullingRS->Finalize("Light Culling", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

        m_pLightCullingPSO = std::make_unique<ComputePipelineState>();
        m_pLightCullingPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
        m_pLightCullingPSO->SetRootSignature(m_pLightCullingRS->GetRootSignature());
        m_pLightCullingPSO->Finalize("Light Culling", pGraphics->GetDevice());

        D3D12_INDIRECT_ARGUMENT_DESC desc{};
        desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        D3D12_COMMAND_SIGNATURE_DESC sigDesc{};
        sigDesc.ByteStride = 3 * sizeof(uint32_t);
        sigDesc.NodeMask = 0;
        sigDesc.NumArgumentDescs = 1;
        sigDesc.pArgumentDescs = &desc;
        HR(pGraphics->GetDevice()->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(m_pLightCullingCommandSignature.GetAddressOf())));
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

        Shader vertexShader = Shader("Resources/Shaders/CL_Diffuse.hlsl", Shader::Type::VertexShader, "Diffuse_VS");
        Shader pixelShader = Shader("Resources/Shaders/CL_Diffuse.hlsl", Shader::Type::PixelShader, "Diffuse_PS");

        m_pDiffuseRS = std::make_unique<RootSignature>();
		m_pDiffuseRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pDiffuseRS->SetConstantBufferView(1, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pDiffuseRS->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3D12_SHADER_VISIBILITY_ALL);
		m_pDiffuseRS->SetDescriptorTableSimple(3, 3, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3D12_SHADER_VISIBILITY_PIXEL);
        m_pDiffuseRS->SetDescriptorTableSimple(4, 6, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);

        D3D12_SAMPLER_DESC samplerDesc{};
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        m_pDiffuseRS->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        m_pDiffuseRS->AddStaticSampler(1, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

        m_pDiffuseRS->Finalize("Diffuse", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        // opaque
        m_pDiffusePSO = std::make_unique<GraphicsPipelineState>();
        m_pDiffusePSO->SetInputLayout(inputElementDescs, _countof(inputElementDescs));
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
		m_pDiffuseTransparencyPSO->SetDepthTest(D3D12_COMPARISON_FUNC_LESS_EQUAL);
		m_pDiffuseTransparencyPSO->Finalize("Diffuse (Transparent)", pGraphics->GetDevice());
    }

    // cluster debug rendering
    {
        Shader vertexShader = Shader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::VertexShader, "VSMain");
        Shader geometryShader = Shader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::GeometryShader, "GSMain");
        Shader pixelShader = Shader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::PixelShader, "PSMain");

        m_pDebugClusterRS = std::make_unique<RootSignature>();
        m_pDebugClusterRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_GEOMETRY);
        m_pDebugClusterRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, D3D12_SHADER_VISIBILITY_VERTEX);
        m_pDebugClusterRS->Finalize("Debug Cluster", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS);

        m_pDebugClusterPSO = std::make_unique<GraphicsPipelineState>();
        m_pDebugClusterPSO->SetDepthTest(D3D12_COMPARISON_FUNC_LESS_EQUAL);
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

