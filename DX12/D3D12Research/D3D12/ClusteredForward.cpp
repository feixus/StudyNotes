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

/*
based on screen resolutions(eg.1080P), depth complexity(16~32 slices) and GPU performance tradeoff.
depth axis is sliced logarithmically or exponentially to account for non-linear depth distribution.
need adjust for diffrent platform or resolution ...
now we use 3456 clusters.
*/ 
static constexpr int cClusterDimensionsX = 16;
static constexpr int cClusterDimensionsY = 9;
static constexpr int cClusterDimensionsZ = 24;
static constexpr int cMaxClusters = cClusterDimensionsX * cClusterDimensionsY * cClusterDimensionsZ;

ClusteredForward::ClusteredForward(Graphics* pGraphics)
    : m_pGraphics(pGraphics)
{
    SetupResources(pGraphics);
    SetupPipelines(pGraphics);
}

void ClusteredForward::OnSwapchainCreated(int windowWidth, int windowHeight)
{
    struct AABB { Vector4 Min; Vector4 Max; };
    m_pAabbBuffer->Create(m_pGraphics, sizeof(AABB), cMaxClusters, false);
    m_pAabbBuffer->SetName("AABBs");

    m_pUniqueClusterBuffer->Create(m_pGraphics, sizeof(uint32_t), cMaxClusters, false);
    m_pUniqueClusterBuffer->SetName("Unique Clusters");

    m_pActiveClusterListBuffer->Create(m_pGraphics, sizeof(uint32_t), cMaxClusters, false);
    m_pActiveClusterListBuffer->SetName("Active Cluster List");
}

