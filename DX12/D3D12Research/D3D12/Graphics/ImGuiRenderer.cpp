#include "stdafx.h"
#include "ImGuiRenderer.h"
#include "Core/Input.h"
#include "Profiler.h"
#include "RenderGraph/RenderGraph.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/OfflineDescriptorAllocator.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "External/ImGuizmo/ImGuizmo.h"

ImGuiRenderer::ImGuiRenderer(Graphics* pGraphics)
{
	CreatePipeline(pGraphics);
	InitializeImGui(pGraphics);
}

ImGuiRenderer::~ImGuiRenderer()
{
	ImGui::DestroyContext();
}

void ImGuiRenderer::NewFrame(uint32_t width, uint32_t height)
{
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)width, (float)height);
	
	io.MouseDown[0] = Input::Instance().IsMouseDown(VK_LBUTTON);  // left button
	io.MouseDown[1] = Input::Instance().IsMouseDown(VK_RBUTTON);	 // right button
	io.MouseWheel = Input::Instance().GetMouseWheelDelta();

	Vector2 mousePos = Input::Instance().GetMousePosition();
	io.MousePos.x = mousePos.x;
	io.MousePos.y = mousePos.y;

	ImGui::NewFrame();
	ImGuizmo::BeginFrame();
}

void ImGuiRenderer::InitializeImGui(Graphics* pGraphics)
{
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();

	ImFontConfig fontConfig;
	fontConfig.OversampleH = 2;
	fontConfig.OversampleV = 2;

	io.Fonts->AddFontFromFileTTF("Resources/Fonts/Roboto-Bold.ttf", 15.0f, &fontConfig);

	unsigned char* pPixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pPixels, &width, &height);

	m_pFontTexture = std::make_unique<GraphicsTexture>(pGraphics, "ImGui Font");
	m_pFontTexture->Create(TextureDesc::Create2D(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::ShaderResource));
	CommandContext* pContext = pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pFontTexture->SetData(pContext, pPixels);
	
	io.Fonts->TexID = m_pFontTexture.get();

	pContext->Execute(true);

	ImGui::GetStyle().FrameRounding = 4.0f;
	ImGui::GetStyle().GrabRounding = 4.0f;

	ImVec4* colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.42f, 0.47f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
	colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
	colors[ImGuiCol_Border] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.20f, 0.28f, 1.00f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.09f, 0.12f, 0.14f, 1.00f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.12f, 0.14f, 0.65f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.39f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.18f, 0.22f, 0.25f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.09f, 0.21f, 0.31f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.37f, 0.61f, 1.00f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.29f, 0.55f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
	colors[ImGuiCol_Tab] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
	colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
	colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
	colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
	colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

void ImGuiRenderer::CreatePipeline(Graphics* pGraphics)
{
	// shaders
	Shader* pVertexShader = pGraphics->GetShaderManager()->GetShader("imgui.hlsl", ShaderType::Vertex, "VSMain");
	Shader* pPixelShader = pGraphics->GetShaderManager()->GetShader("imgui.hlsl", ShaderType::Pixel, "PSMain");

	// root signature
	m_pRootSignature = std::make_unique<RootSignature>(pGraphics);
	m_pRootSignature->FinalizeFromShader("imgui", pVertexShader);

	// input layout
	CD3DX12_INPUT_ELEMENT_DESC elementDesc[] = {
		CD3DX12_INPUT_ELEMENT_DESC{ "POSITION", DXGI_FORMAT_R32G32_FLOAT },
		CD3DX12_INPUT_ELEMENT_DESC{ "TEXCOORD", DXGI_FORMAT_R32G32_FLOAT },
		CD3DX12_INPUT_ELEMENT_DESC{ "COLOR", DXGI_FORMAT_R8G8B8A8_UNORM }
	};

	// pipeline state
	PipelineStateInitializer psoDesc;
	psoDesc.SetBlendMode(BlendMode::Alpha, false);
	psoDesc.SetDepthWrite(false);
	psoDesc.SetDepthEnable(false);
	psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
	psoDesc.SetInputLayout(elementDesc, (uint32_t)std::size(elementDesc));
	psoDesc.SetRenderTargetFormat(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN, 1);
	psoDesc.SetRootSignature(m_pRootSignature->GetRootSignature());
	psoDesc.SetVertexShader(pVertexShader);
	psoDesc.SetPixelShader(pPixelShader);
	psoDesc.SetName("ImGui Pipeline");
	m_pPipelineStateObject = pGraphics->CreatePipeline(psoDesc);
}

