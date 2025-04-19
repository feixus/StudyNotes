#include "stdafx.h"
#include "Graphics.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
//int main()
{
	std::cout << "hello dx12" << std::endl;

	Graphics* graphic = new Graphics(1024, 512, L"aaa");
	graphic->Initialize();

	return 0;
}