void ClusteredForward::Execute(const ClusteredForwardInputResource& inputResource)
{
    Vector2 screenDimensions((float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight());
    float nearZ = 0.1f;
    float farZ = 1000.0f;
    Matrix projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, screenDimensions.x / screenDimensions.y, nearZ, farZ);

    float sliceMagicA = (float)cClusterDimensionsZ / log(farZ / nearZ);
    float sliceMagicB = (float)cClusterDimensionsZ * log(nearZ) / log(farZ / nearZ);

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
            int ClusterDimensions[3];
            float NearZ;
            float FarZ;
        } constantBuffer;

        constantBuffer.ScreenDimensions = screenDimensions;
        constantBuffer.NearZ = nearZ;
        constantBuffer.FarZ = farZ;
        projection.Invert(constantBuffer.ProjectionInverse);
        constantBuffer.ClusterSize.x = screenDimensions.x / cClusterDimensionsX;
        constantBuffer.ClusterSize.y = screenDimensions.y / cClusterDimensionsY;
        constantBuffer.ClusterDimensions[0] = cClusterDimensionsX;
        constantBuffer.ClusterDimensions[1] = cClusterDimensionsY;
        constantBuffer.ClusterDimensions[2] = cClusterDimensionsZ;

        pContext->SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(constantBuffer));
        pContext->SetDynamicDescriptor(1, 0, m_pAabbBuffer->GetUAV());

        pContext->Dispatch(cClusterDimensionsX, cClusterDimensionsY, cClusterDimensionsZ);

        Profiler::Instance()->End(pContext);
        uint64_t fence = pContext->Execute(false);
        m_pGraphics->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->InsertWaitForFence(fence);
    }

    // mark unique clusters
    {
        GraphicsCommandContext* pContext = (GraphicsCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
        Profiler::Instance()->Begin("Mark Unique Clusters", pContext);

        pContext->InsertResourceBarrier(m_pUniqueClusterBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);
        pContext->InsertResourceBarrier(inputResource.pDepthPrepassBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
        pContext->ClearDepth(inputResource.pDepthPrepassBuffer->GetDSV(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

        Profiler::Instance()->Begin("Update Data", pContext);
        std::vector<uint32_t> zero(cMaxClusters);
        m_pUniqueClusterBuffer->SetData(pContext, zero.data(), sizeof(uint32_t) * zero.size());
        Profiler::Instance()->End(pContext);

        pContext->SetGraphicsRootSignature(m_pMarkUniqueClustersRS.get());
        pContext->SetGraphicsPipelineState(m_pMarkUniqueClustersPSO.get());
        pContext->SetViewport(FloatRect(0.0f, 0.0f, screenDimensions.x, screenDimensions.y));
        pContext->SetScissorRect(FloatRect(0, 0, screenDimensions.x, screenDimensions.y));
        pContext->SetRenderTargets(nullptr, inputResource.pDepthPrepassBuffer->GetDSV());
        pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        
        struct ConstantBuffer
        {
            Matrix WorldView;
            Matrix Projection;
            uint32_t ClusterDimensions[4];
            float ClusterSize[2];
            float SliceMagicA;
            float SliceMagicB;
        } constantBuffer;

        constantBuffer.WorldView = m_pGraphics->GetViewMatrix();
        constantBuffer.Projection = projection;
        constantBuffer.SliceMagicA = sliceMagicA;
        constantBuffer.SliceMagicB = sliceMagicB;
        constantBuffer.ClusterDimensions[0] = cClusterDimensionsX;
        constantBuffer.ClusterDimensions[1] = cClusterDimensionsY;
        constantBuffer.ClusterDimensions[2] = cClusterDimensionsZ;
        constantBuffer.ClusterDimensions[3] = 0;
        constantBuffer.ClusterSize[0] = screenDimensions.x / cClusterDimensionsX;
        constantBuffer.ClusterSize[1] = screenDimensions.y / cClusterDimensionsY;
        
        pContext->SetDynamicConstantBufferView(0, &constantBuffer, sizeof(constantBuffer));
        pContext->SetDynamicDescriptor(1, 0, m_pUniqueClusterBuffer->GetUAV());
        for (const Batch& b : *inputResource.pOpaqueBatches)
        {
            b.pMesh->Draw(pContext);
        }

        Profiler::Instance()->End(pContext);
        uint64_t fence = pContext->Execute(false);
        m_pGraphics->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE)->InsertWaitForFence(fence);
    }
    
    // compact clusters
    {
        ComputeCommandContext* pContext = (ComputeCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE);
        Profiler::Instance()->Begin("Compact Clusters", pContext);

        pContext->SetComputePipelineState(m_pCompactClusterListPSO.get());
        pContext->SetComputeRootSignature(m_pCompactClusterListRS.get());

        uint32_t values[] = {0, 0, 0, 0};
        pContext->ClearUavUInt(m_pActiveClusterListBuffer->GetCounter(), values);

        pContext->SetDynamicDescriptor(0, 0, m_pUniqueClusterBuffer->GetSRV());
        pContext->SetDynamicDescriptor(1, 0, m_pActiveClusterListBuffer->GetUAV());

        pContext->Dispatch(cMaxClusters / 64, 1, 1);
        
        Profiler::Instance()->End(pContext);
        pContext->Execute(false);
    }

    // update indirect arguments
    {
		ComputeCommandContext* pContext = (ComputeCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE);
        Profiler::Instance()->Begin("Update Indirect Arguments", pContext);

        pContext->InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

        pContext->SetComputePipelineState(m_pUpdateIndirectArgumentsPSO.get());
		pContext->SetComputeRootSignature(m_pUpdateIndirectArgumentsRS.get());

        pContext->SetDynamicDescriptor(0, 0, m_pActiveClusterListBuffer->GetCounter()->GetSRV());
		pContext->SetDynamicDescriptor(1, 0, m_pIndirectArguments->GetUAV());

        pContext->Dispatch(1, 1, 1);
        Profiler::Instance()->End(pContext);
        pContext->Execute(false);
    }

    // light culling
    {
        ComputeCommandContext* pContext = (ComputeCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE);
        Profiler::Instance()->Begin("Light Culling", pContext);

        pContext->SetComputePipelineState(m_pLightCullingPSO.get());
        pContext->SetComputeRootSignature(m_pLightCullingRS.get());

        pContext->InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, true);

        Profiler::Instance()->Begin("Update Data", pContext);
        uint32_t zero = 0;
        m_pLightIndexCounter->SetData(pContext, &zero, sizeof(uint32_t));
        uint32_t zero2[64 * cMaxClusters];
        memset(zero2, 0, 64 * sizeof(uint32_t) * cMaxClusters);
        m_pLightIndexGrid->SetData(pContext, &zero2, 64 * cMaxClusters * sizeof(uint32_t));
        m_pLights->SetData(pContext, inputResource.pLights->data(), sizeof(Light) * inputResource.pLights->size(), 0);
        Profiler::Instance()->End(pContext);

        struct ConstantBuffer
        {
            Matrix View;
        } constantBuffer;

        constantBuffer.View = m_pGraphics->GetViewMatrix();

        pContext->SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));
        pContext->SetDynamicDescriptor(1, 0, m_pLights->GetSRV());
        pContext->SetDynamicDescriptor(1, 1, m_pAabbBuffer->GetSRV());
        pContext->SetDynamicDescriptor(1, 2, m_pActiveClusterListBuffer->GetSRV());
        pContext->SetDynamicDescriptor(2, 0, m_pLightIndexCounter->GetUAV());
        pContext->SetDynamicDescriptor(2, 1, m_pLightIndexGrid->GetUAV());
        pContext->SetDynamicDescriptor(2, 2, m_pLightGrid->GetUAV());

        pContext->ExecuteIndirect(m_pLightCullingCommandSignature.Get(), m_pIndirectArguments.get());

        Profiler::Instance()->End(pContext);
        uint64_t fence = pContext->Execute(false);
        m_pGraphics->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->InsertWaitForFence(fence);
    }

    // base pass
    {
		GraphicsCommandContext* pContext = (GraphicsCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
        Profiler::Instance()->Begin("Lighting Pass", pContext);

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
        
        struct PerObjectData
        {
            Matrix World;
		} objectData;

        struct PerFrameData
        {
			Matrix View;
			Matrix Projection;
            Matrix ViewInverse;
            uint32_t ClusterDimensions[4];
            Vector2 ScreenDimensions;
            float NearZ;
            float FarZ;
            float ClusterSize[2];
            float SliceMagicA;
            float SliceMagicB;
        } frameData;

        Matrix view = m_pGraphics->GetViewMatrix();
        view.Invert(frameData.ViewInverse);
        frameData.View = view;
        frameData.Projection = projection;
		frameData.ClusterDimensions[0] = cClusterDimensionsX;
		frameData.ClusterDimensions[1] = cClusterDimensionsY;
		frameData.ClusterDimensions[2] = cClusterDimensionsZ;
		frameData.ClusterDimensions[3] = cMaxClusters;
        frameData.ScreenDimensions = screenDimensions;
        frameData.NearZ = nearZ;
        frameData.FarZ = farZ;
		frameData.ClusterSize[0] = screenDimensions.x / cClusterDimensionsX;
		frameData.ClusterSize[1] = screenDimensions.y / cClusterDimensionsY;
        frameData.SliceMagicA = sliceMagicA;
        frameData.SliceMagicB = sliceMagicB;

		pContext->SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
		pContext->SetDynamicDescriptor(3, 0, m_pLightGrid->GetSRV());
		pContext->SetDynamicDescriptor(3, 1, m_pLightIndexGrid->GetSRV());
		pContext->SetDynamicDescriptor(3, 2, m_pLights->GetSRV());
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

		pContext->InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);
		pContext->InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
		
        Profiler::Instance()->End(pContext);
        pContext->Execute(false);
	}
}

