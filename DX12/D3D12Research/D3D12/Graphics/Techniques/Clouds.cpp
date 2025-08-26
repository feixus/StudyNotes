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

static const int Resolution = 256;
static const int MaxPoints = 256;

struct CloudParameters
{
	Vector4 FrustumCorners[4];
	Matrix ViewInverse;
	float NearPlane{0};
	float FarPlane{0};
	
	float CloudScale{0.02f};
	float CloudThreshold{0.4f};
	Vector3 CloudOffset;
	float CloudDensity{0.7f};

	Vector4 MinExtents;
	Vector4 MaxExtents;
};

static CloudParameters sCloudParameters;

Clouds::Clouds(Graphics* pGraphics)
{
	m_CloudBounds.Center = Vector3(0, 150, 0);
	m_CloudBounds.Extents = Vector3(50, 20, 50);

	Initialize(pGraphics);
}

void Clouds::Initialize(Graphics *pGraphics)
{
	pGraphics->GetImGui()->AddUpdateCallback(ImGuiCallbackDelegate::CreateLambda([this]() {
		ImGui::Begin("Parameters");
		ImGui::Text("Clouds");
		ImGui::SliderFloat3("Position", reinterpret_cast<float*>(&m_CloudBounds.Center), 0.f, 500.f);
		ImGui::SliderFloat3("Extents", reinterpret_cast<float*>(&m_CloudBounds.Extents), 0.f, 500.f);
		ImGui::SliderFloat("Scale", &sCloudParameters.CloudScale, 0, 0.02f);
		ImGui::SliderFloat("Cloud Threshold", &sCloudParameters.CloudThreshold, 0, 0.5f);
		ImGui::SliderFloat("Density", &sCloudParameters.CloudDensity, 0, 1.0f);
		if (ImGui::Button("Generate Noise"))
		{
			m_UpdateNoise = true;
		}
		ImGui::End();
	}));

	{
		Shader shader("WorleyNoise.hlsl", ShaderType::Compute, "WorleyNoiseCS");
		
		m_pWorleyNoiseRS = std::make_unique<RootSignature>();
		m_pWorleyNoiseRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pWorleyNoiseRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pWorleyNoiseRS->Finalize("Worley Noise RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		m_pWorleyNoisePS = std::make_unique<PipelineState>();
		m_pWorleyNoisePS->SetComputeShader(shader);
		m_pWorleyNoisePS->SetRootSignature(m_pWorleyNoiseRS->GetRootSignature());
		m_pWorleyNoisePS->Finalize("Worley Noise PS", pGraphics->GetDevice());

		m_pWorleyNoiseTexture = std::make_unique<GraphicsTexture>(pGraphics, "Worley Noise");
		m_pWorleyNoiseTexture->Create(TextureDesc::Create3D(Resolution, Resolution, Resolution, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource, TextureDimension::Texture3D));
		m_pWorleyNoiseTexture->SetName("Worley Noise Texture");
	}
	{
		Shader vertexShader("Clouds.hlsl", ShaderType::Vertex, "VSMain");
		Shader pixelShader("Clouds.hlsl", ShaderType::Pixel, "PSMain");
		m_pCloudsRS = std::make_unique<RootSignature>();
		m_pCloudsRS->FinalizeFromShader("Clouds RS", vertexShader, pGraphics->GetDevice());

		D3D12_INPUT_ELEMENT_DESC quadIL[] = {
			D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		m_pCloudsPS = std::make_unique<PipelineState>();
		m_pCloudsPS->SetVertexShader(vertexShader);
		m_pCloudsPS->SetPixelShader(pixelShader);
		m_pCloudsPS->SetInputLayout(quadIL, std::size(quadIL));
		m_pCloudsPS->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		m_pCloudsPS->SetDepthEnable(false);
		m_pCloudsPS->SetDepthWrite(false);
		m_pCloudsPS->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, pGraphics->GetMultiSampleCount());
		m_pCloudsPS->SetRootSignature(m_pCloudsRS->GetRootSignature());
		m_pCloudsPS->Finalize("Clouds PS", pGraphics->GetDevice());
	}

	CommandContext* pContext = pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
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

		m_pQuadVertexBuffer = std::make_unique<Buffer>(pGraphics, "Quad Vertex Buffer");
		m_pQuadVertexBuffer->Create(BufferDesc::CreateVertexBuffer(6, sizeof(Vertex)));
		m_pQuadVertexBuffer->SetData(pContext, vertices, sizeof(Vertex) * 6);

		m_pIntermediateColor = std::make_unique<GraphicsTexture>(pGraphics, "Cloud Intermediate Color");
	}
	pContext->Execute(true);
}

void Clouds::Render(RGGraph& graph, GraphicsTexture* pSceneTexture, GraphicsTexture* pDepthTexture, Camera* pCamera)
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
				context.SetPipelineState(m_pWorleyNoisePS.get());
				context.SetComputeRootSignature(m_pWorleyNoiseRS.get());

				struct
				{
					Vector4 WorleyNoisePosition[MaxPoints];
					uint32_t PointsPerRow[4]{};
					uint32_t Resolution{0};
				} Constants;

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
				Constants.PointsPerRow[3] = 12;

				context.InsertResourceBarrier(m_pWorleyNoiseTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.FlushResourceBarriers();

				context.SetComputeDynamicConstantBufferView(0, &Constants, sizeof(Constants));
				context.SetDynamicDescriptor(1, 0, m_pWorleyNoiseTexture->GetUAV());
		
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

				context.SetViewport(FloatRect(0, 0, (float)pSceneTexture->GetWidth(), (float)pSceneTexture->GetHeight()));
				context.SetScissorRect(FloatRect(0, 0, (float)pSceneTexture->GetWidth(), (float)pSceneTexture->GetHeight()));

				context.BeginRenderPass(RenderPassInfo(m_pIntermediateColor.get(), RenderPassAccess::DontCare_Store, nullptr, RenderPassAccess::NoAccess));

				context.SetPipelineState(m_pCloudsPS.get());
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

				context.SetDynamicConstantBufferView(0, &sCloudParameters, sizeof(sCloudParameters));

				context.SetDynamicDescriptor(1, 0, pSceneTexture->GetSRV());
				context.SetDynamicDescriptor(1, 1, pDepthTexture->GetSRV());
				context.SetDynamicDescriptor(1, 2, m_pWorleyNoiseTexture->GetSRV());

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
