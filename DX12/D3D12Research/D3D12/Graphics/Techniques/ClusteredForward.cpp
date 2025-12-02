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
#include "DemoApp.h"
#include "Core/ConsoleVariables.h"

static constexpr int gLightClusterTexelSize = 64;
static constexpr int gLightClusterNumZ = 32;

static constexpr int gVolumetricFroxelTexelSize = 8;
static constexpr int gVolumetricNumZSlices =128;

namespace Tweakables
{
    extern ConsoleVariable<int> g_SsrSamples;
}

bool g_VisualizeClusters = false;

ClusteredForward::ClusteredForward(GraphicsDevice* pGraphicsDevice)
{
    SetupResources(pGraphicsDevice);
    SetupPipelines(pGraphicsDevice);
}

void ClusteredForward::OnSwapChainCreated(int windowWidth, int windowHeight)
{
	m_ClusterCountX = Math::RoundUp((float)windowWidth / gLightClusterTexelSize);
	m_ClusterCountY = Math::RoundUp((float)windowHeight / gLightClusterTexelSize);
    m_MaxClusters = m_ClusterCountX * m_ClusterCountY * gLightClusterNumZ;

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

	TextureDesc volumeDesc = TextureDesc::Create3D(Math::DivideAndRoundUp(windowWidth, gVolumetricFroxelTexelSize),
		Math::DivideAndRoundUp(windowHeight, gVolumetricFroxelTexelSize),
		gVolumetricNumZSlices,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		TextureFlag::ShaderResource | TextureFlag::UnorderedAccess);
	m_pLightScatteringVolume[0]->Create(volumeDesc);
	m_pLightScatteringVolume[1]->Create(volumeDesc);
	m_pFinalVolumeFog->Create(volumeDesc);

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

void ClusteredForward::Execute(RGGraph& graph, const SceneData& inputResource)
{
    RG_GRAPH_SCOPE("Clustered Lighting", graph);

    Vector2 screenDimensions((float)inputResource.pRenderTarget->GetWidth(), (float)inputResource.pRenderTarget->GetHeight());
    float nearZ = inputResource.pCamera->GetNear();
    float farZ = inputResource.pCamera->GetFar();
    Vector2 lightGridParams = ComputeVolumeGridParams(nearZ, farZ, gLightClusterNumZ);

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
                constantBuffer.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
                constantBuffer.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, gLightClusterNumZ);

                context.SetComputeDynamicConstantBufferView(0, constantBuffer);
                context.BindResource(1, 0, m_pAabbBuffer->GetUAV());

                // Cluster count in z is 32 so fits nicely in a wavefront on Nvidia so make groupsize in shader 32
                context.Dispatch(m_ClusterCountX, m_ClusterCountY, Math::DivideAndRoundUp(gLightClusterNumZ, 32));
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
				int VertexBuffer;
            } perObjectParameters{};

			perFrameParameters.LightGridParams = lightGridParams;
			perFrameParameters.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, gLightClusterNumZ);
			perFrameParameters.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
            perFrameParameters.View = inputResource.pCamera->GetView();
            perFrameParameters.ViewProjection = inputResource.pCamera->GetViewProjection();
            
            context.SetGraphicsDynamicConstantBufferView(1, perFrameParameters);
            context.BindResource(2, 0, m_pUniqueClusterBuffer->GetUAV());
			context.BindResourceTable(3, inputResource.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Graphics);

			{
                GPU_PROFILE_SCOPE("Opaque", &context);
                context.SetPipelineState(m_pMarkUniqueClustersOpaquePSO);
				DrawScene(context, inputResource, Batch::Blending::Opaque);
			}
            
			{
                GPU_PROFILE_SCOPE("Transparent", &context);
                context.SetPipelineState(m_pMarkUniqueClustersTransparentPSO);
				DrawScene(context, inputResource, Batch::Blending::AlphaMask);
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
            context.InsertResourceBarrier(inputResource.pLightBuffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

            context.ClearUavUInt(m_pLightGrid.get(), m_pLightGridRawUAV);
            context.ClearUavUInt(m_pLightIndexCounter.get(), m_pLightIndexCounter->GetUAV());

            struct ConstantBuffer
            {
                Matrix View;
                int LightCount{ 0 };
            } constantBuffer{};

            constantBuffer.View = inputResource.pCamera->GetView();
            constantBuffer.LightCount = inputResource.pLightBuffer->GetNumElements();

            context.SetComputeDynamicConstantBufferView(0, constantBuffer);
            context.BindResource(1, 0, inputResource.pLightBuffer->GetSRV());
            context.BindResource(1, 1, m_pAabbBuffer->GetSRV());
            context.BindResource(1, 2, m_pCompactedClusterBuffer->GetSRV());
            context.BindResource(2, 0, m_pLightIndexCounter->GetUAV());
            context.BindResource(2, 1, m_pLightIndexGrid->GetUAV());
            context.BindResource(2, 2, m_pLightGrid->GetUAV());

            context.ExecuteIndirect(m_pLightCullingCommandSignature.get(), 1, m_pIndirectArguments.get(), nullptr);
    });

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

		RGPassBuilder injectVolumeLighting = graph.AddPass("Inject Volumetric Lights");
		injectVolumeLighting.Bind([=](CommandContext& context, const RGPassResource& passResource)
			{
				context.InsertResourceBarrier(pSourceVolume, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(pDestinationVolume, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetComputeRootSignature(m_pVolumetricLightingRS.get());
				context.SetPipelineState(m_pInjectVolumeLightPSO);

				D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
					inputResource.pLightBuffer->GetSRV()->GetDescriptor(),
					inputResource.pLightBuffer->GetSRV()->GetDescriptor(),
					inputResource.pAO->GetSRV()->GetDescriptor(),
					inputResource.pResolvedDepth->GetSRV()->GetDescriptor(),
				};

				context.SetComputeDynamicConstantBufferView(0, constantBuffer);
				context.SetComputeDynamicConstantBufferView(1, *inputResource.pShadowData);
				context.BindResource(2, 0, pDestinationVolume->GetUAV());
				context.BindResources(3, 0, srvs, std::size(srvs));
				context.BindResourceTable(4, inputResource.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Compute);

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pDestinationVolume->GetWidth(), 8, pDestinationVolume->GetHeight(), 8, pDestinationVolume->GetDepth(), 4));
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

                context.Dispatch(ComputeUtils::GetNumThreadGroups(pDestinationVolume->GetWidth(), 8,pDestinationVolume->GetHeight(), 8));
            });
	}
    
    RGPassBuilder basePass = graph.AddPass("Base Pass");
    basePass.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
			struct PerObjectData
			{
				Matrix World;
                MaterialData Material;
				uint32_t VertexBuffer;
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
			};
			context.BindResources(4, 0, srvs, std::size(srvs));

            {
                GPU_PROFILE_SCOPE("Opaque", &context);
                context.SetPipelineState(m_pDiffusePSO);
				DrawScene(context, inputResource, Batch::Blending::Opaque | Batch::Blending::AlphaMask);
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
                context.SetGraphicsDynamicConstantBufferView(0, p);
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

void ClusteredForward::VisualizeLightDensity(RGGraph& graph, GraphicsDevice* pGraphicsDevice, Camera& camera, GraphicsTexture* pTarget, GraphicsTexture* pDepth)
{
    if (!m_pVisualizeIntermediateTexture)
    {
        m_pVisualizeIntermediateTexture = std::make_unique<GraphicsTexture>(pGraphicsDevice, "LightDensity Debug Texture");
    }

    if (m_pVisualizeIntermediateTexture->GetDesc() != pTarget->GetDesc())
    {
        m_pVisualizeIntermediateTexture->Create(pTarget->GetDesc());
    }

    float nearZ = camera.GetNear();
    float farZ = camera.GetFar();
    Vector2 lightGridParams = ComputeVolumeGridParams(nearZ, farZ, gLightClusterNumZ);

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
            constantData.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, gLightClusterNumZ);
            constantData.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
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

            context.SetComputeDynamicConstantBufferView(0, constantData);

            context.BindResource(1, 0, pTarget->GetSRV());
            context.BindResource(1, 1, pDepth->GetSRV());
            context.BindResource(1, 2, m_pLightGrid->GetSRV());
            
            context.BindResource(2, 0, m_pVisualizeIntermediateTexture->GetUAV());

            context.Dispatch(Math::DivideAndRoundUp(pTarget->GetWidth(), 16), Math::DivideAndRoundUp(pTarget->GetHeight(), 16));
            context.InsertUavBarrier();

            context.CopyTexture(m_pVisualizeIntermediateTexture.get(), pTarget);
        });
}

