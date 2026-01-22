#include "stdafx.h"
#include "CBTTessellation.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/SceneView.h"
#include "Graphics/Profiler.h"
#include "Scene/Camera.h"
#include "Core/Input.h"
#include "imgui/imgui_internal.h"

constexpr uint32_t IndirectDispatchArgsOffset = 0;
constexpr uint32_t IndirectDispatchMeshArgsOffset = IndirectDispatchArgsOffset + sizeof(D3D12_DISPATCH_ARGUMENTS);
constexpr uint32_t IndirectDrawArgsOffset = IndirectDispatchMeshArgsOffset + sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);

namespace CBTSettings
{
    static int CBTDepth = 15;
    static bool FreezeCamera = false;
    static bool DebugVisualize = false;
    static bool CpuDemo = false;
    static bool MeshShader = true;
    static bool SumReductionOptimized = true;
    static float ScreenSizeBias = 9.0f;
    static float HeightmapVarianceBias = 0.02f;
    
    // PSO settings
    static bool Wireframe = false;
    static bool FrustumCull = true;
    static bool DisplacementLOD = true;
    static bool DistanceLOD = true;
    static bool AlwaysSubdivide = false;
    static int TriangleSubdivision = 3;
}

CBTTessellation::CBTTessellation(GraphicsDevice* pGraphicsDevice) : m_pDevice(pGraphicsDevice)
{
    if (!m_pDevice->GetCapabilities().SupportMeshShading())
    {
        CBTSettings::MeshShader = false;
    }

    CreateResources();
    SetupPipelines();
    AllocateCBT();
}

