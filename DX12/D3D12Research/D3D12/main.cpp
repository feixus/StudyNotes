#include "stdafx.h"
#include "Graphics/Graphics.h"
#include "Core/Input.h"

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
	void Run()
	{
		m_DisplayWidth = gWindowWidth;
		m_DisplayHeight = gWindowHeight;

		MakeWindow();

		m_pGraphics = std::make_unique<Graphics>(m_DisplayWidth, m_DisplayHeight, gMsaaSampleCount);
		m_pGraphics->Initialize(m_Hwnd);

		GameTimer::Reset();

		MSG msg = {};
		while (msg.message != WM_QUIT)
		{
			if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			else
			{
				GameTimer::Tick();
				m_pGraphics->Update();
				Input::Instance().Update();
			}
		}

		m_pGraphics->Shutdown();
	}

private:
	void OnPause(const bool paused)
	{
		m_Pause = paused;
		if (m_Pause)
			GameTimer::Stop();
		else
			GameTimer::Start();
	}

	void MakeWindow()
	{
		WNDCLASS wc;

		wc.hInstance = GetModuleHandle(0);
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hIcon = 0;
		wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		wc.lpfnWndProc = WndProcStatic;
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpszClassName = TEXT("wndClass");
		wc.lpszMenuName = nullptr;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

		if (!RegisterClass(&wc))
		{
			return;
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

		m_Hwnd = CreateWindow(
			TEXT("wndClass"),
			TEXT("Hello World DX12"),
			windowStyle,
			x,
			y,
			windowWidth,
			windowHeight,
			nullptr,
			nullptr,
			GetModuleHandle(0),
			this
		);

		if (!m_Hwnd) return;

		ShowWindow(m_Hwnd, SW_SHOWDEFAULT);

		if (!UpdateWindow(m_Hwnd))
		{
			return;
		}

		Input::Instance().SetWindow(m_Hwnd);
	}


	LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_ACTIVATE:
		{
			OnPause(LOWORD(wParam) == WA_INACTIVE);
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
					OnPause(true);
					m_Minimized = true;
					m_Maximized = false;
				}
				else if (wParam == SIZE_MAXIMIZED)
				{
					OnPause(false);
					m_Minimized = false;
					m_Maximized = true;
					m_pGraphics->OnResize(m_DisplayWidth, m_DisplayHeight);
				}
				else if (wParam == SIZE_RESTORED)
				{
					if (m_Minimized)
					{
						OnPause(false);
						m_Minimized = false;
						m_pGraphics->OnResize(m_DisplayWidth, m_DisplayHeight);
					}
					else if (m_Maximized)
					{
						OnPause(false);
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
			OnPause(true);
			m_IsResizing = true;
			break;	
		}
		case WM_EXITSIZEMOVE:
		{
			OnPause(false);
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
			SetLastError(0);
			if (!SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis)))
			{
				if (GetLastError() != 0)
					return 0;
			}
		}
		else
		{
			pThis = reinterpret_cast<ViewWrapper*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
		}
		if (pThis)
		{
			LRESULT callback = pThis->WndProc(hWnd, message, wParam, lParam);
			return callback;
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

private:
	bool m_Pause = false;
	bool m_Minimized = false;
	bool m_Maximized = false;
	int m_DisplayWidth = 1240;
	int m_DisplayHeight = 720;
	bool m_IsResizing = false;
	HWND m_Hwnd = nullptr;
	std::unique_ptr<Graphics> m_pGraphics;
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

	Console::Startup();
	E_LOG(LogType::Info, "Startup hello dx12");

	ViewWrapper vw;
	vw.Run();

	return 0;
}
