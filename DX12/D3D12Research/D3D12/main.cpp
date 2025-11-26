#include "stdafx.h"
#include "Core/Input.h"
#include "Graphics/Core/Graphics.h"
#include "Core/Console.h"
#include "Core/CommandLine.h"
#include "Core/TaskQueue.h"
#include <filesystem>
#include <shlobj.h>
#include "DemoApp.h"

#ifdef _DEBUG
// maps memory functions to debug versions, to help track memory allocations and find memory leaks
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif


#define BREAK_ON_ALLOC 0

const int gWindowWidth = 1240;
const int gWindowHeight = 720;
const int gMsaaSampleCount = 1;

class ViewWrapper
{
public:
	int Run(HINSTANCE hInstance)
	{
#ifdef _DEBUG
		_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
		
#if BREAK_ON_ALLOC > 0
		_CrtSetBreakAlloc(BREAK_ON_ALLOC);
#endif
#endif

		Thread::SetMainThread();
		CommandLine::Parse(GetCommandLineA());
		Console::Initialize();

		E_LOG(Info, "Startup hello dx12");

		// Check to see if a copy of WinPixGpuCapturer.dll has already been injected into the application.
		// This may happen if the application is launched through the PIX UI. 
		if (CommandLine::GetBool("pix") && GetModuleHandleA("WinPixGpuCapturer.dll") == 0)
		{
			std::string pixPath;
			if (D3D::GetLatestWinPixGpuCapturePath(pixPath))
			{
				if (LoadLibraryA(pixPath.c_str()))
				{
					E_LOG(Warning, "Dynamically loaded PIX ('%s')", pixPath.c_str());
				}
			}
		}

		// attach to RenderDoc
		/*HMODULE mod = LoadLibraryA("C:\\Program Files\\RenderDoc\\renderdoc.dll");
		if (!mod)
		{
			printf("RenderDoc DLL not found\n");
		}*/

		TaskQueue::Initialize(std::thread::hardware_concurrency());

		m_DisplayWidth = gWindowWidth;
		m_DisplayHeight = gWindowHeight;

		HWND window = MakeWindow(hInstance);
		Input::Instance().SetWindow(window);

		m_pGraphics = new DemoApp(window, IntVector2(m_DisplayWidth, m_DisplayHeight), gMsaaSampleCount);

		Time::Reset();

		MSG msg = {};
		bool quit = false;
		while (!quit)
		{
			OPTICK_FRAME("MainThread");
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessageA(&msg);

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

		delete m_pGraphics;

		DestroyWindow(hInstance, window);
		TaskQueue::Shutdown();
		Console::Shutdown();
		OPTICK_SHUTDOWN();

		return 0;
	}

private:
	HWND MakeWindow(HINSTANCE hInstance)
	{
		::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

		WNDCLASSEX wc{};

		wc.cbSize = sizeof(WNDCLASSEX);
		wc.hInstance = hInstance;
		wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		wc.lpfnWndProc = WndProcStatic;
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpszClassName = TEXT("wndClass");
		wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);

		check(RegisterClassExA(&wc));

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

		check(window);
		ShowWindow(window, SW_SHOWDEFAULT);
		UpdateWindow(window);
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
			
			ImGuiKey key = ImGui_ImplWin32_VirtualKeyToImGuiKey(wParam);
			if (key != ImGuiKey_None)
			{
				ImGui::GetIO().AddKeyEvent(key, false);
			}

			break;	
		}
		case WM_KEYDOWN:
		{
			Input::Instance().UpdateKey((uint32_t)wParam, true);

			ImGuiKey key = ImGui_ImplWin32_VirtualKeyToImGuiKey(wParam);
			if (key != ImGuiKey_None)
			{
				ImGui::GetIO().AddKeyEvent(key, true);
			}

			break;	
		}
		case WM_CHAR:
		{
			if (wParam < 256)
			{
				ImGui::GetIO().AddInputCharacter((uint32_t)wParam);
			}
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

	ImGuiKey ImGui_ImplWin32_VirtualKeyToImGuiKey(WPARAM wParam)
	{
		switch (wParam)
		{
		case VK_TAB: return ImGuiKey_Tab;
		case VK_LEFT: return ImGuiKey_LeftArrow;
		case VK_RIGHT: return ImGuiKey_RightArrow;
		case VK_UP: return ImGuiKey_UpArrow;
		case VK_DOWN: return ImGuiKey_DownArrow;
		case VK_PRIOR: return ImGuiKey_PageUp;
		case VK_NEXT: return ImGuiKey_PageDown;
		case VK_HOME: return ImGuiKey_Home;
		case VK_END: return ImGuiKey_End;
		case VK_INSERT: return ImGuiKey_Insert;
		case VK_DELETE: return ImGuiKey_Delete;
		case VK_BACK: return ImGuiKey_Backspace;
		case VK_SPACE: return ImGuiKey_Space;
		case VK_RETURN: return ImGuiKey_Enter;
		case VK_ESCAPE: return ImGuiKey_Escape;
		case VK_OEM_7: return ImGuiKey_Apostrophe;
		case VK_OEM_COMMA: return ImGuiKey_Comma;
		case VK_OEM_MINUS: return ImGuiKey_Minus;
		case VK_OEM_PERIOD: return ImGuiKey_Period;
		case VK_OEM_2: return ImGuiKey_Slash;
		case VK_OEM_1: return ImGuiKey_Semicolon;
		case VK_OEM_PLUS: return ImGuiKey_Equal;
		case VK_OEM_4: return ImGuiKey_LeftBracket;
		case VK_OEM_5: return ImGuiKey_Backslash;
		case VK_OEM_6: return ImGuiKey_RightBracket;
		case VK_OEM_3: return ImGuiKey_GraveAccent;
		case VK_CAPITAL: return ImGuiKey_CapsLock;
		case VK_SCROLL: return ImGuiKey_ScrollLock;
		case VK_NUMLOCK: return ImGuiKey_NumLock;
		case VK_SNAPSHOT: return ImGuiKey_PrintScreen;
		case VK_PAUSE: return ImGuiKey_Pause;
		case VK_NUMPAD0: return ImGuiKey_Keypad0;
		case VK_NUMPAD1: return ImGuiKey_Keypad1;
		case VK_NUMPAD2: return ImGuiKey_Keypad2;
		case VK_NUMPAD3: return ImGuiKey_Keypad3;
		case VK_NUMPAD4: return ImGuiKey_Keypad4;
		case VK_NUMPAD5: return ImGuiKey_Keypad5;
		case VK_NUMPAD6: return ImGuiKey_Keypad6;
		case VK_NUMPAD7: return ImGuiKey_Keypad7;
		case VK_NUMPAD8: return ImGuiKey_Keypad8;
		case VK_NUMPAD9: return ImGuiKey_Keypad9;
		case VK_DECIMAL: return ImGuiKey_KeypadDecimal;
		case VK_DIVIDE: return ImGuiKey_KeypadDivide;
		case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
		case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
		case VK_ADD: return ImGuiKey_KeypadAdd;
		case VK_LSHIFT: return ImGuiKey_LeftShift;
		case VK_LCONTROL: return ImGuiKey_LeftCtrl;
		case VK_LMENU: return ImGuiKey_LeftAlt;
		case VK_LWIN: return ImGuiKey_LeftSuper;
		case VK_RSHIFT: return ImGuiKey_RightShift;
		case VK_RCONTROL: return ImGuiKey_RightCtrl;
		case VK_RMENU: return ImGuiKey_RightAlt;
		case VK_RWIN: return ImGuiKey_RightSuper;
		case VK_APPS: return ImGuiKey_Menu;
		case '0': return ImGuiKey_0;
		case '1': return ImGuiKey_1;
		case '2': return ImGuiKey_2;
		case '3': return ImGuiKey_3;
		case '4': return ImGuiKey_4;
		case '5': return ImGuiKey_5;
		case '6': return ImGuiKey_6;
		case '7': return ImGuiKey_7;
		case '8': return ImGuiKey_8;
		case '9': return ImGuiKey_9;
		case 'A': return ImGuiKey_A;
		case 'B': return ImGuiKey_B;
		case 'C': return ImGuiKey_C;
		case 'D': return ImGuiKey_D;
		case 'E': return ImGuiKey_E;
		case 'F': return ImGuiKey_F;
		case 'G': return ImGuiKey_G;
		case 'H': return ImGuiKey_H;
		case 'I': return ImGuiKey_I;
		case 'J': return ImGuiKey_J;
		case 'K': return ImGuiKey_K;
		case 'L': return ImGuiKey_L;
		case 'M': return ImGuiKey_M;
		case 'N': return ImGuiKey_N;
		case 'O': return ImGuiKey_O;
		case 'P': return ImGuiKey_P;
		case 'Q': return ImGuiKey_Q;
		case 'R': return ImGuiKey_R;
		case 'S': return ImGuiKey_S;
		case 'T': return ImGuiKey_T;
		case 'U': return ImGuiKey_U;
		case 'V': return ImGuiKey_V;
		case 'W': return ImGuiKey_W;
		case 'X': return ImGuiKey_X;
		case 'Y': return ImGuiKey_Y;
		case 'Z': return ImGuiKey_Z;
		case VK_F1: return ImGuiKey_F1;
		case VK_F2: return ImGuiKey_F2;
		case VK_F3: return ImGuiKey_F3;
		case VK_F4: return ImGuiKey_F4;
		case VK_F5: return ImGuiKey_F5;
		case VK_F6: return ImGuiKey_F6;
		case VK_F7: return ImGuiKey_F7;
		case VK_F8: return ImGuiKey_F8;
		case VK_F9: return ImGuiKey_F9;
		case VK_F10: return ImGuiKey_F10;
		case VK_F11: return ImGuiKey_F11;
		case VK_F12: return ImGuiKey_F12;
		default: return ImGuiKey_None;
		}
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
		
		return DefWindowProcA(hWnd, message, wParam, lParam);
	}

	void DestroyWindow(HINSTANCE hInstance, HWND hwnd)
	{
		CloseWindow(hwnd);
		char className[256];
		GetClassNameA(hwnd, className, 256);
		UnregisterClassA(className, hInstance);
	}

private:
	bool m_Minimized = false;
	bool m_Maximized = false;
	int m_DisplayWidth = 1240;
	int m_DisplayHeight = 720;
	bool m_IsResizing = false;
	DemoApp* m_pGraphics{nullptr};
};

//int main()
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	ViewWrapper vw;
	return vw.Run(hInstance);
}
