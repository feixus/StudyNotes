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
#include "Graphics/SceneView.h"
#include "Core/ConsoleVariables.h"

static constexpr int gLightClusterTexelSize = 64;
static constexpr int gLightClusterNumZ = 32;
static constexpr int gMaxLightsPerCluster = 32;

static constexpr int gVolumetricFroxelTexelSize = 8;
static constexpr int gVolumetricNumZSlices =128;

namespace Tweakables
{
    extern ConsoleVariable<int> g_SsrSamples;
}

bool g_VisualizeClusters = false;

ClusteredForward::ClusteredForward(GraphicsDevice* pGraphicsDevice)
    : m_pGraphicsDevice(pGraphicsDevice)
{
    SetupPipelines();

     CommandContext* pContext = pGraphicsDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
    m_pHeatMapTexture = std::make_unique<GraphicsTexture>(pGraphicsDevice, "Heatmap texture");
    m_pHeatMapTexture->Create(pContext, "Resources/textures/HeatMap.png");
    pContext->Execute(true);
}

void ClusteredForward::OnResize(int windowWidth, int windowHeight)
{
	m_ClusterCountX = Math::RoundUp((float)windowWidth / gLightClusterTexelSize);
	m_ClusterCountY = Math::RoundUp((float)windowHeight / gLightClusterTexelSize);
    m_MaxClusters = m_ClusterCountX * m_ClusterCountY * gLightClusterNumZ;

    m_pAabbBuffer = m_pGraphicsDevice->CreateBuffer(BufferDesc::CreateStructured(m_MaxClusters, sizeof(Vector4) * 2), "AABBs");
 
    m_pLightIndexGrid = m_pGraphicsDevice->CreateBuffer(BufferDesc::CreateStructured(m_MaxClusters * gMaxLightsPerCluster, sizeof(uint32_t)), "Light Index Grid");
	// LightGrid.x : Offset, LightGrid.y : Count
	m_pLightGrid = m_pGraphicsDevice->CreateBuffer(BufferDesc::CreateStructured(m_MaxClusters * 2, sizeof(uint32_t)), "Light Grid");
    m_pLightGridRawUAV = nullptr;
    m_pLightGrid->CreateUAV(&m_pLightGridRawUAV, BufferUAVDesc::CreateRaw());
    m_pDebugLightGrid = m_pGraphicsDevice->CreateBuffer(m_pLightGrid->GetDesc(), "Debug Light Grid");

	TextureDesc volumeDesc = TextureDesc::Create3D(Math::DivideAndRoundUp(windowWidth, gVolumetricFroxelTexelSize),
		Math::DivideAndRoundUp(windowHeight, gVolumetricFroxelTexelSize),
		gVolumetricNumZSlices,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		TextureFlag::ShaderResource | TextureFlag::UnorderedAccess);
	m_pLightScatteringVolume[0] = m_pGraphicsDevice->CreateTexture(volumeDesc, "Light Scattering Volume 0");
	m_pLightScatteringVolume[1] = m_pGraphicsDevice->CreateTexture(volumeDesc, "Light Scattering Volume 1");
	m_pFinalVolumeFog = m_pGraphicsDevice->CreateTexture(volumeDesc, "Final Light Scattering Volume");

    m_ViewportDirty = true;
}

// logarithmic depth slicing: get z slice index from view space z. 
// (n, f) => (0, N). SliceIndex = A * log(z) - B -> Slice(z)=ln(f/n)N​ln(z)−ln(f/n)Nln(n)​
Vector2 ComputeVolumeGridParams(float nearZ, float farZ, int numSlices)
{
    Vector2 lightGridParams;
    float n = Math::Min(nearZ, farZ);
    float f = Math::Max(nearZ, farZ);
	lightGridParams.x = (float)numSlices / log(f / n);
	lightGridParams.y = (float)numSlices * log(n) / log(f / n);
    return lightGridParams;
}

