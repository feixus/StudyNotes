#include "stdafx.h"
#include "Core/Input.h"
#include "Graphics/Core/Graphics.h"
#include "Core/CommandLine.h"
#include "Core/TaskQueue.h"

// maps memory functions to debug versions, to help track memory allocations and find memory leaks
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include <filesystem>
#include <shlobj.h>

const int gWindowWidth = 1240;
const int gWindowHeight = 720;
const int gMsaaSampleCount = 1;

class ViewWrapper
{
public:
	void Run(HINSTANCE hInstance)
	{
		m_DisplayWidth = gWindowWidth;
		m_DisplayHeight = gWindowHeight;

		TaskQueue::Initialize(std::thread::hardware_concurrency());

		HWND window = MakeWindow(hInstance);
		Input::Instance().SetWindow(window);

		m_pGraphics = new Graphics(m_DisplayWidth, m_DisplayHeight, gMsaaSampleCount);
		m_pGraphics->Initialize(window);

		Time::Reset();

		MSG msg = {};
		bool quit = false;
		while (!quit)
		{
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);

				if (msg.message == WM_QUIT)
				{
					quit = true;
					break;
				}
			}

			Time::Tick();
			m_pGraphics->Update();
			Input::Instance().Update();
		}

		m_pGraphics->Shutdown();
		delete m_pGraphics;

		TaskQueue::Shutdown();
	}

private:
	HWND MakeWindow(HINSTANCE hInstance)
	{
		WNDCLASSEX wc{};

		wc.cbSize = sizeof(WNDCLASSEX);
		wc.hInstance = hInstance;
		wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		wc.lpfnWndProc = WndProcStatic;
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpszClassName = TEXT("wndClass");
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

		if (!RegisterClassEx(&wc))
		{
			return nullptr;
		}

		int displayWidth = GetSystemMetrics(SM_CXSCREEN);
		int displayHeight = GetSystemMetrics(SM_CYSCREEN);

		DWORD windowStyle = WS_OVERLAPPEDWINDOW;

		RECT windowRec = { 0, 0, (LONG)m_DisplayWidth, (LONG)m_DisplayHeight };
		AdjustWindowRect(&windowRec, windowStyle, false);

		uint32_t windowWidth = windowRec.right - windowRec.left;
		uint32_t windowHeight = windowRec.bottom - windowRec.top;

		int x = (displayWidth - m_DisplayWidth) / 2;
		int y = (displayHeight - m_DisplayHeight) / 2;

		HWND window = CreateWindowA(
			TEXT("wndClass"),
			TEXT("Hello World DX12"),
			windowStyle,
			x,
			y,
			windowWidth,
			windowHeight,
			nullptr,
			nullptr,
			hInstance,
			this
		);

		if (!window)
		{
			return nullptr;
		}

		ShowWindow(window, SW_SHOWDEFAULT);

		return window;
	}

	LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_ACTIVATE:
		{
			(LOWORD(wParam) == WA_INACTIVE) ? Time::Stop() : Time::Start();
			break;
		}
		// resize the window
		case WM_SIZE:
		{
			m_DisplayWidth = LOWORD(lParam);
			m_DisplayHeight = HIWORD(lParam);
			if (m_pGraphics)
			{
				if (wParam == SIZE_MINIMIZED)
				{
					Time::Stop();
					m_Minimized = true;
					m_Maximized = false;
				}
				else if (wParam == SIZE_MAXIMIZED)
				{
					Time::Start();
					m_Minimized = false;
					m_Maximized = true;
					m_pGraphics->OnResize(m_DisplayWidth, m_DisplayHeight);
				}
				else if (wParam == SIZE_RESTORED)
				{
					if (m_Minimized)
					{
						Time::Start();
						m_Minimized = false;
						m_pGraphics->OnResize(m_DisplayWidth, m_DisplayHeight);
					}
					else if (m_Maximized)
					{
						Time::Start();
						m_Maximized = false;
						m_pGraphics->OnResize(m_DisplayWidth, m_DisplayHeight);
					}
					else if (!m_IsResizing && m_DisplayWidth > 0 && m_DisplayHeight > 0) // api call such as SetWindowPos or mSwapchain->SetFullscreenState
					{
						m_pGraphics->OnResize(m_DisplayWidth, m_DisplayHeight);
					}
				}	
			}
			break;
		}
		case WM_MOUSEWHEEL:
		{
			float mouseWheel = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
			Input::Instance().UpdateMouseWheel(mouseWheel);
			return 0;
		}
		case WM_KEYUP:
		{
			Input::Instance().UpdateKey((uint32_t)wParam, false);
			break;	
		}
		case WM_KEYDOWN:
		{
			Input::Instance().UpdateKey((uint32_t)wParam, true);
			break;	
		}
		case WM_LBUTTONDOWN:
		{
			Input::Instance().UpdateMouseKey(VK_LBUTTON, true);
			break;	
		}
		case WM_LBUTTONUP:
		{
			Input::Instance().UpdateMouseKey(VK_LBUTTON, false);
			break;	
		}
		case WM_MBUTTONDOWN:
		{
			Input::Instance().UpdateMouseKey(VK_MBUTTON, true);
			break;	
		}
		case WM_MBUTTONUP:
		{
			Input::Instance().UpdateMouseKey(VK_MBUTTON, false);
			break;	
		}
		case WM_RBUTTONDOWN:
		{
			Input::Instance().UpdateMouseKey(VK_RBUTTON, true);
			break;	
		}
		case WM_RBUTTONUP:
		{
			Input::Instance().UpdateMouseKey(VK_RBUTTON, false);
			break;	
		}
		case WM_ENTERSIZEMOVE: // resizing or moving window with the mouse
		{
			Time::Stop();
			m_IsResizing = true;
			break;	
		}
		case WM_EXITSIZEMOVE:
		{
			Time::Start();
			m_IsResizing = false;
			m_pGraphics->OnResize(m_DisplayWidth, m_DisplayHeight);
			break;
		}
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		}

		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	static LRESULT CALLBACK WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		ViewWrapper* pThis = nullptr;

		if (message == WM_NCCREATE)
		{
			pThis = static_cast<ViewWrapper*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
			SetWindowLongPtrA(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
		}
		else
		{
			pThis = reinterpret_cast<ViewWrapper*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
			return pThis->WndProc(hWnd, message, wParam, lParam);
		}
		
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

private:
	bool m_Minimized = false;
	bool m_Maximized = false;
	int m_DisplayWidth = 1240;
	int m_DisplayHeight = 720;
	bool m_IsResizing = false;
	Graphics* m_pGraphics{nullptr};
};

