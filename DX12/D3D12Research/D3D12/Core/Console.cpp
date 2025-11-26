#include "stdafx.h"
#include "Console.h"
#include "CommandLine.h"

namespace Win32Console
{
    static HANDLE Open()
    {
        HANDLE handle = NULL;
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

            handle = GetStdHandle(STD_OUTPUT_HANDLE);

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
        return handle;
    }

    static void Close(HANDLE handle)
    {
        if (handle)
        {
            CloseHandle(handle);
        }
    }
};

static HANDLE sConsoleHandle = nullptr;
std::mutex sLogMutex;
static std::queue<Console::LogEntry> sMessageQueue;
static LogType sVerbosity;
static std::deque<Console::LogEntry> sHistory;

void Console::Initialize()
{
#if WITH_CONSOLE
    if (!CommandLine::GetBool("noconsole"))
    {
        sConsoleHandle = Win32Console::Open();
    }

    E_LOG(Info, "Startup Console");
#endif
}

void Console::Shutdown()
{
    Win32Console::Close(sConsoleHandle);
}

void Console::Log(const char* message, LogType type)
{
    if ((int)type < (int)sVerbosity)
    {
        return;
    }

    const char* pVerbosityMessage = "";
    switch(type)
    {
        case LogType::Info:
            if (sConsoleHandle)
            {
                SetConsoleTextAttribute(sConsoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            }
            pVerbosityMessage = "[Info] ";
            break;
        case LogType::Warning:
            if (sConsoleHandle)
            {
                SetConsoleTextAttribute(sConsoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            }
            pVerbosityMessage = "[Warning] ";
            break;
        case LogType::Error:
        case LogType::FatalError:
            if (sConsoleHandle)
            {
                SetConsoleTextAttribute(sConsoleHandle, FOREGROUND_RED | FOREGROUND_INTENSITY);
            }
            pVerbosityMessage = "[Error] ";
            break;
        default:
            break;
    }

    char messageBuffer[4096];
    stbsp_sprintf(messageBuffer, "%s %s\n", pVerbosityMessage, message);
    printf("%s %s\n", pVerbosityMessage, message);
	// send msg to the vs debugger(Output)
    OutputDebugStringA(messageBuffer);

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

        sHistory.push_back(LogEntry(message, type));
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
    static char sConvertBuffer[8196];
    va_list args;
    va_start(args, format);
    vsnprintf_s(sConvertBuffer, 8196, format, args);
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