void ImGuiRenderer::Render(RGGraph& graph, const SceneData& sceneData, GraphicsTexture* pRenderTarget)
{
	ImGui::Render();
	ImDrawData* pDrawData = ImGui::GetDrawData();
	if (pDrawData->CmdListsCount == 0)
	{
		return;
	}

	RGPassBuilder renderUI = graph.AddPass("RenderUI");
	renderUI.Bind([=](CommandContext& context, const RGPassResource& resources)
		{
			context.InsertResourceBarrier(pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
				
			context.SetPipelineState(m_pPipelineStateObject);
			context.SetGraphicsRootSignature(m_pRootSignature.get());

			Matrix projectionMatrix = Math::CreateOrthographicOffCenterMatrix(0, pDrawData->DisplayPos.x + pDrawData->DisplaySize.x, pDrawData->DisplayPos.y + pDrawData->DisplaySize.y, 0, 0.0f, 1.0f);

			context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context.SetViewport(FloatRect(0, 0, pDrawData->DisplayPos.x + pDrawData->DisplaySize.x, pDrawData->DisplayPos.y + pDrawData->DisplaySize.y), 0, 1);

			context.BeginRenderPass(RenderPassInfo(pRenderTarget, RenderPassAccess::Load_Store, nullptr, RenderPassAccess::NoAccess, false));

			struct Data
			{
				Matrix ProjectionMatrix;
				int TextureIndex;
			} drawData;

			for (int n = 0; n < pDrawData->CmdListsCount; n++)
			{
				const ImDrawList* pCmdList = pDrawData->CmdLists[n];

				context.SetDynamicVertexBuffer(0, pCmdList->VtxBuffer.Size, sizeof(ImDrawVert), pCmdList->VtxBuffer.Data);
				context.SetDynamicIndexBuffer(pCmdList->IdxBuffer.Size, pCmdList->IdxBuffer.Data, true);

				int indexOffset = 0;
				for (int i = 0; i < pCmdList->CmdBuffer.Size; i++)
				{
					const ImDrawCmd* pCmd = &pCmdList->CmdBuffer[i];
					if (pCmd->UserCallback)
					{
						pCmd->UserCallback(pCmdList, pCmd);
					}
					else
					{
						drawData.ProjectionMatrix = projectionMatrix;
						context.SetScissorRect(FloatRect(pCmd->ClipRect.x, pCmd->ClipRect.y, pCmd->ClipRect.z, pCmd->ClipRect.w));
						ImTextureID textureData = pCmd->TextureId;
						if (textureData)
						{
							context.InsertResourceBarrier(textureData.pTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
							drawData.TextureIndex = textureData.pTexture->GetGraphics()->RegisterBindlessResource(textureData.pTexture);
						}
						context.SetGraphicsDynamicConstantBufferView(0, &drawData, sizeof(Data));
						context.BindResourceTable(1, sceneData.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Graphics);
						context.DrawIndexed(pCmd->ElemCount, indexOffset, 0);
					}
					indexOffset += pCmd->ElemCount;
				}
			}
			context.EndRenderPass();
		});
}

void ImGuiRenderer::Update()
{
	m_UpdateCallback.Broadcast();
}

DelegateHandle ImGuiRenderer::AddUpdateCallback(ImGuiCallbackDelegate&& callback)
{
	return m_UpdateCallback.Add(std::move(callback));
}

void ImGuiRenderer::RemoveUpdateCallback(DelegateHandle handle)
{
	m_UpdateCallback.Remove(handle);
}
