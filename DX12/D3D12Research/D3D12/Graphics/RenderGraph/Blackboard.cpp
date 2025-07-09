#include "stdafx.h"
#include "Blackboard.h"

RGBlackboard::RGBlackboard()
{
}

RGBlackboard::~RGBlackboard()
{
}

RGBlackboard& RGBlackboard::Branch()
{
	m_Children.emplace_back(std::make_unique<RGBlackboard>());
	RGBlackboard& b = *m_Children.back();
	b.m_Parent = this;
	return b;
}

void* RGBlackboard::GetData(const char* name)
{
	StringHash nameHash(name);
	auto it = m_DataMap.find(nameHash);
	if (it != m_DataMap.end())
	{
		return it->second;
	}

	if (m_Parent)
	{
		return m_Parent->GetData(name);
	}

	return nullptr;
}
