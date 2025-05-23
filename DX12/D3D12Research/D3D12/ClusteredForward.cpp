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

    m_pDebugTexture->Create(m_pGraphics, windowWidth, windowHeight, Graphics::RENDER_TARGET_FORMAT, TextureUsage::RenderTarget, m_pGraphics->GetMultiSampleCount());
    m_pDebugTexture->SetName("Debug Texture");
}

void ClusteredForward::Execute(const ClusteredForwardInputResource& inputResource)
{
    Vector2 screenDimensions((float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight());
    float nearZ = 0.1f;
    float farZ = 1000.0f;
    Matrix projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, screenDimensions.x / screenDimensions.y, nearZ, farZ);

    // create AABBs
    {
        ComputeCommandContext* pContext = (ComputeCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE);
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

        uint64_t fence = pContext->Execute(false);
        m_pGraphics->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->InsertWaitForFence(fence);
    }

    // mark unique clusters
    {
        GraphicsCommandContext* pContext = (GraphicsCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);

        pContext->InsertResourceBarrier(m_pUniqueClusterBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);
        pContext->InsertResourceBarrier(inputResource.pDepthPrepassBuffer, D3D12_RESOURCE_STATE_DEPTH_READ, false);
        pContext->InsertResourceBarrier(inputResource.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, true);

        std::vector<uint32_t> zero(cMaxClusters);
        m_pUniqueClusterBuffer->SetData(pContext, zero.data(), zero.size());

        pContext->SetGraphicsRootSignature(m_pMarkUniqueClustersRS.get());
        pContext->SetGraphicsPipelineState(m_pMarkUniqueClustersPSO.get());
        pContext->SetViewport(FloatRect(0.0f, 0.0f, screenDimensions.x, screenDimensions.y));
        pContext->SetScissorRect(FloatRect(0, 0, screenDimensions.x, screenDimensions.y));
        pContext->SetRenderTarget(inputResource.pRenderTarget->GetRTV(), inputResource.pDepthPrepassBuffer->GetDSV());
        pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        
        struct ConstantBuffer
        {
            Matrix WorldView;
            Matrix Projection;
            Vector2 ScreenDimensions;
            float NearZ;
            float FarZ;
            uint32_t ClusterDimensions[4];
            float ClusterSize[2];
        } constantBuffer;

        constantBuffer.WorldView = m_pGraphics->GetViewMatrix();
        constantBuffer.Projection = projection;
        constantBuffer.ScreenDimensions = screenDimensions;
        constantBuffer.NearZ = nearZ;
        constantBuffer.FarZ = farZ;
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

        uint64_t fence = pContext->Execute(false);
        m_pGraphics->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE)->InsertWaitForFence(fence);
    }
    
    // compact clusters
    {
        ComputeCommandContext* pContext = (ComputeCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE);
        pContext->SetComputePipelineState(m_pCompactClusterListPSO.get());
        pContext->SetComputeRootSignature(m_pCompactClusterListRS.get());

        std::vector<uint32_t> zero(1);
        m_pActiveClusterListBuffer->GetCounter()->SetData(pContext, zero.data(), zero.size());

        pContext->SetDynamicDescriptor(0, 0, m_pUniqueClusterBuffer->GetSRV());
        pContext->SetDynamicDescriptor(1, 0, m_pActiveClusterListBuffer->GetUAV());

        pContext->Dispatch(cMaxClusters / 64, 1, 1);
        
        pContext->Execute(false);
    }
}

void ClusteredForward::SetupResources(Graphics* pGraphics)
{
    m_pAabbBuffer = std::make_unique<StructuredBuffer>(pGraphics);
    m_pUniqueClusterBuffer = std::make_unique<StructuredBuffer>(pGraphics);
    m_pActiveClusterListBuffer = std::make_unique<StructuredBuffer>(pGraphics);

    m_pDebugTexture = std::make_unique<GraphicsTexture2D>();
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
        m_pMarkUniqueClustersPSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
        m_pMarkUniqueClustersPSO->SetDepthWrite(false);
        m_pMarkUniqueClustersPSO->SetDepthEnable(false);
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
}