void ClusteredForward::SetupResources(GraphicsDevice* pGraphicsDevice)
{
    m_pAabbBuffer = std::make_unique<Buffer>(pGraphicsDevice, "AABBs");
    m_pUniqueClusterBuffer = std::make_unique<Buffer>(pGraphicsDevice, "Unique Clusters");
    m_pCompactedClusterBuffer = std::make_unique<Buffer>(pGraphicsDevice, "Compacted Cluster");
    m_pDebugCompactedClusterBuffer = std::make_unique<Buffer>(pGraphicsDevice, "Debug Compacted Cluster");
	
    m_pIndirectArguments = std::make_unique<Buffer>(pGraphicsDevice, "Light Culling Indirect Arguments");
    m_pIndirectArguments->Create(BufferDesc::CreateIndirectArgumemnts<uint32_t>(3));

	m_pLightIndexCounter = std::make_unique<Buffer>(pGraphicsDevice, "Light Index Counter");
	m_pLightIndexCounter->Create(BufferDesc::CreateByteAddress(sizeof(uint32_t)));
	m_pLightIndexGrid = std::make_unique<Buffer>(pGraphicsDevice, "Light Index Grid");
	
    m_pLightGrid = std::make_unique<Buffer>(pGraphicsDevice, "Light Grid");
    m_pDebugLightGrid = std::make_unique<Buffer>(pGraphicsDevice, "Debug Light Grid");

	m_pLightScatteringVolume[0] = std::make_unique<GraphicsTexture>(pGraphicsDevice, "Light Scattering Volume");
	m_pLightScatteringVolume[1] = std::make_unique<GraphicsTexture>(pGraphicsDevice, "Light Scattering Volume");
	m_pFinalVolumeFog = std::make_unique<GraphicsTexture>(pGraphicsDevice, "Final Light Scattering Volume");

    CommandContext* pContext = pGraphicsDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
    m_pHeatMapTexture = std::make_unique<GraphicsTexture>(pGraphicsDevice, "Heatmap texture");
    m_pHeatMapTexture->Create(pContext, "Resources/textures/HeatMap.png");
    pContext->Execute(true);
}

