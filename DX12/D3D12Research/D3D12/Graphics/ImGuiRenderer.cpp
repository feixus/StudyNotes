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

ImGuiRenderer::ImGuiRenderer(Graphics* pGraphics)
	: m_pGraphics(pGraphics)
{
	InitializeImGui();
	CreatePipeline();
}

ImGuiRenderer::~ImGuiRenderer()
{
	ImGui::DestroyContext();
}

void ImGuiRenderer::NewFrame()
{
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight());
	
	io.MouseDown[0] = Input::Instance().IsMouseDown(0);
	io.MouseDown[1] = Input::Instance().IsMouseDown(1);
	io.MouseDown[2] = Input::Instance().IsMouseDown(2);

	Vector2 mousePos = Input::Instance().GetMousePosition();
	io.MousePos.x = mousePos.x;
	io.MousePos.y = mousePos.y;
	
	ImGui::NewFrame();
}

void ImGuiRenderer::InitializeImGui()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontDefault();

	ImGui::StyleColorsDark();

	unsigned char* pPixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pPixels, &width, &height);

	m_pFontTexture = std::make_unique<GraphicsTexture2D>();
	m_pFontTexture->Create(m_pGraphics, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, TextureUsage::ShaderResource, 1);
	CommandContext* pContext = m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pFontTexture->SetData(pContext, pPixels);
	io.Fonts->SetTexID(m_pFontTexture->GetSRV().ptr);

	pContext->Execute(true);
}

void ImGuiRenderer::CreatePipeline()
{
	// shaders
	Shader vertexShader("Resources/imgui.hlsl", Shader::Type::VertexShader, "VSMain");
	Shader pixelShader("Resources/imgui.hlsl", Shader::Type::PixelShader, "PSMain");

	// root signature
	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	m_pRootSignature = std::make_unique<RootSignature>();
	m_pRootSignature->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_pRootSignature->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_PIXEL);

	D3D12_SAMPLER_DESC samplerDesc = {};
	samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	m_pRootSignature->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

	m_pRootSignature->Finalize("ImGui", m_pGraphics->GetDevice(), rootSignatureFlags);

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

void ImGuiRenderer::Render(GraphicsCommandContext& context)
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
	context.SetScissorRect(FloatRect(0, 0, (float)width, (float)height));

	context.SetRenderTarget(m_pGraphics->GetCurrentRenderTarget()->GetRTV(), m_pGraphics->GetDepthStencil()->GetDSV());

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
}
