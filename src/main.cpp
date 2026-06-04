#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif
#include <windows.h>
#include <commctrl.h>
#include <wrl.h>
#include <WebView2.h>
#include "browsercore.h"
#include "mainwindow.h"

using namespace Microsoft::WRL;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icex = {sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&icex);

    OleInitialize(nullptr);

    BrowserCore* core = BrowserCore::instance();
    core->initialize(hInstance);

    MainWindow window(hInstance);
    if (!window.create(nCmdShow)) {
        MessageBoxW(nullptr, L"Failed to create browser window.", L"AnCore Browser", MB_ICONERROR);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    OleUninitialize();
    core->shutdown();

    return 0;
}
