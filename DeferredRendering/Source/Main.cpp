#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include "App.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    auto app = std::make_unique<MRenderer::App>(hInstance);
    int ret_code = app->Run();
    return ret_code;
}