void ClusteredForward::SetupResources(Graphics* pGraphics)
{
    m_pAabbBuffer = std::make_unique<StructuredBuffer>(pGraphics);
    m_pUniqueClusterBuffer = std::make_unique<StructuredBuffer>(pGraphics);
    m_pActiveClusterListBuffer = std::make_unique<StructuredBuffer>(pGraphics);

	m_pIndirectArguments = std::make_unique<ByteAddressBuffer>(pGraphics);
    m_pIndirectArguments->Create(m_pGraphics, sizeof(uint32_t), 3, false);

	m_pLights = std::make_unique<StructuredBuffer>(pGraphics);
	m_pLights->Create(m_pGraphics, sizeof(Light), Graphics::MAX_LIGHT_COUNT);
	m_pLightIndexCounter = std::make_unique<StructuredBuffer>(pGraphics);
	m_pLightIndexCounter->Create(m_pGraphics, sizeof(uint32_t), 1);
	m_pLightIndexGrid = std::make_unique<StructuredBuffer>(pGraphics);
	m_pLightIndexGrid->Create(m_pGraphics, sizeof(uint32_t), cMaxClusters * 64);
	m_pLightGrid = std::make_unique<StructuredBuffer>(pGraphics);
	m_pLightGrid->Create(m_pGraphics, 2 * sizeof(uint32_t), cMaxClusters);

    CopyCommandContext* pCopyContext = (CopyCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COPY);
    m_pHeatMapTexture = std::make_unique<GraphicsTexture2D>();
    m_pHeatMapTexture->Create(pGraphics, pCopyContext, "Resources/textures/HeatMap.png", TextureUsage::ShaderResource);
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
        };

        Shader vertexShader = Shader("Resources/Shaders/CL_MarkUniqueClusters.hlsl", Shader::Type::VertexShader, "MarkClusters_VS");
        Shader pixelShader = Shader("Resources/Shaders/CL_MarkUniqueClusters.hlsl", Shader::Type::PixelShader, "MarkClusters_PS");

        m_pMarkUniqueClustersRS = std::make_unique<RootSignature>();
        m_pMarkUniqueClustersRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        m_pMarkUniqueClustersRS->SetDescriptorTableSimple(1, 1, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pMarkUniqueClustersRS->Finalize("Mark Unique Clusters", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        m_pMarkUniqueClustersPSO = std::make_unique<GraphicsPipelineState>();
        m_pMarkUniqueClustersPSO->SetInputLayout(inputElementDescs, 1);
        m_pMarkUniqueClustersPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
        m_pMarkUniqueClustersPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
        m_pMarkUniqueClustersPSO->SetBlendMode(BlendMode::Replace, false);
        m_pMarkUniqueClustersPSO->SetRootSignature(m_pMarkUniqueClustersRS->GetRootSignature());
        m_pMarkUniqueClustersPSO->SetRenderTargetFormats(nullptr, 0, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
        m_pMarkUniqueClustersPSO->Finalize("Mark Unique Clusters", pGraphics->GetDevice());
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
		m_pDiffuseRS->SetDescriptorTableSimple(3, 3, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3D12_SHADER_VISIBILITY_ALL);
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

        m_pDiffusePSO = std::make_unique<GraphicsPipelineState>();
        m_pDiffusePSO->SetInputLayout(inputElementDescs, _countof(inputElementDescs));
        m_pDiffusePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
        m_pDiffusePSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
        m_pDiffusePSO->SetBlendMode(BlendMode::Replace, false);
        m_pDiffusePSO->SetRootSignature(m_pDiffuseRS->GetRootSignature());
        m_pDiffusePSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
        m_pDiffusePSO->SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
        m_pDiffusePSO->Finalize("Diffuse", pGraphics->GetDevice());
    }
}

