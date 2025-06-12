#include "stdafx.h"
#include "Profiler.h"
#include "CommandContext.h"
#include "CommandQueue.h"
#include "GraphicsBuffer.h"

#define USE_PIX
#ifdef USE_PIX
#include <pix3.h>
#endif

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
	m_EndTime = end.QuadPart;
}

float CpuTimer::GetTime() const
{
	return (float)(m_EndTime - m_StartTime) * Profiler::Instance()->GetSecondsPerCpuTick() * 1000.0f;
}

GpuTimer::GpuTimer()
{
}

void GpuTimer::Begin(CommandContext* pContext)
{
	//if (m_TimerIndex == -1)
	{
		m_TimerIndex = Profiler::Instance()->GetNextTimerIndex();
	}
	Profiler::Instance()->StartGpuTimer(pContext, m_TimerIndex);
}

void GpuTimer::End(CommandContext* pContext)
{
	Profiler::Instance()->StopGpuTimer(pContext, m_TimerIndex);
}

float GpuTimer::GetTime() const
{
	if (m_TimerIndex >= 0)
	{
		return Profiler::Instance()->GetGpuTime(m_TimerIndex);
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

void ProfileNode::PopulateTimes(int frameIndex)
{
	if (m_Processed == false)
	{
		m_Processed = true;
		m_LastProcessedFrame = frameIndex;
		float cpuTime = m_CpuTimer.GetTime();
		float gpuTime = m_GpuTimer.GetTime();
		m_CpuTimeHistory.AddTime(cpuTime);
		m_GpuTimeHistory.AddTime(gpuTime);

		for (auto& child : m_Children)
		{
			child->PopulateTimes(frameIndex);
		}
	}
}

void ProfileNode::LogTimes(int frameIndex, void(*pLogFunction)(const char* pText), int depth, bool isRoot)
{
	if (frameIndex - m_LastProcessedFrame < 60)
	{
		if (isRoot)
		{
			E_LOG(LogType::Info, "Timings for frame: %d", frameIndex);
			return;
		}

		std::stringstream stream;
		for (int i = 0; i < depth; i++)
		{
			stream << "\t";
		}
		stream << "[" << m_Name << "] GPU: " << m_GpuTimeHistory.GetAverage() << " ms. CPU: " << m_CpuTimeHistory.GetAverage() << " ms" << std::endl;
		pLogFunction(stream.str().c_str());

		for (auto& child : m_Children)
		{
			child->LogTimes(frameIndex, pLogFunction, depth + 1, false);
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
			//expand = ImGui::TreeNode(m_Name);
			expand = ImGui::TreeNodeEx(m_Name, m_Children.size() > 2 ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
		}
		else
		{
			ImGui::Bullet();
			ImGui::Selectable(m_Name);
		}

		ImGui::NextColumn();

		float time = m_CpuTimeHistory.GetAverage();
		ImGui::Text("%8.5f ms", time);
		ImGui::NextColumn();

		time = m_GpuTimeHistory.GetAverage();
		if (time > 0)
		{
			ImGui::Text("%8.5f ms", time);
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

Profiler* Profiler::Instance()
{
	static Profiler profiler;
	return &profiler;
}

void Profiler::Initialize(Graphics* pGraphics)
{
	m_pGraphics = pGraphics;

	D3D12_QUERY_HEAP_DESC desc{};
	desc.Count = HEAP_SIZE * Graphics::FRAME_COUNT * 2;
	desc.NodeMask = 0;
	desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	HR(pGraphics->GetDevice()->CreateQueryHeap(&desc, IID_PPV_ARGS(m_pQueryHeap.GetAddressOf())));

	int bufferSize = HEAP_SIZE * sizeof(uint64_t) * 2 * Graphics::FRAME_COUNT;
	m_pReadBackBuffer = std::make_unique<ReadbackBuffer>();
	m_pReadBackBuffer->Create(pGraphics, bufferSize);

	uint64_t timeStampFrequency;
	pGraphics->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->GetCommandQueue()->GetTimestampFrequency(&timeStampFrequency);
	m_SecondsPerGpuTick = 1.0f / timeStampFrequency;

	LARGE_INTEGER cpuFrequency;
	QueryPerformanceFrequency(&cpuFrequency);
	m_SecondsPerCpuTick = 1.0f / cpuFrequency.QuadPart;

	m_pRootBlock = std::make_unique<ProfileNode>("", StringHash(""), nullptr);
	m_pCurrentBlock = m_pRootBlock.get();

	m_CurrentTimer = 0;
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

void Profiler::BeginReadback(int frameIndex)
{
	int backBufferIndex = frameIndex % Graphics::FRAME_COUNT;
	m_pPreviousBlock = nullptr;

	assert(m_pCurrentReadBackData == nullptr);
	m_pGraphics->WaitForFence(m_FenceValues[backBufferIndex]);

	m_pCurrentReadBackData = (uint64_t*)m_pReadBackBuffer->Map(0, 0, m_pReadBackBuffer->GetSize());
	m_pCurrentBlock->PopulateTimes(frameIndex);
}

void Profiler::EndReadBack(int frameIndex)
{
	int backBufferIndex = frameIndex % Graphics::FRAME_COUNT;

	m_pReadBackBuffer->UnMap();
	m_pCurrentReadBackData = nullptr;

	int offset = HEAP_SIZE * backBufferIndex * 2;
	int numQueries = (m_CurrentTimer - m_LastTimer) * 2;
	m_pCurrentBlock->StartTimer(nullptr);
	m_pCurrentBlock->EndTimer(nullptr);

	CommandContext* pContext = m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	pContext->GetCommandList()->ResolveQueryData(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, offset, numQueries, m_pReadBackBuffer->GetResource(), offset);
	m_FenceValues[backBufferIndex] = pContext->Execute(false);

	m_CurrentTimer = HEAP_SIZE * ((backBufferIndex + 1) % Graphics::FRAME_COUNT);
	m_LastTimer = m_CurrentTimer;
}

float Profiler::GetGpuTime(int timerIndex) const
{
	assert(m_pCurrentReadBackData != nullptr);
	uint64_t start = m_pCurrentReadBackData[timerIndex * 2];
	uint64_t end = m_pCurrentReadBackData[timerIndex * 2 + 1];
	return (float)(end - start) * m_SecondsPerGpuTick * 1000.0f;
}

void Profiler::StartGpuTimer(CommandContext* pContext, int timerIndex)
{
	pContext->GetCommandList()->EndQuery(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timerIndex * 2);
}

void Profiler::StopGpuTimer(CommandContext* pContext, int timerIndex)
{
	pContext->GetCommandList()->EndQuery(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timerIndex * 2 + 1);
}

int32_t Profiler::GetNextTimerIndex()
{
	return m_CurrentTimer++;
}


