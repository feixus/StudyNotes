#include "stdafx.h"
#include "Console.h"
#include "CommandLine.h"

static HANDLE sConsoleHandle = nullptr;

std::mutex sLogMutex;
static std::queue<Console::LogEntry> sMessageQueue;
static LogType sVerbosity;
static std::deque<Console::LogEntry> sHistory;

void InitializeConsoleWindow()
{
    if (AllocConsole())
    {
        // redirect the CRT standard input, output, and error handles to the console
        FILE* pCout;
        freopen_s(&pCout, "CONIN$", "r", stdin);
        freopen_s(&pCout, "CONOUT$", "w", stdout);
        freopen_s(&pCout, "CONOUT$", "w", stderr);

        // clear the error state for each of the C++ standard stream objects.
        // we need to do this, as attempts to access the standard streams before they refer to a valid target will cause the iostream objects to enter an error state. 
        //In the error state, they will refuse to perform any input or output operations.
        std::wcout.clear();
        std::cout.clear();
        std::cerr.clear();
        std::wcerr.clear();
        std::wcin.clear();
        std::cin.clear();

        // set consoleHandle
        sConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);

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

void Console::Initialize()
{
#if WITH_CONSOLE
    if (!CommandLine::GetBool("noconsole"))
    {
        InitializeConsoleWindow();
    }
#endif
}

void Console::Log(const char* message, LogType type)
{
    if ((int)type < (int)sVerbosity)
    {
        return;
    }

    std::stringstream stream;
    switch(type)
    {
        case LogType::Info:
            if (sConsoleHandle)
            {
                SetConsoleTextAttribute(sConsoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            }
            stream << "[Info] ";
            break;
        case LogType::Warning:
            if (sConsoleHandle)
            {
                SetConsoleTextAttribute(sConsoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            }
            stream << "[Warning] ";
            break;
        case LogType::Error:
        case LogType::FatalError:
            if (sConsoleHandle)
            {
                SetConsoleTextAttribute(sConsoleHandle, FOREGROUND_RED | FOREGROUND_INTENSITY);
            }
            stream << "[Error] ";
            break;
        default:
            break;
    }

    stream << message << "\n";
    const std::string output = stream.str();
    printf(output.c_str());
    OutputDebugStringA(output.c_str());

    if (sConsoleHandle)
    {
        SetConsoleTextAttribute(sConsoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    }

    if (Thread::IsMainThread())
    {
        {
            std::scoped_lock lock(sLogMutex);
            while (!sMessageQueue.empty())
            {
                const LogEntry& entry = sMessageQueue.front();
                sHistory.push_back(entry);
                sMessageQueue.pop();
            }
        }

        sHistory.push_back({ message, type });
        if (sHistory.size() > 50)
        {
            sHistory.pop_front();
        }
    }
    else
    {
        std::scoped_lock lock(sLogMutex);
        sMessageQueue.push(LogEntry(message, type));
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
    static char sConvertBuffer[8196 * 2];
    va_list args;
    va_start(args, format);
    vsnprintf_s(sConvertBuffer, 8196 * 2, format, args);
    va_end(args);
    Log(sConvertBuffer, type);
}

void Console::SetVerbosity(LogType type)
{
    sVerbosity = type;
}

const std::deque<Console::LogEntry>& Console::GetHistory()
{
    return sHistory;
}
