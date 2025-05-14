#include "stdafx.h"
#include "Graphics.h"
#include "Input.h"

const int gWindowWidth = 1240;
const int gWindowHeight = 720;
const int gMsaaSampleCount = 4;

class ViewWrapper
{
public:
	void Run()
	{
		MakeWindow();

		m_pGraphics = std::make_unique<Graphics>(gWindowWidth, gWindowHeight, gMsaaSampleCount);
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

		GameTimer::Stop();
	}

private:
	void MakeWindow()
	{
		WNDCLASSW wc;

		wc.hInstance = GetModuleHandle(nullptr);
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hIcon = 0;
		wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		wc.lpfnWndProc = WndProcStatic;
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpszClassName = L"wndClass";
		wc.lpszMenuName = nullptr;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

		if (!RegisterClass(&wc))
		{
			return;
		}

		int displayWidth = GetSystemMetrics(SM_CXSCREEN);
		int displayHeight = GetSystemMetrics(SM_CYSCREEN);

		DWORD windowStyle = WS_OVERLAPPEDWINDOW;

		RECT windowRec = { 0, 0, (LONG)gWindowWidth, (LONG)gWindowHeight };
		AdjustWindowRect(&windowRec, windowStyle, false);

		uint32_t windowWidth = windowRec.right - windowRec.left;
		uint32_t windowHeight = windowRec.bottom - windowRec.top;

		int x = (displayWidth - windowWidth) / 2;
		int y = (displayHeight - windowHeight) / 2;

		m_Hwnd = CreateWindowEx(0,
			L"wndClass",
			L"Hello World DX12",
			windowStyle,
			x,
			y,
			windowWidth,
			windowHeight,
			nullptr,
			nullptr,
			GetModuleHandle(nullptr),
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
			// resize the window
		case WM_SIZE:
		{
			int windowWidth = LOWORD(lParam);
			int windowHeight = HIWORD(lParam);
			if (m_pGraphics && windowWidth > 0 && windowHeight > 0)
			{
				m_pGraphics->OnResize(windowWidth, windowHeight);
			}
			return 0;
		}
		case WM_KEYUP:
		{
			Input::Instance().UpdateKey((uint32_t)wParam, false);
			return 0;
		}
		case WM_KEYDOWN:
		{
			Input::Instance().UpdateKey((uint32_t)wParam, true);
			return 0;
		}
		case WM_LBUTTONDOWN:
		{
			Input::Instance().UpdateMouseKey(VK_LBUTTON, true);
			return 0;
		}
		case WM_LBUTTONUP:
		{
			Input::Instance().UpdateMouseKey(VK_LBUTTON, false);
			return 0;
		}
		case WM_MBUTTONDOWN:
		{
			Input::Instance().UpdateMouseKey(VK_MBUTTON, true);
			return 0;
		}
		case WM_MBUTTONUP:
		{
			Input::Instance().UpdateMouseKey(VK_MBUTTON, false);
			return 0;
		}
		case WM_RBUTTONDOWN:
		{
			Input::Instance().UpdateMouseKey(VK_RBUTTON, true);
			return 0;
		}
		case WM_RBUTTONUP:
		{
			Input::Instance().UpdateMouseKey(VK_RBUTTON, false);
			return 0;
		}
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
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
	HWND m_Hwnd = nullptr;
	std::unique_ptr<Graphics> m_pGraphics;
};

//int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
int main()
{
	std::cout << "hello dx12" << std::endl;

	ViewWrapper vw;
	vw.Run();

	return 0;
}
