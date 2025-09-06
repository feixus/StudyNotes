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
	ImGui::Columns(3);

	ImGui::PushID(ImGui::GetID(m_Name));
	ImGui::Text("Event Name");
	ImGui::NextColumn();
	ImGui::Text("CPU Time");
	ImGui::NextColumn();
	ImGui::Text("GPU Time");
	ImGui::NextColumn();

	for (auto& pChild : m_Children)
	{
		pChild->RenderNodeImGui(frameIndex);
	}

	ImGui::PopID();
	ImGui::Separator();
}

void ProfileNode::RenderNodeImGui(int frameIndex)
{
	if (frameIndex - m_LastProcessedFrame < 60)
	{
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

		ImGui::NextColumn();

		float time = m_CpuTimeHistory.GetAverage();
		ImGui::Text("%4.2f ms", time);
		ImGui::NextColumn();

		time = m_GpuTimeHistory.GetAverage();
		if (time > 0)
		{
			ImGui::Text("%4.2f ms", time);
		}
		else
		{
			ImGui::Text("N/A");
		}
		ImGui::NextColumn();

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

void Profiler::Initialize(Graphics* pGraphics)
{
	CD3DX12_QUERY_HEAP_DESC desc(HEAP_SIZE, D3D12_QUERY_HEAP_TYPE_TIMESTAMP);
	VERIFY_HR_EX(pGraphics->GetDevice()->CreateQueryHeap(&desc, IID_PPV_ARGS(m_pQueryHeap.GetAddressOf())), pGraphics->GetDevice());
	D3D::SetD3DObjectName(m_pQueryHeap.Get(), "Profiler Timestamp Query Heap");

	m_pReadBackBuffer = std::make_unique<Buffer>(pGraphics, "Profiling Readback Buffer");
	m_pReadBackBuffer->Create(BufferDesc::CreateReadback(sizeof(uint64_t) * Graphics::FRAME_COUNT * HEAP_SIZE));

	{
		// GPU frequency
		uint64_t timeStampFrequency;
		pGraphics->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->GetCommandQueue()->GetTimestampFrequency(&timeStampFrequency);
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

void Profiler::Resolve(Graphics* pGraphics, int frameIndex)
{
	//checkf(m_pCurrentBlock == m_pRootBlock.get(), "the current block isn't the root block then something must've gone wrong!");
	
	// start the resolve on the current frame
	int offset = MAX_GPU_TIME_QUERIES * QUERY_PAIR_NUM * m_CurrentReadbackFrame;
	CommandContext* pContext = pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	pContext->GetCommandList()->ResolveQueryData(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, m_CurrentTimer * QUERY_PAIR_NUM, m_pReadBackBuffer->GetResource(), offset * sizeof(uint64_t));
	m_FenceValues[m_CurrentReadbackFrame] = pContext->Execute(false);

	if (frameIndex >= Graphics::FRAME_COUNT)
	{
		// make sure the resolve from 2 frames ago is finished before we read
		uint32_t readFromIndex = (m_CurrentReadbackFrame + Graphics::FRAME_COUNT - 1) % Graphics::FRAME_COUNT;
		pGraphics->WaitForFence(m_FenceValues[readFromIndex]);

		const uint64_t* pReadbackData = (uint64_t*)m_pReadBackBuffer->Map(0, 0, m_pReadBackBuffer->GetSize());
		check(pReadbackData);
		m_pCurrentBlock->PopulateTimes(pReadbackData, frameIndex - 2);
		m_pReadBackBuffer->UnMap();
	}
	m_CurrentReadbackFrame = (m_CurrentReadbackFrame + 1) % Graphics::FRAME_COUNT;

	m_pPreviousBlock = nullptr;
	m_pCurrentBlock->StartTimer(nullptr);
	m_pCurrentBlock->EndTimer(nullptr);
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


