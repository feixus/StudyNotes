#include "stdafx.h"
#include "Clouds.h"
#include "Scene/Camera.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Profiler.h"
#include "Graphics/ImGuiRenderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"

static const int Resolution = 128;
static const int MaxPoints = 1024;
static Vector4 NoiseWeights = Vector4(0.625f, 0.225f, 0.15f, 0.05f);

struct CloudParameters
{
	Vector4 NoiseWeights;
	Vector4 FrustumCorners[4];
	Matrix ViewInverse;
	float NearPlane{0};
	float FarPlane{0};
	
	float CloudScale{0.004f};
	float CloudThreshold{0.4f};
	Vector3 CloudOffset;
	float CloudDensity{0.3f};

	Vector4 MinExtents;
	Vector4 MaxExtents;

	Vector4 SunDirection;
	Vector4 SunColor;
};

static CloudParameters sCloudParameters;

Clouds::Clouds(Graphics* pGraphics)
{
	m_CloudBounds.Center = Vector3(0, 200, 0);
	m_CloudBounds.Extents = Vector3(300, 20, 300);

	Initialize(pGraphics);
}

void Clouds::Initialize(Graphics* pGraphics)
{
	GraphicsDevice* pGraphicsDevice = pGraphics->GetDevice();
	ShaderManager* pShaderManager = pGraphics->GetShaderManager();

	pGraphics->GetImGui()->AddUpdateCallback(ImGuiCallbackDelegate::CreateLambda([this]() {
		ImGui::Begin("Parameters");
		ImGui::Text("Clouds");
		ImGui::SliderFloat3("Position", reinterpret_cast<float*>(&m_CloudBounds.Center), 0.f, 500.f);
		ImGui::SliderFloat3("Extents", reinterpret_cast<float*>(&m_CloudBounds.Extents), 0.f, 500.f);
		ImGui::SliderFloat("Scale", &sCloudParameters.CloudScale, 0, 0.02f);
		ImGui::SliderFloat("Cloud Threshold", &sCloudParameters.CloudThreshold, 0, 0.5f);
		ImGui::SliderFloat("Density", &sCloudParameters.CloudDensity, 0, 1.0f);
		ImGui::SliderFloat4("Noise Weights", &NoiseWeights.x, 0, 1);
		if (ImGui::Button("Generate Noise"))
		{
			m_UpdateNoise = true;
		}
		ImGui::End();
	}));

	{
		Shader* pShader = pShaderManager->GetShader("WorleyNoise.hlsl", ShaderType::Compute, "WorleyNoiseCS");
		
		m_pWorleyNoiseRS = std::make_unique<RootSignature>(pGraphicsDevice);
		m_pWorleyNoiseRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pWorleyNoiseRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pWorleyNoiseRS->Finalize("Worley Noise RS", D3D12_ROOT_SIGNATURE_FLAG_NONE);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pShader);
		psoDesc.SetRootSignature(m_pWorleyNoiseRS->GetRootSignature());
		psoDesc.SetName("Worley Noise PS");
		m_pWorleyNoisePS = pGraphicsDevice->CreatePipeline(psoDesc);

		m_pWorleyNoiseTexture = std::make_unique<GraphicsTexture>(pGraphicsDevice, "Worley Noise");
		m_pWorleyNoiseTexture->Create(TextureDesc::Create3D(Resolution, Resolution, Resolution, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));
		m_pWorleyNoiseTexture->SetName("Worley Noise Texture");
	}
	{
		Shader* pvVertexShader = pShaderManager->GetShader("Clouds.hlsl", ShaderType::Vertex, "VSMain");
		Shader* pPixelShader = pShaderManager->GetShader("Clouds.hlsl", ShaderType::Pixel, "PSMain");
		m_pCloudsRS = std::make_unique<RootSignature>(pGraphicsDevice);
		m_pCloudsRS->FinalizeFromShader("Clouds RS", pvVertexShader);

		VertexElementLayout inputLayout;
		inputLayout.AddVertexElement("POSITION", DXGI_FORMAT_R32G32B32_FLOAT);
		inputLayout.AddVertexElement("TEXCOORD", DXGI_FORMAT_R32G32_FLOAT);

		PipelineStateInitializer psoDesc;
		psoDesc.SetVertexShader(pvVertexShader);
		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetInputLayout(inputLayout);
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		psoDesc.SetDepthEnable(false);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, pGraphicsDevice->GetMultiSampleCount());
		psoDesc.SetRootSignature(m_pCloudsRS->GetRootSignature());
		psoDesc.SetName("Clouds PS");
		m_pCloudsPS = pGraphicsDevice->CreatePipeline(psoDesc);
	}

	CommandContext* pContext = pGraphicsDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	{
		struct Vertex
		{
			Vector3 Position;
			Vector2 TexCoord;
		};

		Vertex vertices[] = {
			{ Vector3(-1, 1, 0), Vector2(0, 0) },
			{ Vector3(1, 1, 1), Vector2(1, 0) },
			{ Vector3(-1, -1, 3), Vector2(0, 1) },
			{ Vector3(-1, -1, 3), Vector2(0, 1) },
			{ Vector3(1, 1, 1), Vector2(1, 0) },
			{ Vector3(1, -1, 2), Vector2(1, 1) },
		};

		m_pQuadVertexBuffer = std::make_unique<Buffer>(pGraphicsDevice, "Quad Vertex Buffer");
		m_pQuadVertexBuffer->Create(BufferDesc::CreateVertexBuffer(6, sizeof(Vertex)));
		m_pQuadVertexBuffer->SetData(pContext, vertices, sizeof(Vertex) * 6);

		m_pIntermediateColor = std::make_unique<GraphicsTexture>(pGraphicsDevice, "Cloud Intermediate Color");
	}

	m_pVerticalDensityTexture = std::make_unique<GraphicsTexture>(pGraphicsDevice, "Vertical Density Texture");
	m_pVerticalDensityTexture->Create(pContext, "Resources/textures/CloudVerticalDensity.png");

	pContext->Execute(true);
}

