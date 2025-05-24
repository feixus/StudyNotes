#include "stdafx.h"
#include "Profiler.h"
#include "CommandContext.h"
#include "CommandQueue.h"
#include "GraphicsBuffer.h"

#include <pix3.h>

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

	m_pRootBlock = std::make_unique<Block>();
	m_pCurrentBlock = m_pRootBlock.get();

	m_CurrentTimer = 0;
}

void Profiler::Begin(const char* pName, CommandContext* pContext)
{
	std::unique_ptr<Block> pNewBlock = std::make_unique<Block>(pName, m_pCurrentBlock);
	pNewBlock->CpuTimer.Begin();

	if (pContext)
	{
		int startIndex = pNewBlock->GpuTimer.GetTimerIndex() * 2;
		pContext->GetCommandList()->EndQuery(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, startIndex);
		m_pCurrentBlock->Children.push_back(std::move(pNewBlock));
		m_pCurrentBlock = m_pCurrentBlock->Children.back().get();
		
		wchar_t name[256];
		size_t written = 0;
		mbstowcs_s(&written, name, pName, 256);
		::PIXBeginEvent(pContext->GetCommandList(), 0, name);
	}
}

void Profiler::End(CommandContext* pContext)
{
	m_pCurrentBlock->CpuTimer.End();

	if (pContext)
	{
		int endIndex = m_pCurrentBlock->GpuTimer.GetTimerIndex() * 2 + 1;
		pContext->GetCommandList()->EndQuery(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, endIndex);
		m_pCurrentBlock = m_pCurrentBlock->pParent;

		::PIXEndEvent(pContext->GetCommandList());
	}
}

void Profiler::BeginReadback(int frameIndex)
{
	GraphicsCommandContext* pContext = (GraphicsCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	int startIndex = HEAP_SIZE * frameIndex * 2;
	int numQueries = (m_CurrentTimer - m_LastFrameTimer) * 2;
	pContext->GetCommandList()->ResolveQueryData(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, startIndex, numQueries, m_pReadBackBuffer->GetResource(), startIndex * sizeof(uint64_t));
	m_FenceValues[frameIndex] = pContext->Execute(false);

	assert(m_pCurrentReadBackData == nullptr);
	m_pGraphics->WaitForFence(m_FenceValues[frameIndex]);

   	m_pCurrentReadBackData = (uint64_t*)m_pReadBackBuffer->Map(0, 0, m_pReadBackBuffer->GetSize());

	assert(m_pCurrentBlock == m_pRootBlock.get());
	m_pCurrentBlock = m_pRootBlock->Children.front().get();
	int depth = 0;
	std::stringstream stream;
	bool run = true;
	while (run)
	{
		for (int i = 0; i < depth; i++)
		{
			stream << "\t";
		}
		stream << "[" << m_pCurrentBlock->Name << "] > GPU: " << m_pCurrentBlock->GpuTimer.GetTime(m_SecondsPerGpuTick) << " ms";

		stream << "\t CPU: " << m_pCurrentBlock->CpuTimer.GetTime(m_SecondsPerCpuTick) << " ms" << std::endl;

		while (m_pCurrentBlock->Children.size() == 0)
		{
			m_pCurrentBlock = m_pCurrentBlock->pParent;
			if (m_pCurrentBlock == nullptr)
			{
				run = false;
				break;
			}
			m_pCurrentBlock->Children.pop_front();
			--depth;
		}

		if (!run)
		{
			break;
		}

		m_pCurrentBlock = m_pCurrentBlock->Children.front().get();
		depth++;
	}

	std::cout << stream.str() << std::endl;
	//E_LOG(LogType::Info, stream.str());
	m_pCurrentBlock = m_pRootBlock.get();
}

void Profiler::EndReadBack(int frameIndex)
{
	m_pReadBackBuffer->UnMap();
	m_pCurrentReadBackData = nullptr;

	m_CurrentTimer = HEAP_SIZE* frameIndex;
	m_LastFrameTimer = m_CurrentTimer;
}

int32_t Profiler::GetNextTimerIndex()
{
	return m_CurrentTimer++;
}

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

float CpuTimer::GetTime(float ticksPerSecond) const
{
	return (float)(m_EndTime - m_StartTime) * ticksPerSecond * 1000.0f;
}

GpuTimer::GpuTimer()
{
	m_TimerIndex = Profiler::Instance()->GetNextTimerIndex();
}

void GpuTimer::Begin(const char* pName, CommandContext* pContext)
{
	Profiler::Instance()->Begin(pName, pContext);
}

void GpuTimer::End(CommandContext* pContext)
{
	Profiler::Instance()->End(pContext);
}

float GpuTimer::GetTime(float ticksPerSecond) const
{
	const uint64_t* pData = Profiler::Instance()->GetData();
	assert(pData);

	uint64_t start = pData[m_TimerIndex * 2];
	uint64_t end = pData[m_TimerIndex * 2 + 1];
	return (float)((end - start) * ticksPerSecond * 1000.0);
}
