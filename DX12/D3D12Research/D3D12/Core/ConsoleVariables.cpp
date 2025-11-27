#include "stdafx.h"
#include "ConsoleVariables.h"
#include "Core/Input.h"

CVarManager gConsoleManager;

void ImGuiConsole::Update(const ImVec2& position, const ImVec2& size)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::SetNextWindowPos(position, 0, ImVec2(0, 1));
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowCollapsed(!m_ShowConsole);

    bool show = ImGui::Begin("Output Log", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    if (show)
    {
        for (const Console::LogEntry& entry : Console::GetHistory())
        {
            switch (entry.Type)
            {
            case LogType::VeryVerbose:
            case LogType::Verbose:
            case LogType::Info:
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
                ImGui::TextWrapped("[Info] %s", entry.Message.c_str());
                break;
            case LogType::Warning:
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));
                ImGui::TextWrapped("[Warning] %s", entry.Message.c_str());
                break;
            case LogType::Error:
            case LogType::FatalError:
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
                ImGui::TextWrapped("[Error] %s", entry.Message.c_str());
                break;
            }
            ImGui::PopStyleColor();
        }

        int inputFlags = ImGuiInputTextFlags_EnterReturnsTrue | 
            ImGuiInputTextFlags_CallbackHistory |
            ImGuiInputTextFlags_CallbackCompletion | 
            ImGuiInputTextFlags_CallbackCharFilter |
            ImGuiInputTextFlags_CallbackEdit |
            ImGuiInputTextFlags_CallbackAlways;

        auto inputCallback = [](ImGuiInputTextCallbackData* pData) {
            ImGuiConsole* pConsole = (ImGuiConsole*)pData->UserData;
            return pConsole->InputCallback(pData);
        };

        //ImGui::PushItemWidth(size.x);
        ImGui::SetKeyboardFocusHere();
		if (ImGui::InputText("##some thing", m_Input, 1024, inputFlags, inputCallback, this))
		{
			if (*m_Input != '\0')
			{
				if (strcmp(m_Input, "help") == 0)
				{
					E_LOG(Info, "Available Commands:");
					gConsoleManager.ForEachCvar([](IConsoleObject* pObject) {
						E_LOG(Info, "\t- %s", pObject->GetName());
					});
				}
				else
				{
					char buffer[1024];
					strncpy_s(buffer, &m_Input[0], 1024);
					char* pArgs = strchr(buffer, ' ');
					if (pArgs)
					{
						*pArgs = '\0';
						pArgs++;
					}
					bool success = gConsoleManager.ExecuteCommand(buffer, pArgs ? pArgs : "");
					m_Suggestions.clear();
					m_History.push_back(m_Input);
					m_HistoryPos = -1;
					if (!success)
					{
						E_LOG(Warning, "Unknow command '%s'", m_Input);
					}
				}
				m_Input[0] = '\0';
			}
		}

        if (m_FocusConsole)
        {
            m_FocusConsole = false;
            ImGui::SetKeyboardFocusHere();
        }

        //ImGui::PopItemWidth();

        if (!m_Suggestions.empty())
        {
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y - ImGui::GetFontSize() * 2), 0, ImVec2(0, 1));
			if (ImGui::Begin("Suggestions", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoFocusOnAppearing))
			{
				for (uint32_t i = 0; i < m_Suggestions.size(); i++)
				{
					if (ImGui::Selectable(m_Suggestions[i], i == (uint32_t)m_SuggestionPos))
					{
						m_SuggestionPos = i;
						strncpy_s(m_Input, std::size(m_Input), m_Suggestions[i], strlen(m_Suggestions[i]));
						m_Suggestions.clear();
						m_AutoCompleted = true;
						ImGui::SetKeyboardFocusHere();
						break;
					}
				}
				ImGui::End();
			}
        }

		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
			ImGui::SetScrollHereY(1.0f);
    }
    
    ImGui::End();
    ImGui::PopStyleVar();

    if (Input::Instance().IsKeyPressed(VK_OEM_3))
    {
        m_ShowConsole = !m_ShowConsole;
        m_FocusConsole = m_ShowConsole;
    }
}

bool CVarManager::ExecuteCommand(const char* pCommand, const char* pArguments)
{
	const char* argList[16];
	char buffer[1024];
	int numArgs = CharConv::SplitString(pArguments, buffer, &argList[0]);
	auto it = m_CvarMap.find(pCommand);
	if (it != m_CvarMap.end())
	{
		return it->second->Execute(argList, numArgs);
	}
	return false;
}