void ClusteredForward::SetupPipelines(GraphicsDevice* pGraphicsDevice)
{
    // AABB
    {
        Shader* pComputeShader = pGraphicsDevice->GetShader("ClusterAABBGeneration.hlsl", ShaderType::Compute, "GenerateAABBs");

        m_pCreateAabbRS = std::make_unique<RootSignature>(pGraphicsDevice);
        m_pCreateAabbRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        m_pCreateAabbRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pCreateAabbRS->Finalize("Create AABBs", D3D12_ROOT_SIGNATURE_FLAG_NONE);

        PipelineStateInitializer psoDesc;
        psoDesc.SetComputeShader(pComputeShader);
        psoDesc.SetRootSignature(m_pCreateAabbRS->GetRootSignature());
        psoDesc.SetName("Create AABBs");
        m_pCreateAabbPSO = pGraphicsDevice->CreatePipeline(psoDesc);
    }

    // mark unique clusters
    {
        Shader* pVertexShader = pGraphicsDevice->GetShader("ClusterMarking.hlsl", ShaderType::Vertex, "MarkClusters_VS");
		Shader* pPixelShaderOpaque = pGraphicsDevice->GetShader("ClusterMarking.hlsl", ShaderType::Pixel, "MarkClusters_PS");

        m_pMarkUniqueClustersRS = std::make_unique<RootSignature>(pGraphicsDevice);
        m_pMarkUniqueClustersRS->FinalizeFromShader("Mark Unique Clusters", pVertexShader);

        PipelineStateInitializer psoDesc;
        psoDesc.SetVertexShader(pVertexShader);
        psoDesc.SetPixelShader(pPixelShaderOpaque);
        psoDesc.SetBlendMode(BlendMode::Replace, false);
        psoDesc.SetRootSignature(m_pMarkUniqueClustersRS->GetRootSignature());
        psoDesc.SetRenderTargetFormats(nullptr, 0, GraphicsDevice::DEPTH_STENCIL_FORMAT, pGraphicsDevice->GetMultiSampleCount());
		psoDesc.SetDepthWrite(false);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);  // mark cluster must need GREATER_EQUAL on reverse-Z depth
		psoDesc.SetName("Mark Unique Opaque Clusters");
		m_pMarkUniqueClustersOpaquePSO = pGraphicsDevice->CreatePipeline(psoDesc);

		psoDesc.SetName("Mark Unique Transparent Clusters");
		m_pMarkUniqueClustersTransparentPSO = pGraphicsDevice->CreatePipeline(psoDesc);
	}

	// compact cluster list
	{
		Shader* pComputeShader = pGraphicsDevice->GetShader("ClusterCompaction.hlsl", ShaderType::Compute, "CompactClusters");

		m_pCompactClusterListRS = std::make_unique<RootSignature>(pGraphicsDevice);
		m_pCompactClusterListRS->FinalizeFromShader("Compact Cluster List", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pCompactClusterListRS->GetRootSignature());
		psoDesc.SetName("Compact Cluster List");
		m_pCompactClusterListPSO = pGraphicsDevice->CreatePipeline(psoDesc);
	}

	// prepare indirect dispatch buffer
	{
		Shader* pComputeShader = pGraphicsDevice->GetShader("ClusterLightCullingArguments.hlsl", ShaderType::Compute, "UpdateIndirectArguments");

		m_pUpdateIndirectArgumentsRS = std::make_unique<RootSignature>(pGraphicsDevice);
		m_pUpdateIndirectArgumentsRS->FinalizeFromShader("Update Indirect Dispatch Buffer", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pUpdateIndirectArgumentsRS->GetRootSignature());
		psoDesc.SetName("Update Indirect Dispatch Buffer");
		m_pUpdateIndirectArgumentsPSO = pGraphicsDevice->CreatePipeline(psoDesc);
	}

	// light culling
	{
		Shader* pComputeShader = pGraphicsDevice->GetShader("ClusterLightCulling.hlsl", ShaderType::Compute, "LightCulling");

		m_pLightCullingRS = std::make_unique<RootSignature>(pGraphicsDevice);
		m_pLightCullingRS->FinalizeFromShader("Light Culling", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pLightCullingRS->GetRootSignature());
		psoDesc.SetName("Light Culling");
		m_pLightCullingPSO = pGraphicsDevice->CreatePipeline(psoDesc);

		m_pLightCullingCommandSignature = std::make_unique<CommandSignature>(pGraphicsDevice);
		m_pLightCullingCommandSignature->AddDispatch();
		m_pLightCullingCommandSignature->Finalize("Light Culling Command Signature");
	}

	// diffuse
	{
		Shader* pVertexShader = pGraphicsDevice->GetShader("Diffuse.hlsl", ShaderType::Vertex, "VSMain", { "CLUSTERED_FORWARD" });
		Shader* pPixelShader = pGraphicsDevice->GetShader("Diffuse.hlsl", ShaderType::Pixel, "PSMain", { "CLUSTERED_FORWARD" });

		m_pDiffuseRS = std::make_unique<RootSignature>(pGraphicsDevice);
		m_pDiffuseRS->FinalizeFromShader("Diffuse", pVertexShader);

		DXGI_FORMAT formats[] = { GraphicsDevice::RENDER_TARGET_FORMAT, DXGI_FORMAT_R16G16B16A16_FLOAT };

		// opaque
		PipelineStateInitializer psoDesc;
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetBlendMode(BlendMode::Replace, false);
		psoDesc.SetRootSignature(m_pDiffuseRS->GetRootSignature());
		psoDesc.SetRenderTargetFormats(formats, std::size(formats), GraphicsDevice::DEPTH_STENCIL_FORMAT, pGraphicsDevice->GetMultiSampleCount());
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetName("Diffuse (Opaque)");
		m_pDiffusePSO = pGraphicsDevice->CreatePipeline(psoDesc);

		// transparent
		psoDesc.SetBlendMode(BlendMode::Alpha, false);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		psoDesc.SetName("Diffuse (Transparent)");
		m_pDiffuseTransparencyPSO = pGraphicsDevice->CreatePipeline(psoDesc);
	}

	// cluster debug rendering
	{
		Shader* pPixelShader = pGraphicsDevice->GetShader("ClusterDebugDrawing.hlsl", ShaderType::Pixel, "PSMain");

		m_pDebugClusterRS = std::make_unique<RootSignature>(pGraphicsDevice);

		PipelineStateInitializer psoDesc;
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetRenderTargetFormat(GraphicsDevice::RENDER_TARGET_FORMAT, GraphicsDevice::DEPTH_STENCIL_FORMAT, pGraphicsDevice->GetMultiSampleCount());
		psoDesc.SetBlendMode(BlendMode::Add, false);

		if (pGraphicsDevice->GetCapabilities().SupportMeshShading())
        {
            Shader* pMeshShader = pGraphicsDevice->GetShader("ClusterDebugDrawing.hlsl", ShaderType::Mesh, "MSMain");
            
		    m_pDebugClusterRS->FinalizeFromShader("Debug Cluster", pMeshShader);
            
            psoDesc.SetRootSignature(m_pDebugClusterRS->GetRootSignature());
            psoDesc.SetMeshShader(pMeshShader);
			psoDesc.SetName("Debug Cluster PSO");
        }
        else
        {
            Shader* pVertexShader = pGraphicsDevice->GetShader("ClusterDebugDrawing.hlsl", ShaderType::Vertex, "VSMain");
            Shader* pGeometryShader = pGraphicsDevice->GetShader("ClusterDebugDrawing.hlsl", ShaderType::Geometry, "GSMain");
            
		    m_pDebugClusterRS->FinalizeFromShader("Debug Cluster", pVertexShader);
            
            psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
            psoDesc.SetVertexShader(pVertexShader);
            psoDesc.SetGeometryShader(pGeometryShader);
            psoDesc.SetRootSignature(m_pDebugClusterRS->GetRootSignature());
            psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
            psoDesc.SetName("Debug Cluster PSO");
        }

        m_pDebugClusterPSO = pGraphicsDevice->CreatePipeline(psoDesc);
    }

    {
        Shader* pComputeShader = pGraphicsDevice->GetShader("VisualizeLightCount.hlsl", ShaderType::Compute, "DebugLightDensityCS", { "CLUSTERED_FORWARD" });

        m_pVisualizeLightsRS = std::make_unique<RootSignature>(pGraphicsDevice);
        m_pVisualizeLightsRS->FinalizeFromShader("Visualize Light Density RS", pComputeShader);

        PipelineStateInitializer psoDesc;
        psoDesc.SetRootSignature(m_pVisualizeLightsRS->GetRootSignature());
        psoDesc.SetComputeShader(pComputeShader);
        psoDesc.SetName("Visualize Light Density PSO");
        m_pVisualizeLightsPSO = pGraphicsDevice->CreatePipeline(psoDesc);
    }

	{
		Shader* pComputeShader = pGraphicsDevice->GetShader("VolumetricFog.hlsl", ShaderType::Compute, "InjectFogLightingCS", {});

		m_pVolumetricLightingRS = std::make_unique<RootSignature>(pGraphicsDevice);
		m_pVolumetricLightingRS->FinalizeFromShader("Inject Fog Lighting", pComputeShader);

		{
			PipelineStateInitializer psoDesc;
			psoDesc.SetComputeShader(pComputeShader);
			psoDesc.SetRootSignature(m_pVolumetricLightingRS->GetRootSignature());
			psoDesc.SetName("Inject Fog Lighting");
			m_pInjectVolumeLightPSO = pGraphicsDevice->CreatePipeline(psoDesc);
		}

		{
			Shader* pAccumulateComputeShader = pGraphicsDevice->GetShader("VolumetricFog.hlsl", ShaderType::Compute, "AccumulateFogCS", {});
			PipelineStateInitializer psoDesc;
			psoDesc.SetComputeShader(pAccumulateComputeShader);
			psoDesc.SetRootSignature(m_pVolumetricLightingRS->GetRootSignature());
			psoDesc.SetName("Accumulate Fog Lighting");
			m_pAccumulateVolumeLightPSO = pGraphicsDevice->CreatePipeline(psoDesc);
		}
	}
}
