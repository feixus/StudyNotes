#include "stdafx.h"
#include "Profiler.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/CommandQueue.h"
#include "Graphics/Core/GraphicsBuffer.h"

#include <pix3.h>

void CpuTimer::Begin()
{
	LARGE_INTEGER start;
	QueryPerformanceCounter(&start);
	m_StartTime = start.QuadPart;
}

void CpuTimer::End()
{
	LARGE_INTEGER end;
	QueryPerformanceCounter(&end);
	m_TotalTime = (float)(end.QuadPart - m_StartTime) * Profiler::Get()->GetSecondsPerCpuTick() * 1000.0f;
}

float CpuTimer::GetTime() const
{
	return m_TotalTime;
}

GpuTimer::GpuTimer()
{}

void GpuTimer::Begin(CommandContext* pContext)
{
	if (m_TimerIndex == -1)
	{
		m_TimerIndex = Profiler::Get()->GetNextTimerIndex();
	}
	Profiler::Get()->StartGpuTimer(pContext, m_TimerIndex);
}

void GpuTimer::End(CommandContext* pContext)
{
	Profiler::Get()->StopGpuTimer(pContext, m_TimerIndex);
}

float GpuTimer::GetTime(const uint64_t* pReadbackData) const
{
	if (m_TimerIndex >= 0)
	{
		return Profiler::Get()->GetGpuTime(pReadbackData, m_TimerIndex);
	}
	return 0.f;
}

void ProfileNode::StartTimer(CommandContext* pContext)
{
	m_CpuTimer.Begin();

	if (pContext)
	{
		m_GpuTimer.Begin(pContext);

#ifdef USE_PIX
		::PIXBeginEvent(pContext->GetCommandList(), 0, m_Name);
#endif
	}
}

void ProfileNode::EndTimer(CommandContext* pContext)
{
	m_CpuTimer.End();
	m_Processed = false;

	if (pContext)
	{
		m_GpuTimer.End(pContext);

#ifdef USE_PIX
		::PIXEndEvent(pContext->GetCommandList());
#endif
	}
}

void ProfileNode::PopulateTimes(const uint64_t* pReadbackData, int frameIndex)
{
	if (m_Processed == false)
	{
		m_Processed = true;
		m_LastProcessedFrame = frameIndex;
		float cpuTime = m_CpuTimer.GetTime();
		float gpuTime = m_GpuTimer.GetTime(pReadbackData);
		m_CpuTimeHistory.AddTime(cpuTime);
		m_GpuTimeHistory.AddTime(gpuTime);

		for (auto& child : m_Children)
		{
			child->PopulateTimes(pReadbackData, frameIndex);
		}
	}
}

