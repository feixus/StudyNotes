#include "stdafx.h"
#include "RTReflections.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Core/RaytracingCommon.h"
#include "Graphics/Mesh.h"
#include "Scene/Camera.h"

RTReflections::RTReflections(Graphics* pGraphics)
{
    if (pGraphics->SupportsRaytracing())
    {
        SetupResources(pGraphics);
        SetupPipelines(pGraphics);
    }
}

void RTReflections::Execute(RGGraph& graph, const SceneData& sceneData)
{

}

void RTReflections::SetupResources(Graphics* pGraphics)
{
    m_pTestOutput = std::make_unique<GraphicsTexture>(pGraphics, "RTReflections Test Output");
    m_pTestOutput->Create(TextureDesc::Create2D(512, 512, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));
}

void RTReflections::SetupPipelines(Graphics* pGraphics)
{
    // raytracing pipeline
    {
        m_pRayGenRS = std::make_unique<RootSignature>(pGraphics);
        m_pRayGenRS->Finalize("Ray Gen RS", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

        m_pHitRS = std::make_unique<RootSignature>(pGraphics);
        m_pHitRS->SetConstantBufferView(0, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pHitRS->SetShaderResourceView(1, 100, D3D12_SHADER_VISIBILITY_ALL);
        m_pHitRS->SetShaderResourceView(2, 101, D3D12_SHADER_VISIBILITY_ALL);
        m_pHitRS->Finalize("Hit RS", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

        m_pMissRS = std::make_unique<RootSignature>(pGraphics);
        m_pMissRS->Finalize("Miss RS", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

        m_pGlobalRS = std::make_unique<RootSignature>(pGraphics);
        m_pGlobalRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        m_pGlobalRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pGlobalRS->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, D3D12_SHADER_VISIBILITY_ALL);
        m_pGlobalRS->SetDescriptorTableSimple(3, 200, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 128, D3D12_SHADER_VISIBILITY_ALL);
        m_pGlobalRS->AddStaticSampler(0, CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT), D3D12_SHADER_VISIBILITY_ALL);
        m_pGlobalRS->Finalize("Dummy Global RS", D3D12_ROOT_SIGNATURE_FLAG_NONE);

        ShaderLibrary shaderLibrary("RTReflections.hlsl");

        CD3DX12_STATE_OBJECT_HELPER stateObjectDesc;
        stateObjectDesc.AddLibrary(CD3DX12_SHADER_BYTECODE(shaderLibrary.GetByteCode(), shaderLibrary.GetByteCodeSize()), {"RayGen", "ClosestHit", "Miss"});
        stateObjectDesc.AddHitGroup("HitGroup", "ClosestHit");
		stateObjectDesc.BindLocalRootSignature("RayGen", m_pRayGenRS->GetRootSignature());
		stateObjectDesc.BindLocalRootSignature("Miss", m_pMissRS->GetRootSignature());
		stateObjectDesc.BindLocalRootSignature("HitGroup", m_pHitRS->GetRootSignature());
        stateObjectDesc.SetRaytracingShaderConfig(3 * sizeof(float), 2 * sizeof(float));
        stateObjectDesc.SetRaytracingPipelineConfig(1);
        stateObjectDesc.SetGlobalRootSignature(m_pGlobalRS->GetRootSignature());
        D3D12_STATE_OBJECT_DESC desc = stateObjectDesc.Desc();
        pGraphics->GetRaytracingDevice()->CreateStateObject(&desc, IID_PPV_ARGS(m_pStateObject.GetAddressOf()));
    }
}
