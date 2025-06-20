#include "stdafx.h"
#include "ImGuiRenderer.h"
#include "CommandContext.h"
#include "D3DUtils.h"
#include "Graphics.h"
#include "Shader.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "DescriptorAllocator.h"
#include "Core/Input.h"
#include "GraphicsTexture.h"
#include "Profiler.h"

ImGuiRenderer::ImGuiRenderer(Graphics* pGraphics)
	: m_pGraphics(pGraphics)
{
	CreatePipeline();
	InitializeImGui();
}

ImGuiRenderer::~ImGuiRenderer()
{
	ImGui::DestroyContext();
}

void ImGuiRenderer::NewFrame()
{
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight());
	
	io.MouseDown[0] = Input::Instance().IsMouseDown(VK_LBUTTON);  // left button
	io.MouseDown[1] = Input::Instance().IsMouseDown(VK_RBUTTON);	 // right button

	Vector2 mousePos = Input::Instance().GetMousePosition();
	io.MousePos.x = mousePos.x;
	io.MousePos.y = mousePos.y;

	ImGui::NewFrame();
}

void ImGuiRenderer::InitializeImGui()
{
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontDefault();

	unsigned char* pPixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pPixels, &width, &height);

	m_pFontTexture = std::make_unique<GraphicsTexture>();
	m_pFontTexture->Create(m_pGraphics, TextureDesc::Create2D(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::ShaderResource));
	CommandContext* pContext = m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pFontTexture->SetData(pContext, pPixels);
	
	io.Fonts->SetTexID(m_pFontTexture->GetSRV().ptr);

	pContext->Execute(true);
}

void ImGuiRenderer::CreatePipeline()
{
	// shaders
	Shader vertexShader("Resources/Shaders/imgui.hlsl", Shader::Type::VertexShader, "VSMain");
	Shader pixelShader("Resources/Shaders/imgui.hlsl", Shader::Type::PixelShader, "PSMain");

	// root signature
	m_pRootSignature = std::make_unique<RootSignature>();
	m_pRootSignature->FinalizeFromShader("imgui", vertexShader, m_pGraphics->GetDevice());

	// input layout
	std::vector<D3D12_INPUT_ELEMENT_DESC> elementDesc = {
		D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// pipeline state
	m_pPipelineStateObject = std::make_unique<GraphicsPipelineState>();
	m_pPipelineStateObject->SetBlendMode(BlendMode::Alpha, false);
	m_pPipelineStateObject->SetDepthWrite(false);
	m_pPipelineStateObject->SetDepthEnable(false);
	m_pPipelineStateObject->SetCullMode(D3D12_CULL_MODE_NONE);
	m_pPipelineStateObject->SetInputLayout(elementDesc.data(), (uint32_t)elementDesc.size());
	m_pPipelineStateObject->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
	m_pPipelineStateObject->SetRootSignature(m_pRootSignature->GetRootSignature());
	m_pPipelineStateObject->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
	m_pPipelineStateObject->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
	m_pPipelineStateObject->Finalize("ImGui Pipeline", m_pGraphics->GetDevice());
}

void ImGuiRenderer::Render(CommandContext& context)
{
	ImGui::Render();
	ImDrawData* pDrawData = ImGui::GetDrawData();
	if (pDrawData->CmdListsCount == 0)
	{
		return;
	}

	context.SetGraphicsPipelineState(m_pPipelineStateObject.get());
	context.SetGraphicsRootSignature(m_pRootSignature.get());

	uint32_t width = m_pGraphics->GetWindowWidth();
	uint32_t height = m_pGraphics->GetWindowHeight();
	Matrix projectionMatrix = XMMatrixOrthographicOffCenterLH(0.0f, (float)width, (float)height, 0.0f, 0.0f, 1.0f);
	context.SetDynamicConstantBufferView(0, &projectionMatrix, sizeof(Matrix));

	context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context.SetViewport(FloatRect(0, 0, (float)width, (float)height), 0, 1);

	Profiler::Instance()->Begin("Render UI", &context);
	context.BeginRenderPass(RenderPassInfo(m_pGraphics->GetCurrentRenderTarget(), RenderPassAccess::Load_Store, m_pGraphics->GetDepthStencil(), RenderPassAccess::DontCare_DontCare));

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
				context.SetScissorRect(FloatRect(pCmd->ClipRect.x, pCmd->ClipRect.y, pCmd->ClipRect.z, pCmd->ClipRect.w));
				if (pCmd->TextureId > 0)
				{
					D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
					cpuHandle.ptr = pCmd->TextureId;
					context.SetDynamicDescriptor(1, 0, cpuHandle);
				}
				context.DrawIndexed(pCmd->ElemCount, indexOffset, 0);
			}
			indexOffset += pCmd->ElemCount;
		}
	}
	context.EndRenderPass();
	Profiler::Instance()->End(&context);
}