void CBTTessellation::Execute(RGGraph& graph, GraphicsTexture* pRenderTarget, GraphicsTexture* pDepthTexture, const SceneView& sceneView)
{
    Matrix terrainTransform = Matrix::CreateScale(30, 10, 30) * Matrix::CreateTranslation(0, -10, 0);

    ImGui::Begin("Parameters");
    ImGui::Text("CBT");

    if (ImGui::SliderInt("Max Depth", &CBTSettings::CBTDepth, 5, 22))
	{
        AllocateCBT();
	}
    if (ImGui::SliderInt("Triangle SubD", &CBTSettings::TriangleSubdivision, 0, 4))
    {
        SetupPipelines();
    }
    ImGui::SliderFloat("Screen Size Bias", &CBTSettings::ScreenSizeBias, 0.0f, 15.0f);
    ImGui::SliderFloat("Heightmap Variance Bias", &CBTSettings::HeightmapVarianceBias, 0.0f, 0.1f);
    
    if (ImGui::TreeNode("Debug"))
    {
        ImGui::Checkbox("Debug Visualize", &CBTSettings::DebugVisualize);
        ImGui::Checkbox("CPU Demo", &CBTSettings::CpuDemo);
        if (m_pDevice->GetCapabilities().SupportMeshShading())
        {
            ImGui::Checkbox("Mesh Shader", &CBTSettings::MeshShader);
        }
        ImGui::Checkbox("Freeze Camera", &CBTSettings::FreezeCamera);
        ImGui::Checkbox("Optimized Sum Reduction", &CBTSettings::SumReductionOptimized);

         if (ImGui::Checkbox("Wireframe", &CBTSettings::Wireframe))
        {
            SetupPipelines();
        }
        if (ImGui::Checkbox("Frustum Cull", &CBTSettings::FrustumCull))
        {
            SetupPipelines();
        }
        if (ImGui::Checkbox("Displacement LOD", &CBTSettings::DisplacementLOD))
        {
            SetupPipelines();
        }
        if (ImGui::Checkbox("Distance LOD", &CBTSettings::DistanceLOD))
        {
            SetupPipelines();
        }
        if (ImGui::Checkbox("Always Subdivide", &CBTSettings::AlwaysSubdivide))
        {
            SetupPipelines();
        }
        ImGui::TreePop();
    }
   
    ImGui::End();

    if (CBTSettings::CpuDemo)
    {
        DemoCpuCBT();
    }

    RG_GRAPH_SCOPE("CBT", graph);

    if (!CBTSettings::FreezeCamera)
    {
        m_CachedFrustum = sceneView.pCamera->GetFrustum();
        m_CachedViewMatrix = sceneView.pCamera->GetView();
    }

    struct CommonArgs
    {
        uint32_t NumElements;
    } commonArgs;
    commonArgs.NumElements = (uint32_t)m_pCBTBuffer->GetSize() / sizeof(uint32_t);

    struct UpdateData
    {
        Matrix World;
        Matrix WorldView;
        Matrix ViewProjection;
        Matrix WorldViewProjection;
        Vector4 FrustumPlanes[6];
        float HeightmapSizeInv;
        float ScreenSizeBias;
        float HeightmapVarianceBias;
    } updateData;
    updateData.World = terrainTransform;
    updateData.WorldView = terrainTransform * m_CachedViewMatrix;
    updateData.ViewProjection = sceneView.pCamera->GetViewProjection();
    updateData.WorldViewProjection = terrainTransform * sceneView.pCamera->GetViewProjection();

    DirectX::XMVECTOR nearPlane, farPlane, left, right, top, bottom;
    m_CachedFrustum.GetPlanes(&nearPlane, &farPlane, &right, &left, &top, &bottom);
    updateData.FrustumPlanes[0] = Vector4(nearPlane);
    updateData.FrustumPlanes[1] = Vector4(farPlane);
    updateData.FrustumPlanes[2] = Vector4(left);
    updateData.FrustumPlanes[3] = Vector4(right);
    updateData.FrustumPlanes[4] = Vector4(top);
    updateData.FrustumPlanes[5] = Vector4(bottom);

    updateData.HeightmapSizeInv = 1.0f / m_pHeightmap->GetWidth();
    updateData.ScreenSizeBias = CBTSettings::ScreenSizeBias;
    updateData.HeightmapVarianceBias = CBTSettings::HeightmapVarianceBias;

    if (m_IsDirty)
    {
        RGPassBuilder cbtUpload = graph.AddPass("CBT Upload");
        cbtUpload.Bind([=](CommandContext& context, const RGPassResource&)
        {
            m_pCBTBuffer->SetData(&context, m_CBT.GetData(), m_CBT.GetMemoryUse());
            context.FlushResourceBarriers();
        });
        m_IsDirty = false;
    }

    if (!CBTSettings::MeshShader)
    {
        RGPassBuilder cbtUpdate = graph.AddPass("CBT Update");
        cbtUpdate.Bind([=](CommandContext& context, const RGPassResource& resources)
        {
            context.InsertResourceBarrier(m_pCBTBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.InsertResourceBarrier(m_pCBTIndirectArgs.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            context.SetComputeRootSignature(m_pCBTRS.get());
    
            context.SetComputeDynamicConstantBufferView(0, commonArgs);
            context.SetComputeDynamicConstantBufferView(1, updateData);
    
            context.BindResource(2, 0, m_pCBTBuffer->GetUAV());
            context.BindResource(3, 0, m_pHeightmap->GetSRV());
    
            context.SetPipelineState(m_pCBTUpdatePSO);
            context.ExecuteIndirect(m_pDevice->GetIndirectDispatchSignature(), 1, m_pCBTIndirectArgs.get(), nullptr, IndirectDispatchArgsOffset);
            context.InsertUavBarrier(m_pCBTBuffer.get());
        });
    }

    RGPassBuilder cbtSumReduction = graph.AddPass("CBT Sum Reduction");
	cbtSumReduction.Bind([=](CommandContext& context, const RGPassResource& resources)
    {
        context.SetComputeRootSignature(m_pCBTRS.get());
        context.SetComputeDynamicConstantBufferView(0, commonArgs);

        context.BindResource(2, 0, m_pCBTBuffer->GetUAV());

        struct SumReductionData
        {
            uint32_t Depth;
        } reductionArgs;
        int32_t currentDepth = CBTSettings::CBTDepth;

        if (CBTSettings::SumReductionOptimized)
        {
            reductionArgs.Depth = currentDepth;
            context.SetComputeDynamicConstantBufferView(1, reductionArgs);

            context.SetPipelineState(m_pCBTSumReductionFirstPassPSO);
            context.Dispatch(ComputeUtils::GetNumThreadGroups(1 << currentDepth, 256 * sizeof(uint32_t)));
            context.InsertUavBarrier(m_pCBTBuffer.get());

            currentDepth -= 5;
        }

        for (currentDepth = CBTSettings::CBTDepth - 1; currentDepth >= 0; currentDepth--)
        {
            reductionArgs.Depth = currentDepth;
            context.SetComputeDynamicConstantBufferView(1, reductionArgs);
    
            context.SetPipelineState(m_pCBTSumReductionPSO);
            context.Dispatch(ComputeUtils::GetNumThreadGroups(1 << currentDepth, 256));
            context.InsertUavBarrier(m_pCBTBuffer.get());
        }
    });
        
    RGPassBuilder cbtUpdateIndirectArgs = graph.AddPass("CBT Update Indirect Args");
	cbtUpdateIndirectArgs.Bind([=](CommandContext& context, const RGPassResource& resources)
    {
        context.SetComputeRootSignature(m_pCBTRS.get());
        context.SetComputeDynamicConstantBufferView(0, commonArgs);

        context.BindResource(2, 0, m_pCBTBuffer->GetUAV());
        context.BindResource(2, 1, m_pCBTIndirectArgs->GetUAV());

        context.InsertResourceBarrier(m_pCBTIndirectArgs.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        context.SetPipelineState(m_pCBTIndirectArgsPSO);
        context.Dispatch(1, 1, 1);
    });

    RGPassBuilder cbtRender = graph.AddPass("CBT Render");
    cbtRender.Bind([=](CommandContext& context, const RGPassResource& resources)
    {
        context.InsertResourceBarrier(m_pCBTIndirectArgs.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        context.InsertResourceBarrier(pDepthTexture, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        context.SetGraphicsRootSignature(m_pCBTRS.get());
        context.SetPipelineState(CBTSettings::MeshShader ? m_pCBTRenderMeshShaderPSO : m_pCBTRenderPSO);
        context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        context.SetComputeDynamicConstantBufferView(0, commonArgs);
        context.SetGraphicsDynamicConstantBufferView(1, updateData);

        context.BindResource(2, 0, m_pCBTBuffer->GetUAV());
        context.BindResource(3, 0, m_pHeightmap->GetSRV());

        context.BeginRenderPass(RenderPassInfo(pRenderTarget, RenderPassAccess::Load_Store, pDepthTexture, RenderPassAccess::Load_Store, true));
        if (CBTSettings::MeshShader)
        {
            context.ExecuteIndirect(m_pDevice->GetIndirectDispatchMeshSignature(), 1, m_pCBTIndirectArgs.get(), nullptr, IndirectDispatchMeshArgsOffset);
        }
        else
        {
            context.ExecuteIndirect(m_pDevice->GetIndirectDrawSignature(), 1, m_pCBTIndirectArgs.get(), nullptr, IndirectDrawArgsOffset);
        }
        context.EndRenderPass();
    });

    if (CBTSettings::DebugVisualize)
    {
        ImGui::Begin("CBT");
        ImGui::ImageAutoSize(m_pDebugVisualizeTexture.get(), ImVec2((float)m_pDebugVisualizeTexture->GetWidth(), (float)m_pDebugVisualizeTexture->GetHeight()));
        ImGui::End();

        RGPassBuilder cbtDebugVisualize = graph.AddPass("CBT Debug Visualize");
		cbtDebugVisualize.Bind([=](CommandContext& context, const RGPassResource& resources)
        {
            context.InsertResourceBarrier(m_pCBTIndirectArgs.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            context.InsertResourceBarrier(m_pDebugVisualizeTexture.get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

            context.SetGraphicsRootSignature(m_pCBTRS.get());
            context.SetPipelineState(m_pCBTDebugVisualizePSO);
            context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            context.SetComputeDynamicConstantBufferView(0, commonArgs);
            context.SetComputeDynamicConstantBufferView(1, updateData);

            context.BindResource(2, 0, m_pCBTBuffer->GetUAV());

            context.BeginRenderPass(RenderPassInfo(m_pDebugVisualizeTexture.get(), RenderPassAccess::Load_Store, nullptr, RenderPassAccess::NoAccess, false));
            context.ExecuteIndirect(m_pDevice->GetIndirectDrawSignature(), 1, m_pCBTIndirectArgs.get(), nullptr, IndirectDrawArgsOffset);
            context.EndRenderPass();
        });
    }
}

void CBTTessellation::AllocateCBT()
{
    m_CBT.InitBare(CBTSettings::CBTDepth, 1);
    uint32_t size = m_CBT.GetMemoryUse();
    m_pCBTBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateByteAddress(size, BufferFlag::ShaderResource | BufferFlag::UnorderedAccess), "CBT Buffer");
    m_IsDirty = true;
}

void CBTTessellation::CreateResources()
{
    CommandContext* pContext = m_pDevice->AllocateCommandContext();

    m_pHeightmap = std::make_unique<GraphicsTexture>(m_pDevice);
    m_pHeightmap->Create(pContext, "Resources/textures/heightmap.png");

    pContext->Execute(true);

    m_pDebugVisualizeTexture = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(512, 512, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::ShaderResource), "CBT Visualize Texture");

    m_pCBTIndirectArgs = m_pDevice->CreateBuffer(BufferDesc::CreateIndirectArgumemnts<uint32_t>(10), "CBT Indirect Args");
}

void CBTTessellation::SetupPipelines()
{
    std::vector<ShaderDefine> defines = {
        ShaderDefine(std::format("RENDER_WIREFRAME={}", CBTSettings::Wireframe ? 1 : 0)),
        ShaderDefine(std::format("FRUSTUM_CULL={}", CBTSettings::FrustumCull ? 1 : 0)),
        ShaderDefine(std::format("DISPLACEMENT_LOD={}", CBTSettings::DisplacementLOD ? 1 : 0)),
        ShaderDefine(std::format("DISTANCE_LOD={}", CBTSettings::DistanceLOD ? 1 : 0)),
        ShaderDefine(std::format("DEBUG_ALWAYS_SUBDIVIDE={}", CBTSettings::AlwaysSubdivide ? 1 : 0)),
        ShaderDefine(std::format("MESH_SHADER_SUBD_LEVEL={}", Math::Min(CBTSettings::TriangleSubdivision * 2, 6))),
        ShaderDefine(std::format("AMPLIFICATION_SHADER_SUBD_LEVEL={}", Math::Max(CBTSettings::TriangleSubdivision * 2 - 6, 0))),
    };

    m_pCBTRS = std::make_unique<RootSignature>(m_pDevice);
    m_pCBTRS->FinalizeFromShader("CBT RS", m_pDevice->GetShader("CBT.hlsl", ShaderType::Compute, "SumReductionCS", defines));

    {
        PipelineStateInitializer psoDesc;
        psoDesc.SetRootSignature(m_pCBTRS->GetRootSignature());
        psoDesc.SetComputeShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Compute, "PrepareDispatchArgsCS", defines));
        psoDesc.SetName("CBT Indirect Args PSO");
        m_pCBTIndirectArgsPSO = m_pDevice->CreatePipeline(psoDesc);

        psoDesc.SetComputeShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Compute, "SumReductionFirstPassCS", defines));
        psoDesc.SetName("CBT Sum Reduction First Pass PSO");
        m_pCBTSumReductionFirstPassPSO = m_pDevice->CreatePipeline(psoDesc);

        psoDesc.SetComputeShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Compute, "SumReductionCS", defines));
        psoDesc.SetName("CBT Sum Reduction PSO");
        m_pCBTSumReductionPSO = m_pDevice->CreatePipeline(psoDesc);

        psoDesc.SetComputeShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Compute, "UpdateCS", defines));
        psoDesc.SetName("CBT Update PSO");
        m_pCBTUpdatePSO = m_pDevice->CreatePipeline(psoDesc);
    }

    {
        PipelineStateInitializer drawPsoDesc;
        drawPsoDesc.SetRootSignature(m_pCBTRS->GetRootSignature());
        drawPsoDesc.SetPixelShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Pixel, "RenderPS", defines));
        drawPsoDesc.SetVertexShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Vertex, "RenderVS", defines));
        drawPsoDesc.SetRenderTargetFormat(GraphicsDevice::RENDER_TARGET_FORMAT, GraphicsDevice::DEPTH_STENCIL_FORMAT, 1);
        drawPsoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        drawPsoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
        drawPsoDesc.SetName("Draw CBT PSO");
        m_pCBTRenderPSO = m_pDevice->CreatePipeline(drawPsoDesc);
    }

    {
		if (m_pDevice->GetCapabilities().SupportMeshShading())
		{
			PipelineStateInitializer drawPsoDesc;
			drawPsoDesc.SetRootSignature(m_pCBTRS->GetRootSignature());
			drawPsoDesc.SetAmplificationShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Mesh, "UpdateAS", defines));
			drawPsoDesc.SetMeshShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Mesh, "RenderMS", defines));
			drawPsoDesc.SetPixelShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Pixel, "RenderPS", defines));
			drawPsoDesc.SetRenderTargetFormat(GraphicsDevice::RENDER_TARGET_FORMAT, GraphicsDevice::DEPTH_STENCIL_FORMAT, 1);
			drawPsoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
            drawPsoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
			drawPsoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
			drawPsoDesc.SetName("Draw CBT Mesh Shader PSO");
			m_pCBTRenderMeshShaderPSO = m_pDevice->CreatePipeline(drawPsoDesc);
		}
    }

    {
        PipelineStateInitializer drawPsoDesc;
        drawPsoDesc.SetRootSignature(m_pCBTRS->GetRootSignature());
        drawPsoDesc.SetPixelShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Pixel, "DebugVisualizePS", defines));
        drawPsoDesc.SetVertexShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Vertex, "DebugVisualizeVS", defines));
        drawPsoDesc.SetRenderTargetFormat(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN, 1);
        drawPsoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        drawPsoDesc.SetDepthEnable(false);
        drawPsoDesc.SetName("Debug Visualize CBT PSO");
        m_pCBTDebugVisualizePSO = m_pDevice->CreatePipeline(drawPsoDesc);
    }
}