static std::wstring GetLatestWinPixGpuCapturerPath()
{
	LPWSTR programFilesPath = nullptr;
	SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, NULL, &programFilesPath);

	std::filesystem::path pixInstallationPath = programFilesPath;
	pixInstallationPath /= "Microsoft PIX";

	std::wstring newestVersionFound;

	for (auto const& directory_entry : std::filesystem::directory_iterator(pixInstallationPath))
	{
		if (directory_entry.is_directory())
		{
			if (newestVersionFound.empty() || newestVersionFound < directory_entry.path().filename().c_str())
			{
				newestVersionFound = directory_entry.path().filename().c_str();
			}
		}
	}

	if (newestVersionFound.empty())
	{
		// TODO: Error, no PIX installation found
	}

	return pixInstallationPath / newestVersionFound / L"WinPixGpuCapturer.dll";
}

//int main()
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	Thread::SetMainThread();

	CommandLine::Parse(lpCmdLine);
	
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	// Check to see if a copy of WinPixGpuCapturer.dll has already been injected into the application.
	// This may happen if the application is launched through the PIX UI. 
	/*if (GetModuleHandle("WinPixGpuCapturer.dll") == 0)
	{
		std::wstring aa = GetLatestWinPixGpuCapturerPath();
		char name[256];
		ToMultibyte(aa.c_str(), name, 256);
		LoadLibrary(name);
	}*/

	Console::Initialize();
	E_LOG(Info, "Startup hello dx12");

	ViewWrapper vw;
	vw.Run(hInstance);

	return 0;
}
