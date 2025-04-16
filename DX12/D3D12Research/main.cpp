#include "stdafx.h"
#include "Graphics.h"

int main()
{
	std::cout << "hello dx12" << std::endl;

	Graphics* graphic = new Graphics(1024, 512, L"aaa");
	graphic->OnInit();

	return 0;
}