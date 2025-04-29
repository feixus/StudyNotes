#include "stdafx.h"
#include "Graphics.h"
#include "External/imgui/imgui_impl_win32.h"

//int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
int WINAPI WinMain(HINSTANCE , HINSTANCE , LPSTR , int )
{
	std::cout << "hello dx12" << std::endl;

	Graphics* graphic = new Graphics(1024, 512);
	graphic->Initialize();

	return 0;
}