void Clouds::Render(RGGraph& graph, GraphicsTexture* pSceneTexture, GraphicsTexture* pDepthTexture, Camera* pCamera, const Light& sunLight)
{
	if (pSceneTexture->GetWidth() != m_pIntermediateColor->GetWidth() || pSceneTexture->GetHeight() != m_pIntermediateColor->GetHeight())
	{
		m_pIntermediateColor->Create(pSceneTexture->GetDesc());
	}

	RG_GRAPH_SCOPE("Clouds", graph);

	if (m_UpdateNoise)
	{
		m_UpdateNoise = false;

		RGPassBuilder worleyNoisePass = graph.AddPass("Compute Worley Noise");
		worleyNoisePass.Bind([=](CommandContext& context, const RGPassResource& passResources)
			{
				context.SetPipelineState(m_pWorleyNoisePS);
				context.SetComputeRootSignature(m_pWorleyNoiseRS.get());

				struct
				{
					Vector4 WorleyNoisePosition[MaxPoints];
					uint32_t PointsPerRow[16]{};
					uint32_t Resolution{0};
				} Constants;

				srand(0);
				for (int i = 0; i < MaxPoints; i++)
				{
					Constants.WorleyNoisePosition[i].x = Math::RandomRange(0.f, 1.f);
					Constants.WorleyNoisePosition[i].y = Math::RandomRange(0.f, 1.f);
					Constants.WorleyNoisePosition[i].z = Math::RandomRange(0.f, 1.f);
				}

				Constants.Resolution = Resolution;
				Constants.PointsPerRow[0] = 4;
				Constants.PointsPerRow[1] = 8;
				Constants.PointsPerRow[2] = 10;
				Constants.PointsPerRow[3] = 18;

				Constants.PointsPerRow[0+4] = 8;
				Constants.PointsPerRow[1+4] = 10;
				Constants.PointsPerRow[2+4] = 12;
				Constants.PointsPerRow[3+4] = 18;

				Constants.PointsPerRow[0+8] = 12;
				Constants.PointsPerRow[1+8] = 14;
				Constants.PointsPerRow[2+8] = 16;
				Constants.PointsPerRow[3+8] = 20;

				Constants.PointsPerRow[0 + 12] = 14;
				Constants.PointsPerRow[1 + 12] = 15;
				Constants.PointsPerRow[2 + 12] = 19;
				Constants.PointsPerRow[3 + 12] = 26;

				context.InsertResourceBarrier(m_pWorleyNoiseTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.FlushResourceBarriers();

				context.SetComputeDynamicConstantBufferView(0, &Constants, sizeof(Constants));
				context.BindResource(1, 0, m_pWorleyNoiseTexture->GetUAV());
		
				context.Dispatch(Resolution / 8, Resolution / 8, Resolution / 8);
			});
	}

	{
		RGPassBuilder cloudsPass = graph.AddPass("Clouds");
		cloudsPass.Bind([=](CommandContext& context, const RGPassResource& passResources)
			{
				context.InsertResourceBarrier(m_pWorleyNoiseTexture.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(pSceneTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(pDepthTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pIntermediateColor.get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
				context.FlushResourceBarriers();

				context.BeginRenderPass(RenderPassInfo(m_pIntermediateColor.get(), RenderPassAccess::DontCare_Store, nullptr, RenderPassAccess::NoAccess, false));

				context.SetPipelineState(m_pCloudsPS);
				context.SetGraphicsRootSignature(m_pCloudsRS.get());
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				float fov = pCamera->GetFoV();
				float aspect = (float)pSceneTexture->GetWidth() / (float)pSceneTexture->GetHeight();
				float halfFov = fov * 0.5f;
				float tanFov = tan(halfFov);
				Vector3 toRight = Vector3::Right * tanFov * aspect;  // right vector to frustum edge
				Vector3 toTop = Vector3::Up * tanFov;				// top vector to frustum edge

				sCloudParameters.FrustumCorners[0] = Vector4(-Vector3::Forward - toRight + toTop);
				sCloudParameters.FrustumCorners[1] = Vector4(-Vector3::Forward + toRight + toTop);
				sCloudParameters.FrustumCorners[2] = Vector4(-Vector3::Forward + toRight - toTop);
				sCloudParameters.FrustumCorners[3] = Vector4(-Vector3::Forward - toRight - toTop);

				sCloudParameters.ViewInverse = pCamera->GetViewInverse();
				sCloudParameters.NearPlane = pCamera->GetNear();
				sCloudParameters.FarPlane = pCamera->GetFar();
				sCloudParameters.MinExtents = Vector4(Vector3(m_CloudBounds.Center) - Vector3(m_CloudBounds.Extents));
				sCloudParameters.MaxExtents = Vector4(Vector3(m_CloudBounds.Center) + Vector3(m_CloudBounds.Extents));
				sCloudParameters.NoiseWeights = NoiseWeights;
				sCloudParameters.SunDirection = Vector4(sunLight.Direction);
				sCloudParameters.SunColor = sunLight.Colour;

				context.SetGraphicsDynamicConstantBufferView(0, &sCloudParameters, sizeof(sCloudParameters));

				context.BindResource(1, 0, pSceneTexture->GetSRV());
				context.BindResource(1, 1, pDepthTexture->GetSRV());
				context.BindResource(1, 2, m_pWorleyNoiseTexture->GetSRV());
				context.BindResource(1, 3, m_pVerticalDensityTexture->GetSRV());

				context.SetVertexBuffer(m_pQuadVertexBuffer.get());
				context.Draw(0, 6);

				context.EndRenderPass();
			});
	}

	{
		RGPassBuilder blitToMainPass = graph.AddPass("Blit to Main Render Target");
		blitToMainPass.Bind([=](CommandContext& context, const RGPassResource& passResources)
			{
				context.InsertResourceBarrier(pSceneTexture, D3D12_RESOURCE_STATE_COPY_DEST);
				context.InsertResourceBarrier(m_pIntermediateColor.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
				context.FlushResourceBarriers();

				context.CopyTexture(m_pIntermediateColor.get(), pSceneTexture);

				context.InsertResourceBarrier(pSceneTexture, D3D12_RESOURCE_STATE_RENDER_TARGET);
				context.FlushResourceBarriers();
			});
	}
}
