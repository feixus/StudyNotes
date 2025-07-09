#include "stdafx.h"
#include "Console.h"

static Console* consoleInstance = nullptr;

Console::Console()
{
}

Console::~Console()
{
    delete[] m_ConvertBuffer;
}

void Console::Startup()
{
    static Console instance;
    consoleInstance = &instance;

#ifdef _DEBUG
    consoleInstance->InitializeConsoleWindow();
#endif

    consoleInstance->m_ConvertBuffer = new char[consoleInstance->m_ConvertBufferSize];
}

bool Console::LogHRESULT(const char* source, HRESULT hr)
{
    if (FAILED(hr))
    {
        if (FACILITY_WINDOWS == HRESULT_FACILITY(hr))
        {
            hr = HRESULT_CODE(hr);
        }

        char* errorMsg;
        FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&errorMsg, 0, nullptr);

        LogFormat(LogType::Error, "Source: %s\n Message: %s", source, errorMsg);
        return true;
    }

    return false;
}

void Console::Log(const char* message, LogType type)
{
    if ((int)type < (int)consoleInstance->m_Verbosity)
    {
        return;
    }

    std::stringstream stream;
    switch(type)
    {
        case LogType::VeryVerbose:
            stream << "[VeryVerbose] ";
            break;
        case LogType::Verbose:
            stream << "[Verbose] ";
            break;
        case LogType::Info:
            stream << "[Info] ";
            break;
        case LogType::Warning:
            if (consoleInstance->m_ConsoleHandle)
            {
                SetConsoleTextAttribute(consoleInstance->m_ConsoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            }
            stream << "[Warning] ";
            break;
        case LogType::Error:
        case LogType::FatalError:
            if (consoleInstance->m_ConsoleHandle)
            {
                SetConsoleTextAttribute(consoleInstance->m_ConsoleHandle, FOREGROUND_RED | FOREGROUND_INTENSITY);
            }
            stream << "[Error] ";
            break;
        default:
            break;
    }

    stream << message;
    const std::string output = stream.str();
    std::cout << output << std::endl;
    OutputDebugStringA(output.c_str());
    OutputDebugStringA("\n");

    if (consoleInstance->m_ConsoleHandle)
    {
        SetConsoleTextAttribute(consoleInstance->m_ConsoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    }

    consoleInstance->m_History.push_back({ message, type });
    if (consoleInstance->m_History.size() > 50)
    {
        consoleInstance->m_History.pop_front();
    }

    if (type == LogType::Error)
    {
        __debugbreak();
    }
    else if (type == LogType::FatalError)
    {
        abort();
    }
}

void Console::LogFormat(LogType type, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    _vsnprintf_s(&consoleInstance->m_ConvertBuffer[0], consoleInstance->m_ConvertBufferSize, consoleInstance->m_ConvertBufferSize, format, args);
    va_end(args);
    Log(&consoleInstance->m_ConvertBuffer[0], type);
}

void Console::SetVerbosity(LogType type)
{
    consoleInstance->m_Verbosity = type;
}

const std::deque<Console::LogEntry>& Console::GetHistory()
{
    return consoleInstance->m_History;
}

void Console::InitializeConsoleWindow()
{
    if (AllocConsole())
    {
        // redirect the CRT standard input, output, and error handles to the console
        FILE* pCout;
        freopen_s(&pCout, "CONIN$", "r", stdin);
        freopen_s(&pCout, "CONOUT$", "w", stdout);
        freopen_s(&pCout, "CONOUT$", "w", stderr);

        std::wcout.clear();
        std::cout.clear();
        std::cerr.clear();
        std::wcerr.clear();
        std::wcin.clear();
        std::cin.clear();

        // set consoleHandle
        m_ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);

        // disable close-button
        HWND hwnd = GetConsoleWindow();
        if (hwnd != nullptr)
        {
            HMENU hMenu = GetSystemMenu(hwnd, FALSE);
            if (hMenu != nullptr)
            {
                DeleteMenu(hMenu, SC_CLOSE, MF_BYCOMMAND);
            }
        }
    }
}