void ClusteredForward::Execute(RGGraph& graph, const SceneView& inputResource)
{
    RG_GRAPH_SCOPE("Clustered Lighting", graph);

    Vector2 screenDimensions((float)inputResource.pRenderTarget->GetWidth(), (float)inputResource.pRenderTarget->GetHeight());
    float nearZ = inputResource.pCamera->GetNear();
    float farZ = inputResource.pCamera->GetFar();
    Vector2 lightGridParams = ComputeVolumeGridParams(nearZ, farZ, gLightClusterNumZ);

    if (m_ViewportDirty)
    {
        RGPassBuilder calculateAabbs = graph.AddPass("Cluster AABBs");
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
                constantBuffer.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
                constantBuffer.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, gLightClusterNumZ);

                context.SetComputeDynamicConstantBufferView(0, constantBuffer);
                context.BindResource(1, 0, m_pAabbBuffer->GetUAV());

                // Cluster count in z is 32 so fits nicely in a wavefront on Nvidia so make groupsize in shader 32
                constexpr uint32_t threadGroupSize = 32;
                context.Dispatch(ComputeUtils::GetNumThreadGroups(
                    m_ClusterCountX, 1,
                    m_ClusterCountY, 1,
                    gLightClusterNumZ, threadGroupSize
                ));
            });

        m_ViewportDirty = false;
    }

    RGPassBuilder lightCulling = graph.AddPass("Clustered Light Culling");
    lightCulling.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
            context.SetPipelineState(m_pLightCullingPSO);
            context.SetComputeRootSignature(m_pLightCullingRS.get());

            context.InsertResourceBarrier(m_pAabbBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.InsertResourceBarrier(inputResource.pLightBuffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

			// clear the light grid because we're accumulating the light count in the shader
            context.ClearUavUInt(m_pLightGrid.get(), m_pLightGridRawUAV);

            struct ConstantBuffer
            {
                Matrix View;
				IntVector3 ClusterDimensions;
                int LightCount{ 0 };
            } constantBuffer{};

            constantBuffer.View = inputResource.pCamera->GetView();
			constantBuffer.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, gLightClusterNumZ);
            constantBuffer.LightCount = inputResource.pLightBuffer->GetNumElements();

            context.SetComputeDynamicConstantBufferView(0, constantBuffer);
            context.BindResource(1, 0, inputResource.pLightBuffer->GetSRV());
            context.BindResource(1, 1, m_pAabbBuffer->GetSRV());

            context.BindResource(2, 0, m_pLightIndexGrid->GetUAV());
            context.BindResource(2, 1, m_pLightGrid->GetUAV());

            constexpr uint32_t threadGroupSize = 4;
            context.Dispatch(ComputeUtils::GetNumThreadGroups(
                    m_ClusterCountX, threadGroupSize,
                    m_ClusterCountY, threadGroupSize,
                    gLightClusterNumZ, threadGroupSize
            ));
    });

    // why the volumetric lighting is not working???
	{
		RG_GRAPH_SCOPE("Volumetric Lighting", graph);

		GraphicsTexture* pSourceVolume = m_pLightScatteringVolume[inputResource.FrameIndex % 2].get();
		GraphicsTexture* pDestinationVolume = m_pLightScatteringVolume[(inputResource.FrameIndex + 1) % 2].get();

        Matrix premult = {
            2, 0, 0, 0,
            0, -2, 0, 0,
            0, 0, 1, 0,
            -1, 1, 0, 1
        };

        Matrix postmult = {
            0.5f, 0, 0, 0,
            0, -0.5f, 0, 0,
            0, 0, 1, 0,
            0.5f, 0.5f, 0, 1
        };
        Matrix reprojectionMatrix = inputResource.pCamera->GetViewProjection().Invert() * inputResource.pCamera->GetPreviousViewProjection();

        struct ShaderData
        {
            Matrix ViewProjectionInv;
			Matrix Projection;
            Matrix PrevViewProjection;
            IntVector3 ClusterDimensions;
            int NumLights;
            Vector3 InvClusterDimensions;
            float NearZ;
			Vector3 ViewLocation;
            float FarZ;
            float Jitter;
            float LightClusterSizeFactor;
            Vector2 LightGridParams;
            IntVector3 LightClusterDimensions;
        }constantBuffer{};

		constantBuffer.ClusterDimensions = IntVector3(pDestinationVolume->GetWidth(), pDestinationVolume->GetHeight(), pDestinationVolume->GetDepth());
		constantBuffer.InvClusterDimensions = Vector3(1.0f / constantBuffer.ClusterDimensions.x, 1.0f / constantBuffer.ClusterDimensions.y, 1.0f / constantBuffer.ClusterDimensions.z);
		constantBuffer.ViewLocation = inputResource.pCamera->GetPosition();
		constantBuffer.Projection = inputResource.pCamera->GetProjection();
        constantBuffer.PrevViewProjection = inputResource.pCamera->GetPreviousViewProjection();
		constantBuffer.ViewProjectionInv = inputResource.pCamera->GetProjectionInverse() * inputResource.pCamera->GetViewInverse();
		constantBuffer.NumLights = inputResource.pLightBuffer->GetNumElements();
		constantBuffer.NearZ = inputResource.pCamera->GetNear();
		constantBuffer.FarZ = inputResource.pCamera->GetFar();
        constexpr Math::HaltonSequence<1024, 2> halton;
		constantBuffer.Jitter = halton[inputResource.FrameIndex & 1023];
        constantBuffer.LightClusterSizeFactor = (float)gVolumetricFroxelTexelSize / gLightClusterTexelSize;
        constantBuffer.LightGridParams = lightGridParams;
        constantBuffer.LightClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, gLightClusterNumZ);

		RGPassBuilder injectVolumeLighting = graph.AddPass("Inject Volumetric Lights");
		injectVolumeLighting.Bind([=](CommandContext& context, const RGPassResource& passResource)
			{
				context.InsertResourceBarrier(pSourceVolume, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(pDestinationVolume, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetComputeRootSignature(m_pVolumetricLightingRS.get());
				context.SetPipelineState(m_pInjectVolumeLightPSO);

				D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
                    m_pLightGrid->GetSRV()->GetDescriptor(),
                    m_pLightIndexGrid->GetSRV()->GetDescriptor(),
					pSourceVolume->GetSRV()->GetDescriptor(),
					inputResource.pLightBuffer->GetSRV()->GetDescriptor(),
					inputResource.pAO->GetSRV()->GetDescriptor(),
					inputResource.pResolvedDepth->GetSRV()->GetDescriptor(),
				};

				context.SetComputeDynamicConstantBufferView(0, constantBuffer);
				context.SetComputeDynamicConstantBufferView(1, *inputResource.pShadowData);
				context.BindResource(2, 0, pDestinationVolume->GetUAV());
				context.BindResources(3, 0, srvs, std::size(srvs));
				context.BindResourceTable(4, inputResource.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Compute);

                constexpr uint32_t threadGroupSizeXY = 8;
                constexpr uint32_t threadGroupSizeZ = 4;
				context.Dispatch(ComputeUtils::GetNumThreadGroups(
                    pDestinationVolume->GetWidth(), threadGroupSizeXY, 
                    pDestinationVolume->GetHeight(), threadGroupSizeXY, 
                    pDestinationVolume->GetDepth(), threadGroupSizeZ));
			});

        RGPassBuilder accumulateFog = graph.AddPass("Accumulate Volume Fog");
        accumulateFog.Bind([=](CommandContext& context, const RGPassResource& passResource)
            {
                context.InsertResourceBarrier(pDestinationVolume, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                context.InsertResourceBarrier(m_pFinalVolumeFog.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                context.SetComputeRootSignature(m_pVolumetricLightingRS.get());
                context.SetPipelineState(m_pAccumulateVolumeLightPSO);

                // float values[] = { 0, 0, 0, 0 };
                // context.ClearUavFloat(m_pFinalVolumeFog.get(), m_pFinalVolumeFog->GetUAV(), values);

                D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
                    m_pLightGrid->GetSRV()->GetDescriptor(),
                    m_pLightIndexGrid->GetSRV()->GetDescriptor(),
                    pDestinationVolume->GetSRV()->GetDescriptor(),
                    inputResource.pLightBuffer->GetSRV()->GetDescriptor(),
                    inputResource.pAO->GetSRV()->GetDescriptor(),
                    inputResource.pResolvedDepth->GetSRV()->GetDescriptor(),
                };

                context.SetComputeDynamicConstantBufferView(0, constantBuffer);
                context.SetComputeDynamicConstantBufferView(1, *inputResource.pShadowData);
                context.BindResource(2, 0, m_pFinalVolumeFog->GetUAV());
                context.BindResources(3, 0, srvs, std::size(srvs));
                context.BindResourceTable(4, inputResource.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Compute);

                constexpr uint32_t threadGroupSize = 8;
                context.Dispatch(ComputeUtils::GetNumThreadGroups(
                    pDestinationVolume->GetWidth(), threadGroupSize,
                    pDestinationVolume->GetHeight(), threadGroupSize));
            });
	}
    
    RGPassBuilder basePass = graph.AddPass("Base Pass");
    basePass.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
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
				IntVector3 VolumeFogDimensions;
			} frameData{};

			frameData.View = inputResource.pCamera->GetView();
			frameData.Projection = inputResource.pCamera->GetProjection();
            frameData.ProjectionInverse = inputResource.pCamera->GetProjectionInverse();
            frameData.ViewProjection = inputResource.pCamera->GetViewProjection();
            frameData.ViewPosition = Vector4(inputResource.pCamera->GetPosition());
			frameData.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, gLightClusterNumZ);
			frameData.InvScreenDimensions = Vector2(1.0f / screenDimensions.x, 1.0f / screenDimensions.y);
			frameData.NearZ = nearZ;
			frameData.FarZ = farZ;
			frameData.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
			frameData.LightGridParams = lightGridParams;
            frameData.FrameIndex = inputResource.FrameIndex;
            frameData.SsrSamples = Tweakables::g_SsrSamples;
			frameData.LightCount = inputResource.pLightBuffer->GetNumElements();
			frameData.VolumeFogDimensions = IntVector3(m_pFinalVolumeFog->GetWidth(), m_pFinalVolumeFog->GetHeight(), m_pFinalVolumeFog->GetDepth());

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
			context.InsertResourceBarrier(m_pFinalVolumeFog.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

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

            context.SetGraphicsDynamicConstantBufferView(1, frameData);
            context.SetGraphicsDynamicConstantBufferView(2, *inputResource.pShadowData);
            context.BindResourceTable(3, inputResource.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Graphics);

			D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
				m_pFinalVolumeFog->GetSRV()->GetDescriptor(),
				m_pLightGrid->GetSRV()->GetDescriptor(),
				m_pLightIndexGrid->GetSRV()->GetDescriptor(),
				inputResource.pLightBuffer->GetSRV()->GetDescriptor(),
				inputResource.pAO->GetSRV()->GetDescriptor(),
				inputResource.pResolvedDepth->GetSRV()->GetDescriptor(),
				inputResource.pPreviousColor->GetSRV()->GetDescriptor(),
                inputResource.pMaterialBuffer->GetSRV()->GetDescriptor(),
				inputResource.pMaterialBuffer->GetSRV()->GetDescriptor(),
                inputResource.pMeshBuffer->GetSRV()->GetDescriptor(),
                inputResource.pMeshInstanceBuffer->GetSRV()->GetDescriptor(),
			};
			context.BindResources(4, 0, srvs, std::size(srvs));

            {
                GPU_PROFILE_SCOPE("Opaque", &context);
                context.SetPipelineState(m_pDiffusePSO);
				DrawScene(context, inputResource, Batch::Blending::Opaque);
            }

            {
                GPU_PROFILE_SCOPE("Opaque - Masked", &context);
                context.SetPipelineState(m_pDiffuseMaskedPSO);
				DrawScene(context, inputResource, Batch::Blending::AlphaMask);
            }

            {
                GPU_PROFILE_SCOPE("Transparent", &context);
                context.SetPipelineState(m_pDiffuseTransparencyPSO);
				DrawScene(context, inputResource, Batch::Blending::AlphaBlend);
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
                    context.CopyTexture(m_pLightGrid.get(), m_pDebugLightGrid.get());
                    m_DebugClusterViewMatrix = inputResource.pCamera->GetView();
                    m_DebugClusterViewMatrix.Invert(m_DebugClusterViewMatrix);
                    m_DidCopyDebugClusterData = true;
                }

                context.BeginRenderPass(RenderPassInfo(inputResource.pRenderTarget, RenderPassAccess::Load_Store, inputResource.pDepthBuffer, RenderPassAccess::Load_Store, false));

                context.SetPipelineState(m_pVisualizeLightClustersPSO);
                context.SetGraphicsRootSignature(m_pVisualizeLightClustersRS.get());
                context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

                struct ConstantBuffer
                {
                    Matrix View;
                } constantBuffer;
                constantBuffer.View = m_DebugClusterViewMatrix * inputResource.pCamera->GetViewProjection();
                context.SetGraphicsDynamicConstantBufferView(0, constantBuffer);
                
                D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
                    m_pAabbBuffer->GetSRV()->GetDescriptor(),
                    m_pDebugLightGrid->GetSRV()->GetDescriptor(),
                    m_pHeatMapTexture->GetSRV()->GetDescriptor()
                };
                context.BindResources(1, 0, srvs, std::size(srvs));

                context.Draw(0, m_ClusterCountX * m_ClusterCountY * gLightClusterNumZ);

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
    if (!m_pVisualizeIntermediateTexture || m_pVisualizeIntermediateTexture->GetDesc() != pTarget->GetDesc())
    {
        m_pVisualizeIntermediateTexture = m_pGraphicsDevice->CreateTexture(pTarget->GetDesc(), "LightDensity Debug Texture");
    }

    float nearZ = camera.GetNear();
    float farZ = camera.GetFar();
    Vector2 lightGridParams = ComputeVolumeGridParams(nearZ, farZ, gLightClusterNumZ);

    RGPassBuilder visualizePass = graph.AddPass("Visualize Light Density");
    visualizePass.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
            struct ConstantBuffer
            {
                Matrix ProjectionInverse;
                IntVector3 ClusterDimensions;
                float padding;
                IntVector2 ClusterSize;
                Vector2 LightGridParams;
                float Near;
                float Far;
                float FoV;
            } constantBuffer{};

            constantBuffer.ProjectionInverse = camera.GetProjectionInverse();
            constantBuffer.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, gLightClusterNumZ);
            constantBuffer.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
            constantBuffer.LightGridParams = lightGridParams;
            constantBuffer.Near = nearZ;
            constantBuffer.Far = farZ;
            constantBuffer.FoV = camera.GetFoV();

            context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.InsertResourceBarrier(m_pVisualizeIntermediateTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            context.SetPipelineState(m_pVisualizeLightsPSO);
            context.SetComputeRootSignature(m_pVisualizeLightsRS.get());

            context.SetComputeDynamicConstantBufferView(0, constantBuffer);

            context.BindResource(1, 0, pTarget->GetSRV());
            context.BindResource(1, 1, pDepth->GetSRV());
            context.BindResource(1, 2, m_pLightGrid->GetSRV());
            
            context.BindResource(2, 0, m_pVisualizeIntermediateTexture->GetUAV());

            context.Dispatch(ComputeUtils::GetNumThreadGroups(
                pTarget->GetWidth(), 16,
                pTarget->GetHeight(), 16
            ));
            context.InsertUavBarrier();

            context.CopyTexture(m_pVisualizeIntermediateTexture.get(), pTarget);
        });
}