void CBTTessellation::DemoCpuCBT()
{
    PROFILE_SCOPE("CPU CBT Demo");

    ImGui::Begin("CBT Demo");

    static int maxDepth = 7;
	static bool init = false;

	static CBT cbt;
	if (ImGui::SliderInt("Max Depth", &maxDepth, 2, 16) || !init)
	{
		cbt.Init(maxDepth, maxDepth);
		init = true;
	}

	static bool splitting = true;
	static bool merging = true;
	static bool alwaysUpdate = false;
	ImGui::Checkbox("Always Update", &alwaysUpdate);
	ImGui::SameLine();
	ImGui::Checkbox("Splitting", &splitting);
	ImGui::SameLine();
	ImGui::Checkbox("Merging", &merging);
	ImGui::SameLine();

	ImGui::Text("Size: %s", Math::PrettyPrintDataSize(cbt.GetMemoryUse()).c_str());

	ImVec2 cPos = ImGui::GetCursorScreenPos();
	float scale = 400;

    const float itemWidth = 20;
    const float itemSpacing = 5;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(itemSpacing, itemSpacing));

    ImDrawList* bgList = ImGui::GetWindowDrawList();

    uint32_t heapID = 1;
    for (uint32_t d = 0; d < cbt.GetMaxDepth(); d++)
    {
        ImGui::Spacing();
        for (uint32_t j = 0; j < Math::Exp2(d); j++)
        {
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            cursor += ImVec2(itemWidth, itemWidth * 0.5f);
            float rightChildPos = (itemWidth + itemSpacing) * ((1u << (cbt.GetMaxDepth() - d - 1)) - 0.5f);

            ImGui::PushID(heapID);
            ImGui::Button(Sprintf("%d", cbt.GetData(heapID)).c_str(), ImVec2(itemWidth, itemWidth));

            bgList->AddLine(cursor, ImVec2(cursor.x + rightChildPos, cursor.y), 0xFFFFFFFF);
            bgList->AddLine(ImVec2(cursor.x - itemWidth * 0.5f, cursor.y + itemWidth * 0.5f), ImVec2(cursor.x - itemWidth * 0.5f, cursor.y + itemWidth * 0.5f + itemSpacing), 0xFFFFFFFF);
            bgList->AddLine(ImVec2(cursor.x + rightChildPos, cursor.y), ImVec2(cursor.x + rightChildPos, cursor.y + itemWidth * 0.5f + itemSpacing), 0xFFFFFFFF);
            ImGui::SameLine();
            ImGui::Spacing();
            ImGui::SameLine(0, (itemWidth + itemSpacing) * ((1u << (cbt.GetMaxDepth() - d)) - 1));
            ImGui::PopID();
            heapID++;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    for (uint32_t leafIndex = 0; leafIndex < cbt.NumBitfieldBits(); leafIndex++)
    {
        ImGui::PushID(10000 + leafIndex);
        uint32_t index = (int)Math::Exp2(cbt.GetMaxDepth()) + leafIndex;
        if (ImGui::Button(Sprintf("%d", cbt.GetData(index)).c_str(), ImVec2(itemWidth, itemWidth)))
        {
            cbt.SetData(index, !cbt.GetData(index));
        }
        ImGui::SameLine();
        ImGui::PopID();
    }

    ImGui::PopStyleVar();
    ImGui::Spacing();

    cPos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddQuadFilled(
        cPos + ImVec2(0, 0),
        cPos + ImVec2(scale, 0),
        cPos + ImVec2(scale, scale),
        cPos + ImVec2(0, scale),
        ImColor(1.0f, 1.0f, 1.0f, 0.5f));

    if (alwaysUpdate || Input::Instance().IsMouseDown(VK_LBUTTON))
    {
        PROFILE_SCOPE("CBT Update");
        cbt.IterateLeaves([&](uint32_t heapIndex)
        {
            Vector2 relMousePos = Input::Instance().GetMousePosition() - Vector2(cPos.x, cPos.y);
        
            if (splitting && LEB::PointInTriangle(relMousePos, heapIndex, scale))
            {
                LEB::CBTSplitConformed(cbt, heapIndex);
            }
        
            if (!CBT::IsRootNode(heapIndex))
            {
                LEB::DiamondIDs diamond = LEB::GetDiamond(heapIndex);
                if (merging && !LEB::PointInTriangle(relMousePos, diamond.Base, scale) && !LEB::PointInTriangle(relMousePos, diamond.Top, scale))
                {
                    LEB::CBTMergeConformed(cbt, heapIndex);
                }
            }
        });
    }

    cbt.SumReduction();

    auto LEBTriangle = [&](uint32_t heapIndex, Color color, float scale)
    {
        Vector3 a, b, c;
        LEB::GetTriangleVertices(heapIndex, a, b, c);
        a *= scale;
        b *= scale;
        c *= scale;

        ImGui::GetWindowDrawList()->AddTriangle(
            cPos + ImVec2(a.x, a.y),
            cPos + ImVec2(b.x, b.y),
            cPos + ImVec2(c.x, c.y),
            ImColor(color.x, color.y, color.z, color.w));

        ImVec2 pos = (ImVec2(a.x, a.y) + ImVec2(b.x, b.y) + ImVec2(c.x, c.y)) / 3;
        std::string text = std::format("{}", heapIndex);
        ImVec2 textPos = cPos + pos - ImGui::CalcTextSize(text.c_str()) * 0.5f;
        ImGui::GetWindowDrawList()->AddText(textPos, ImColor(1.0f, 0.0f, 1.0f, 1.0f), text.c_str());
    };

    PROFILE_SCOPE("CBT Draw");
    cbt.IterateLeaves([&](uint32_t heapIndex)
    {
        LEBTriangle(heapIndex, Color(1, 0, 0, 0.5f), scale);
    });

    ImGui::End();
}
