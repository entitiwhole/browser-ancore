#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <wrl.h>
#include <WebView2.h>
#include <string>
#include <vector>
#include <functional>

using namespace Microsoft::WRL;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

struct TabWebView {
    ICoreWebView2Controller* ctrl = nullptr;
    ICoreWebView2* wv = nullptr;
    bool pending = false;
    EventRegistrationToken navStart = {0};
    EventRegistrationToken navEnd = {0};
    EventRegistrationToken srcChanged = {0};
    EventRegistrationToken titleChanged = {0};
    EventRegistrationToken favChanged = {0};
    EventRegistrationToken downloadToken = {0};
    EventRegistrationToken contextMenuToken = {0};
    EventRegistrationToken audioChanged = {0};
    EventRegistrationToken webResourceToken = {0};
    EventRegistrationToken fullscreenToken = {0};
    EventRegistrationToken newWindowToken = {0};
};

class MainWindow {
public:
    MainWindow(HINSTANCE hInst);
    ~MainWindow();

    HWND create(int nCmdShow);
    HWND handle() const { return m_hwnd; }
    HWND contentHwnd() const { return m_contentContainer; }

    ICoreWebView2Controller* chromeController() const { return m_chromeCtrl; }
    ICoreWebView2* chromeWebView() const { return m_chromeWv; }
    void postToChrome(const std::wstring& json);
    void toggleFullscreen();
    void updateZoomDisplay();

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(UINT, WPARAM, LPARAM);

    void createWebView();
    void createChromeWebView();
    void createPanelWebView();
    void resizeViews();
    void showPanelOverlay();
    void clearPanelSurface();
    void postToPanel(const std::wstring& msg);
    void flushPendingPanelMessage();
    int chromeBarHeight() const;
    void applyWheelZoom(short wheelDelta);
    int chromeHeight() const;
    int m_panelEpoch = 0;

    // Per-tab WebView management
    void ensureTabContent(int idx);
    void showOnlyTab(int idx);
    void destroyTabContent(int idx);
    void reorderTabContent(int fromIdx, int toIdx);
    void resizeAllTabContent();
    void setupTabWebView(TabWebView& tv, ICoreWebView2Controller* ctrl, int tabIdx);
    ICoreWebView2* activeWv();
    ICoreWebView2Controller* activeCtrl();

    HINSTANCE m_hInst;
    HWND m_hwnd = nullptr;
    bool m_suppressZoomSync = false;
    HWND m_chromeContainer = nullptr;
    HWND m_contentContainer = nullptr;
    HWND m_panelContainer = nullptr;

    ICoreWebView2Environment* m_env = nullptr;
    ICoreWebView2Controller* m_chromeCtrl = nullptr;
    ICoreWebView2* m_chromeWv = nullptr;
    ICoreWebView2Controller* m_panelCtrl = nullptr;
    ICoreWebView2* m_panelWv = nullptr;
    std::vector<TabWebView> m_tabViews;

    EventRegistrationToken m_chromeMsgToken;
    EventRegistrationToken m_panelMsgToken;
    EventRegistrationToken m_panelNavToken;

    bool m_panelReady = false;
    std::wstring m_pendingPanelMsg;
    std::vector<ComPtr<ICoreWebView2BytesReceivedChangedEventHandler>> m_dlBytesHandlers;
    std::vector<ComPtr<ICoreWebView2StateChangedEventHandler>> m_dlStateHandlers;
    std::vector<ComPtr<ICoreWebView2DownloadOperation>> m_downloadOps;

    bool m_isPanelExpanded = false;
    bool m_isFullscreen = false;
    double m_zoomFactor = 1.0;
    RECT m_savedRect = {};
};