void ClusteredForward::SetupPipelines()
{
    // AABB
    {
        Shader* pComputeShader = m_pGraphicsDevice->GetShader("ClusterAABBGeneration.hlsl", ShaderType::Compute, "GenerateAABBs");

        m_pCreateAabbRS = std::make_unique<RootSignature>(m_pGraphicsDevice);
        m_pCreateAabbRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        m_pCreateAabbRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pCreateAabbRS->Finalize("Create AABBs", D3D12_ROOT_SIGNATURE_FLAG_NONE);

        PipelineStateInitializer psoDesc;
        psoDesc.SetComputeShader(pComputeShader);
        psoDesc.SetRootSignature(m_pCreateAabbRS->GetRootSignature());
        psoDesc.SetName("Create AABBs");
        m_pCreateAabbPSO = m_pGraphicsDevice->CreatePipeline(psoDesc);
    }

	// light culling
	{
		Shader* pComputeShader = m_pGraphicsDevice->GetShader("ClusterLightCulling.hlsl", ShaderType::Compute, "LightCulling");

		m_pLightCullingRS = std::make_unique<RootSignature>(m_pGraphicsDevice);
		m_pLightCullingRS->FinalizeFromShader("Light Culling", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pLightCullingRS->GetRootSignature());
		psoDesc.SetName("Light Culling");
		m_pLightCullingPSO = m_pGraphicsDevice->CreatePipeline(psoDesc);

		m_pLightCullingCommandSignature = std::make_unique<CommandSignature>(m_pGraphicsDevice);
		m_pLightCullingCommandSignature->AddDispatch();
		m_pLightCullingCommandSignature->Finalize("Light Culling Command Signature");
	}

	// diffuse
	{
		Shader* pVertexShader = m_pGraphicsDevice->GetShader("Diffuse.hlsl", ShaderType::Vertex, "VSMain", { "CLUSTERED_FORWARD" });
		Shader* pPixelShader = m_pGraphicsDevice->GetShader("Diffuse.hlsl", ShaderType::Pixel, "PSMain", { "CLUSTERED_FORWARD" });

		m_pDiffuseRS = std::make_unique<RootSignature>(m_pGraphicsDevice);
		m_pDiffuseRS->FinalizeFromShader("Diffuse", pVertexShader);

		DXGI_FORMAT formats[] = { GraphicsDevice::RENDER_TARGET_FORMAT, DXGI_FORMAT_R16G16B16A16_FLOAT };

		// opaque
		PipelineStateInitializer psoDesc;
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetBlendMode(BlendMode::Replace, false);
		psoDesc.SetRootSignature(m_pDiffuseRS->GetRootSignature());
		psoDesc.SetRenderTargetFormats(formats, std::size(formats), GraphicsDevice::DEPTH_STENCIL_FORMAT, m_pGraphicsDevice->GetMultiSampleCount());
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetName("Diffuse (Opaque)");
		m_pDiffusePSO = m_pGraphicsDevice->CreatePipeline(psoDesc);

        // opaque masked
        psoDesc.SetName("Diffuse Masked (Opaque)");
        psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
        m_pDiffuseMaskedPSO = m_pGraphicsDevice->CreatePipeline(psoDesc);

		// transparent
		psoDesc.SetName("Diffuse (Transparent)");
		psoDesc.SetBlendMode(BlendMode::Alpha, false);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pDiffuseTransparencyPSO = m_pGraphicsDevice->CreatePipeline(psoDesc);
	}

	// cluster debug rendering
	{
		Shader* pVertexShader = m_pGraphicsDevice->GetShader("VisualizeLightClusters.hlsl", ShaderType::Vertex, "VSMain");
		Shader* pGeometryShader = m_pGraphicsDevice->GetShader("VisualizeLightClusters.hlsl", ShaderType::Geometry, "GSMain");
		Shader* pPixelShader = m_pGraphicsDevice->GetShader("VisualizeLightClusters.hlsl", ShaderType::Pixel, "PSMain");

		m_pVisualizeLightClustersRS = std::make_unique<RootSignature>(m_pGraphicsDevice);

		PipelineStateInitializer psoDesc;
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetRenderTargetFormat(GraphicsDevice::RENDER_TARGET_FORMAT, GraphicsDevice::DEPTH_STENCIL_FORMAT, m_pGraphicsDevice->GetMultiSampleCount());
		psoDesc.SetBlendMode(BlendMode::Add, false);

        m_pVisualizeLightClustersRS->FinalizeFromShader("Visualize Cluster", pVertexShader);

		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
        psoDesc.SetRootSignature(m_pVisualizeLightClustersRS->GetRootSignature());
        psoDesc.SetVertexShader(pVertexShader);
        psoDesc.SetGeometryShader(pGeometryShader);
        psoDesc.SetName("Visualize Cluster PSO");

        m_pVisualizeLightClustersPSO = m_pGraphicsDevice->CreatePipeline(psoDesc);
    }

    {
        Shader* pComputeShader = m_pGraphicsDevice->GetShader("VisualizeLightCount.hlsl", ShaderType::Compute, "DebugLightDensityCS", { "CLUSTERED_FORWARD" });

        m_pVisualizeLightsRS = std::make_unique<RootSignature>(m_pGraphicsDevice);
        m_pVisualizeLightsRS->FinalizeFromShader("Visualize Light Density RS", pComputeShader);

        PipelineStateInitializer psoDesc;
        psoDesc.SetRootSignature(m_pVisualizeLightsRS->GetRootSignature());
        psoDesc.SetComputeShader(pComputeShader);
        psoDesc.SetName("Visualize Light Density PSO");
        m_pVisualizeLightsPSO = m_pGraphicsDevice->CreatePipeline(psoDesc);
    }

	{
		Shader* pComputeShader = m_pGraphicsDevice->GetShader("VolumetricFog.hlsl", ShaderType::Compute, "InjectFogLightingCS", {});

		m_pVolumetricLightingRS = std::make_unique<RootSignature>(m_pGraphicsDevice);
		m_pVolumetricLightingRS->FinalizeFromShader("Inject Fog Lighting", pComputeShader);

		{
			PipelineStateInitializer psoDesc;
			psoDesc.SetComputeShader(pComputeShader);
			psoDesc.SetRootSignature(m_pVolumetricLightingRS->GetRootSignature());
			psoDesc.SetName("Inject Fog Lighting");
			m_pInjectVolumeLightPSO = m_pGraphicsDevice->CreatePipeline(psoDesc);
		}

		{
			Shader* pAccumulateComputeShader = m_pGraphicsDevice->GetShader("VolumetricFog.hlsl", ShaderType::Compute, "AccumulateFogCS", {});
			PipelineStateInitializer psoDesc;
			psoDesc.SetComputeShader(pAccumulateComputeShader);
			psoDesc.SetRootSignature(m_pVolumetricLightingRS->GetRootSignature());
			psoDesc.SetName("Accumulate Fog Lighting");
			m_pAccumulateVolumeLightPSO = m_pGraphicsDevice->CreatePipeline(psoDesc);
		}
	}
}