void ProfileNode::RenderImGui(int frameIndex)
{
	ImGui::Spacing();

	if (ImGui::BeginTable("Profiling", 3, ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch, 3.0f);
		ImGui::TableSetupColumn("CPU (ms)", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("GPU (ms)", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableHeadersRow();

		for (auto& pChild : m_Children)
		{
			pChild->RenderNodeImGui(frameIndex);
		}
		ImGui::EndTable();
	}

	ImGui::Separator();
}

void ProfileNode::RenderNodeImGui(int frameIndex)
{
	if (frameIndex - m_LastProcessedFrame < 60)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::PushID(m_Hash);

		bool expand = false;
		if (m_Children.size() > 0)
		{
			expand = ImGui::TreeNodeEx(m_Name, m_Children.size() > 2 ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
		}
		else
		{
			ImGui::Bullet();
			ImGui::Selectable(m_Name);
		}

		ImGui::TableNextColumn();

		float time = m_CpuTimeHistory.GetAverage();
		ImGui::Text("%4.2f ms", time);
		ImGui::TableNextColumn();

		time = m_GpuTimeHistory.GetAverage();
		if (time > 0)
		{
			ImGui::Text("%4.2f ms", time);
		}
		else
		{
			ImGui::Text("N/A");
		}

		if (expand)
		{
			for (auto& pChild : m_Children)
			{
				pChild->RenderNodeImGui(frameIndex);
			}
			ImGui::TreePop();
		}

		ImGui::PopID();
	}
}

bool ProfileNode::HashChild(const char* pName)
{
	StringHash hash(pName);
	return m_Map.find(hash) != m_Map.end();
}

ProfileNode* ProfileNode::GetChild(const char* pName, int i)
{
	StringHash hash(pName);
	auto it = m_Map.find(hash);
	if (it != m_Map.end())
	{
		return it->second;
	}

	std::unique_ptr<ProfileNode> pNewNode = std::make_unique<ProfileNode>(pName, hash, this);
	ProfileNode* pNode = m_Children.insert(m_Children.begin() + i, std::move(pNewNode))._Ptr->get();
	m_Map[hash] = pNode;
	return pNode;
}

Profiler* Profiler::Get()
{
	static Profiler profiler;
	return &profiler;
}

void Profiler::Initialize(GraphicsDevice* pGraphicsDevice)
{
	D3D12_QUERY_HEAP_DESC desc{}; 
	desc.Count = HEAP_SIZE;
	desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	desc.NodeMask = 0;
	VERIFY_HR_EX(pGraphicsDevice->GetDevice()->CreateQueryHeap(&desc, IID_PPV_ARGS(m_pQueryHeap.GetAddressOf())), pGraphicsDevice->GetDevice());
	D3D::SetObjectName(m_pQueryHeap.Get(), "Profiler Timestamp Query Heap");

	m_pReadBackBuffer = std::make_unique<Buffer>(pGraphicsDevice, "Profiling Readback Buffer");
	m_pReadBackBuffer->Create(BufferDesc::CreateReadback(sizeof(uint64_t) * Graphics::FRAME_COUNT * HEAP_SIZE));
	m_pReadBackBuffer->Map();

	{
		// GPU frequency
		uint64_t timeStampFrequency;
		pGraphicsDevice->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->GetCommandQueue()->GetTimestampFrequency(&timeStampFrequency);
		m_SecondsPerGpuTick = 1.0f / timeStampFrequency;
	}

	{
		// CPU frequency
		LARGE_INTEGER cpuFrequency;
		QueryPerformanceFrequency(&cpuFrequency);
		m_SecondsPerCpuTick = 1.0f / cpuFrequency.QuadPart;
	}

	m_pRootBlock = std::make_unique<ProfileNode>("", StringHash(""), nullptr);
	m_pCurrentBlock = m_pRootBlock.get();

	ID3D12CommandQueue* pQueue = pGraphicsDevice->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->GetCommandQueue();
	OPTICK_GPU_INIT_D3D12(pGraphicsDevice->GetDevice(), &pQueue, 1);
}

void Profiler::Begin(const char* pName, CommandContext* pContext)
{
	if (m_pCurrentBlock->HashChild(pName))
	{
		m_pCurrentBlock = m_pCurrentBlock->GetChild(pName);
		m_pCurrentBlock->StartTimer(pContext);
	}
	else
	{
		int i = 0;
		if (m_pPreviousBlock)
		{
			for (; i < m_pCurrentBlock->GetChildCount(); i++)
			{
				if (m_pCurrentBlock->GetChild(i) == m_pPreviousBlock)
				{
					++i;
					break;
				}
			}
		}

		m_pCurrentBlock = m_pCurrentBlock->GetChild(pName, i);
		m_pCurrentBlock->StartTimer(pContext);
	}
}

void Profiler::End(CommandContext* pContext)
{
	m_pCurrentBlock->EndTimer(pContext);
	m_pPreviousBlock = m_pCurrentBlock;
	m_pCurrentBlock = m_pCurrentBlock->GetParent();
}

void Profiler::Resolve(SwapChain* pSwapChain, GraphicsDevice* pGraphicsDevice, int frameIndex)
{
	//checkf(m_pCurrentBlock == m_pRootBlock.get(), "the current block isn't the root block then something must've gone wrong!");
	
	// start the resolve on the current frame
	int offset = MAX_GPU_TIME_QUERIES * QUERY_PAIR_NUM * m_CurrentReadbackFrame;
	CommandContext* pContext = pGraphicsDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	pContext->GetCommandList()->ResolveQueryData(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, m_CurrentTimer * QUERY_PAIR_NUM, m_pReadBackBuffer->GetResource(), offset * sizeof(uint64_t));
	m_FenceValues[m_CurrentReadbackFrame] = pContext->Execute(false);

	if (frameIndex >= Graphics::FRAME_COUNT)
	{
		// make sure the resolve from 2 frames ago is finished before we read
		uint32_t readFromIndex = (m_CurrentReadbackFrame + Graphics::FRAME_COUNT - 1) % Graphics::FRAME_COUNT;
		pGraphicsDevice->WaitForFence(m_FenceValues[readFromIndex]);

		m_pCurrentBlock->PopulateTimes((const uint64_t*)m_pReadBackBuffer->GetMappedData(), frameIndex - 2);
	}
	m_CurrentReadbackFrame = (m_CurrentReadbackFrame + 1) % Graphics::FRAME_COUNT;

	m_pPreviousBlock = nullptr;
	m_pCurrentBlock->StartTimer(nullptr);
	m_pCurrentBlock->EndTimer(nullptr);

	OPTICK_GPU_FLIP(pSwapChain->GetSwapChain());
	OPTICK_CATEGORY("Present", Optick::Category::Wait);
}

float Profiler::GetGpuTime(const uint64_t* pReadbackData, int timerIndex) const
{
	check(timerIndex >= 0);
	check(pReadbackData);
	uint64_t start = pReadbackData[timerIndex * QUERY_PAIR_NUM];
	uint64_t end = pReadbackData[timerIndex * QUERY_PAIR_NUM + 1];
	return (float)(end - start) * m_SecondsPerGpuTick * 1000.0f;
}

void Profiler::StartGpuTimer(CommandContext* pContext, int timerIndex)
{
	pContext->GetCommandList()->EndQuery(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timerIndex * QUERY_PAIR_NUM);
}

void Profiler::StopGpuTimer(CommandContext* pContext, int timerIndex)
{
	pContext->GetCommandList()->EndQuery(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timerIndex * QUERY_PAIR_NUM + 1);
}

int32_t Profiler::GetNextTimerIndex()
{
	return m_CurrentTimer++;
}


