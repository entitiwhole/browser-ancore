#include "mainwindow.h"
#include "resource.h"
#include "browsercore.h"
#include "saveas.h"
#include "contextmenu.h"
#include <windowsx.h>
#include <shlobj.h>
#include <urlmon.h>
#include <algorithm>
#include <utility>

#pragma comment(lib, "urlmon.lib")

namespace {

void registerManualSave(const std::wstring& path) {
    size_t pos = path.rfind(L'\\');
    std::wstring fname = (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
    DownloadInfo di;
    di.fileName = fname;
    di.path = path;
    di.state = 2; // COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED
    di.receivedBytes = di.totalBytes = 0;
    BrowserCore::instance()->addDownload(di);
    BrowserCore::instance()->setStatusText(L"Сохранено: " + fname);
}

std::wstring uniqueDownloadPath(const std::wstring& dir, const std::wstring& fname) {
    std::wstring base = dir;
    if (!base.empty() && base.back() != L'\\') base += L'\\';
    std::wstring path = base + fname;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        return path;
    size_t dot = fname.rfind(L'.');
    std::wstring stem = (dot != std::wstring::npos) ? fname.substr(0, dot) : fname;
    std::wstring ext = (dot != std::wstring::npos) ? fname.substr(dot) : L"";
    for (int n = 1; n < 1000; n++) {
        wchar_t suffix[16];
        swprintf(suffix, 16, L" (%d)", n);
        path = base + stem + suffix + ext;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            return path;
    }
    return base + fname;
}

} // namespace

static const wchar_t* CLASS_NAME = L"AnCoreBrowserWindow";
static HBRUSH brBg = CreateSolidBrush(RGB(0x0F, 0x0F, 0x0F));

LRESULT CALLBACK MainWindow::wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    MainWindow* s = (MainWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!s && m == WM_CREATE) {
        auto* cs = (CREATESTRUCTW*)l;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        s = (MainWindow*)cs->lpCreateParams;
    }
    return s ? s->handleMessage(m, w, l) : DefWindowProcW(h, m, w, l);
}

MainWindow::MainWindow(HINSTANCE h) : m_hInst(h) {}
MainWindow::~MainWindow() {
    for (auto& tv : m_tabViews) {
        if (tv.wv) {
            tv.wv->remove_NavigationStarting(tv.navStart);
            tv.wv->remove_NavigationCompleted(tv.navEnd);
            tv.wv->remove_SourceChanged(tv.srcChanged);
            tv.wv->remove_DocumentTitleChanged(tv.titleChanged);
            ICoreWebView2_15* wv15 = nullptr;
            if (SUCCEEDED(tv.wv->QueryInterface(IID_ICoreWebView2_15, (void**)&wv15))) {
                wv15->remove_FaviconChanged(tv.favChanged);
                wv15->remove_IsDocumentPlayingAudioChanged(tv.audioChanged);
                wv15->Release();
            }
            ICoreWebView2_4* wv4 = nullptr;
            if (SUCCEEDED(tv.wv->QueryInterface(IID_ICoreWebView2_4, (void**)&wv4))) {
                wv4->remove_DownloadStarting(tv.downloadToken);
                wv4->Release();
            }
            tv.wv->remove_ContainsFullScreenElementChanged(tv.fullscreenToken);
            ICoreWebView2_11* wv11 = nullptr;
            if (SUCCEEDED(tv.wv->QueryInterface(IID_ICoreWebView2_11, (void**)&wv11))) {
                wv11->remove_ContextMenuRequested(tv.contextMenuToken);
                wv11->Release();
            }
            tv.wv->remove_WebResourceRequested(tv.webResourceToken);
            ICoreWebView2_6* wv6_del = nullptr;
            if (SUCCEEDED(tv.wv->QueryInterface(IID_ICoreWebView2_6, (void**)&wv6_del))) {
                wv6_del->remove_NewWindowRequested(tv.newWindowToken);
                wv6_del->Release();
            }
            tv.wv->Release();
        }
        if (tv.ctrl) tv.ctrl->Release();
    }
    m_tabViews.clear();
    if (m_chromeWv) { m_chromeWv->remove_WebMessageReceived(m_chromeMsgToken); m_chromeWv->Release(); }
    if (m_chromeCtrl) m_chromeCtrl->Release();
    if (m_panelWv) {
        m_panelWv->remove_WebMessageReceived(m_panelMsgToken);
        m_panelWv->remove_NavigationCompleted(m_panelNavToken);
        m_panelWv->Release();
    }
    if (m_panelCtrl) m_panelCtrl->Release();
    if (m_env) m_env->Release();
}

HWND MainWindow::create(int nCmdShow) {
    HICON appIcon = LoadIconW(m_hInst, MAKEINTRESOURCEW(IDI_APP));
    if (!appIcon) appIcon = LoadIconW(nullptr, IDI_APPLICATION);
    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW), CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW, wndProc, 0, 0, m_hInst,
                      appIcon, LoadCursorW(nullptr, IDC_ARROW),
                      (HBRUSH)GetStockObject(BLACK_BRUSH), nullptr, CLASS_NAME, nullptr};
    RegisterClassExW(&wc);

    int cx = GetSystemMetrics(SM_CXSCREEN), cy = GetSystemMetrics(SM_CYSCREEN);
    m_hwnd = CreateWindowExW(0, CLASS_NAME, L"AnCore Browser",
                             WS_POPUP | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU,
                             (cx - 1500) / 2, (cy - 900) / 2, 1500, 900,
                             nullptr, nullptr, m_hInst, this);
    if (!m_hwnd) return nullptr;

    if (appIcon) {
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)appIcon);
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)appIcon);
    }

    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DWORD corner = 2;
    DwmSetWindowAttribute(m_hwnd, 33, &corner, sizeof(corner));

    HRGN rgn = CreateRoundRectRgn(0, 0, 1500, 900, 12, 12);
    SetWindowRgn(m_hwnd, rgn, TRUE);

    RECT rc; GetClientRect(m_hwnd, &rc);
    int barH = chromeBarHeight();
    int chromeH = chromeHeight();
    m_chromeContainer = CreateWindowExW(0, L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE, 0, 0, rc.right, chromeH,
        m_hwnd, nullptr, m_hInst, nullptr);
    m_contentContainer = CreateWindowExW(0, L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, barH, rc.right,
        std::max<int>(rc.bottom - barH, 0),
        m_hwnd, nullptr, m_hInst, nullptr);
    m_panelContainer = CreateWindowExW(0, L"STATIC", nullptr,
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, barH, rc.right,
        std::max<int>(rc.bottom - barH, 0),
        m_hwnd, nullptr, m_hInst, nullptr);

    auto bc = BrowserCore::instance();
    bc->postToChrome = [this](const std::wstring& j) { postToChrome(j); };
    bc->setMainWindow(m_hwnd);

    bc->navigateContent = [this](const std::wstring& u) {
        int idx = BrowserCore::instance()->currentTab();
        if (idx >= 0 && idx < (int)m_tabViews.size() && m_tabViews[idx].wv)
            m_tabViews[idx].wv->Navigate(u.c_str());
        else if (!m_tabViews.empty())
            ensureTabContent(idx);
    };
    bc->switchTabContent = [this](int idx) {
        ensureTabContent(idx);
        showOnlyTab(idx);
        auto* b = BrowserCore::instance();
        if (idx >= 0 && idx < b->tabCount() && b->setZoomContent)
            b->setZoomContent(b->siteZoomForUrl(b->tab(idx).url));
    };
    bc->addTabContent = [this](int idx, const std::wstring& url) {
        if (idx >= (int)m_tabViews.size()) {
            m_tabViews.resize(idx + 1);
        }
        // Create WebView in background for the new tab
        ensureTabContent(idx);
    };
    bc->removeTabContent = [this](int idx) {
        destroyTabContent(idx);
        if (idx >= 0 && idx < (int)m_tabViews.size())
            m_tabViews.erase(m_tabViews.begin() + idx);
    };
    bc->reorderTabContent = [this](int from, int to) { reorderTabContent(from, to); };
    bc->goBackContent = [this]() {
        ICoreWebView2* wv = activeWv();
        if (wv) wv->GoBack();
    };
    bc->goForwardContent = [this]() {
        ICoreWebView2* wv = activeWv();
        if (wv) wv->GoForward();
    };
    bc->reloadContent = [this]() {
        ICoreWebView2* wv = activeWv();
        if (wv) wv->Reload();
    };
    bc->reloadTabContent = [this](int idx) {
        if (idx >= 0 && idx < (int)m_tabViews.size() && m_tabViews[idx].wv)
            m_tabViews[idx].wv->Reload();
    };
    bc->stopContent = [this]() {
        ICoreWebView2* wv = activeWv();
        if (wv) wv->Stop();
    };
    bc->zoomContent = [this](double delta) {
        ICoreWebView2Controller* c = activeCtrl();
        if (!c) return;
        if (delta == -999) m_zoomFactor = 1.0;
        else m_zoomFactor = (std::max)(0.3, (std::min)(3.0, m_zoomFactor + delta));
        m_suppressZoomSync = true;
        c->put_ZoomFactor(m_zoomFactor);
        m_suppressZoomSync = false;
        auto* b = BrowserCore::instance();
        int idx = b->currentTab();
        if (idx >= 0 && idx < b->tabCount())
            b->saveSiteZoomForUrl(b->tab(idx).url, m_zoomFactor);
        b->sendStateToChrome();
    };
    bc->setZoomContent = [this](double factor) {
        factor = (std::max)(0.3, (std::min)(3.0, factor));
        m_zoomFactor = factor;
        if (ICoreWebView2Controller* c = activeCtrl()) {
            m_suppressZoomSync = true;
            c->put_ZoomFactor(m_zoomFactor);
            m_suppressZoomSync = false;
        }
        BrowserCore::instance()->sendStateToChrome();
    };
    bc->cancelDownloadContent = [this](int idx) {
        if (idx >= 0 && idx < (int)m_downloadOps.size() && m_downloadOps[idx])
            m_downloadOps[idx]->Cancel();
    };
    bc->clearDownloadOpsContent = [this]() { m_downloadOps.clear(); };
    bc->findContent = [this](const std::wstring& text, bool backward) {
        ICoreWebView2* wv = activeWv();
        if (!wv) return;
        std::wstring s;
        for (wchar_t c : text) {
            if (c == L'\\') s += L"\\\\";
            else if (c == L'\'') s += L"\\'";
            else if (c == L'\n') s += L"\\n";
            else if (c == L'\r') s += L"\\r";
            else s += c;
        }
        std::wstring script = L"window.find('" + s + L"',false," + (backward ? L"true" : L"false") + L",true,false,true)";
        wv->ExecuteScript(script.c_str(), nullptr);
    };
    bc->findClearContent = [this]() {
        ICoreWebView2* wv = activeWv();
        if (!wv) return;
        wv->ExecuteScript(L"window.getSelection().removeAllRanges();var s=window.getSelection();s.collapse(document.body,0)", nullptr);
    };
    bc->devtoolsContent = [this]() {
        ICoreWebView2* wv = activeWv();
        if (wv) wv->OpenDevToolsWindow();
    };
    bc->panelOpenChanged = [this](bool o) {
        m_isPanelExpanded = o;
        showPanelOverlay();
    };
    bc->contextMenuAction = [this](const std::wstring& action, ICoreWebView2* sender) {
        handleContextMenuAction(this, action, sender);
    };
    bc->getFullscreen = [this]() { return m_isFullscreen; };
    bc->getZoom = [this]() { return m_zoomFactor; };
    bc->toggleFullscreen = [this]() { toggleFullscreen(); };
    bc->dlPopoverChanged = [this]() { resizeViews(); };

    RegisterHotKey(m_hwnd, 1, MOD_CONTROL, 'T');
    RegisterHotKey(m_hwnd, 2, MOD_CONTROL, 'W');
    RegisterHotKey(m_hwnd, 3, 0, VK_F5);
    RegisterHotKey(m_hwnd, 4, MOD_CONTROL, VK_TAB);
    RegisterHotKey(m_hwnd, 5, MOD_CONTROL | MOD_SHIFT, VK_TAB);
    RegisterHotKey(m_hwnd, 6, 0, VK_F11);
    RegisterHotKey(m_hwnd, 7, MOD_CONTROL, 'L');
    RegisterHotKey(m_hwnd, 8, MOD_ALT, VK_LEFT);
    RegisterHotKey(m_hwnd, 9, MOD_ALT, VK_RIGHT);
    RegisterHotKey(m_hwnd, 10, MOD_CONTROL, VK_OEM_PLUS);
    RegisterHotKey(m_hwnd, 11, MOD_CONTROL, VK_OEM_MINUS);
    RegisterHotKey(m_hwnd, 12, MOD_CONTROL, '0');
    RegisterHotKey(m_hwnd, 13, MOD_CONTROL, VK_ADD);
    RegisterHotKey(m_hwnd, 14, MOD_CONTROL, VK_SUBTRACT);
    RegisterHotKey(m_hwnd, 15, 0, VK_F3);
    RegisterHotKey(m_hwnd, 16, MOD_SHIFT, VK_F3);
    RegisterHotKey(m_hwnd, 17, MOD_CONTROL, 'F');

    createWebView();
    ShowWindow(m_hwnd, nCmdShow);
    return m_hwnd;
}

void MainWindow::createWebView() {
    auto envCb = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [this](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(hr) || !env) return E_FAIL;
            m_env = env; m_env->AddRef();
            createChromeWebView();
            return S_OK;
        });
    SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
        L"--disable-blink-features=AutomationControlled "
        L"--disable-features=ChromeWhatsNewUI,ChromeInBrowser,MsWebView2"
        L" --disable-sync --disable-component-update");
    CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr, envCb.Get());
}

void MainWindow::createChromeWebView() {
    auto cb = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [this](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
            if (FAILED(hr) || !ctrl) return E_FAIL;
            m_chromeCtrl = ctrl; m_chromeCtrl->AddRef();
            m_chromeCtrl->get_CoreWebView2(&m_chromeWv); m_chromeWv->AddRef();

            ICoreWebView2Settings* s = nullptr;
            m_chromeWv->get_Settings(&s);
            if (s) {
                s->put_IsScriptEnabled(TRUE);
                s->put_IsWebMessageEnabled(TRUE);
                s->put_AreDevToolsEnabled(FALSE);
                s->put_IsStatusBarEnabled(FALSE);
                s->Release();
            }

            m_chromeWv->add_WebMessageReceived(
                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                    [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* a) -> HRESULT {
                        wchar_t* m = nullptr;
                        a->TryGetWebMessageAsString(&m);
                        if (m) { BrowserCore::instance()->handleChromeMessage(m); CoTaskMemFree(m); }
                        return S_OK;
                    }).Get(), &m_chromeMsgToken);

            ICoreWebView2Controller2* ctrl2 = nullptr;
            if (SUCCEEDED(m_chromeCtrl->QueryInterface(IID_ICoreWebView2Controller2, (void**)&ctrl2))) {
                COREWEBVIEW2_COLOR bg = {0, 0, 0, 0};
                ctrl2->put_DefaultBackgroundColor(bg);
                ctrl2->Release();
            }

            resizeViews();
            m_chromeCtrl->put_IsVisible(TRUE);
            m_chromeWv->add_NavigationCompleted(
                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                    [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* a) -> HRESULT {
                        BOOL ok = FALSE;
                        a->get_IsSuccess(&ok);
                        if (ok && m_chromeWv)
                            m_chromeWv->ExecuteScript(
                                L"(function(){if(typeof reportChromeLayout==='function')reportChromeLayout();})()",
                                nullptr);
                        return S_OK;
                    }).Get(), nullptr);
            m_chromeWv->Navigate(BrowserCore::instance()->chromeUrl().c_str());
            BrowserCore::instance()->sendStateToChrome();

            createPanelWebView();
            return S_OK;
        });
    m_env->CreateCoreWebView2Controller(m_chromeContainer, cb.Get());
}

void MainWindow::createPanelWebView() {
    auto cb = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [this](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
            if (FAILED(hr) || !ctrl) return E_FAIL;
            m_panelCtrl = ctrl; m_panelCtrl->AddRef();
            m_panelCtrl->get_CoreWebView2(&m_panelWv); m_panelWv->AddRef();

            ICoreWebView2Controller2* ctrl2 = nullptr;
            if (SUCCEEDED(m_panelCtrl->QueryInterface(IID_ICoreWebView2Controller2, (void**)&ctrl2))) {
                COREWEBVIEW2_COLOR bg = {0, 0, 0, 0};
                ctrl2->put_DefaultBackgroundColor(bg);
                ctrl2->Release();
            }

            ICoreWebView2Settings* s = nullptr;
            m_panelWv->get_Settings(&s);
            if (s) {
                s->put_IsScriptEnabled(TRUE);
                s->put_IsWebMessageEnabled(TRUE);
                s->put_AreDevToolsEnabled(FALSE);
                s->put_IsStatusBarEnabled(FALSE);
                s->Release();
            }

            m_panelWv->add_WebMessageReceived(
                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                    [](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* a) -> HRESULT {
                        wchar_t* m = nullptr;
                        a->TryGetWebMessageAsString(&m);
                        if (m) {
                            BrowserCore::instance()->handleContentMessage(m, sender);
                            CoTaskMemFree(m);
                        }
                        return S_OK;
                    }).Get(), &m_panelMsgToken);

            m_panelWv->add_NavigationCompleted(
                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                    [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* a) -> HRESULT {
                        BOOL ok = FALSE;
                        a->get_IsSuccess(&ok);
                        if (ok) {
                            m_panelReady = true;
                            flushPendingPanelMessage();
                            auto* bc = BrowserCore::instance();
                        }
                        return S_OK;
                    }).Get(), &m_panelNavToken);

            resizeViews();
            m_panelCtrl->put_IsVisible(FALSE);
            m_panelReady = false;
            m_panelWv->Navigate(BrowserCore::instance()->panelUrl().c_str());

            // Now create first tab's content WebView
            int idx = BrowserCore::instance()->currentTab();
            if (idx >= 0) {
                ensureTabContent(idx);
                showOnlyTab(idx);
            }
            return S_OK;
        });
    m_env->CreateCoreWebView2Controller(m_panelContainer, cb.Get());
}

void MainWindow::setupTabWebView(TabWebView& tv, ICoreWebView2Controller* ctrl, int tabIdx) {
    tv.ctrl = ctrl;
    tv.ctrl->AddRef();
    tv.ctrl->get_CoreWebView2(&tv.wv);
    tv.wv->AddRef();
    tv.pending = false;

    // Dark background for content WebViews
    ICoreWebView2Controller2* ctrl2bg = nullptr;
    if (SUCCEEDED(tv.ctrl->QueryInterface(IID_ICoreWebView2Controller2, (void**)&ctrl2bg))) {
        COREWEBVIEW2_COLOR bg = {255, 0x0F, 0x0F, 0x0F};
        ctrl2bg->put_DefaultBackgroundColor(bg);
        ctrl2bg->Release();
    }

    // Settings
    ICoreWebView2Settings* s = nullptr;
    tv.wv->get_Settings(&s);
    if (s) {
        s->put_IsScriptEnabled(TRUE);
        s->put_IsWebMessageEnabled(TRUE);
        s->put_AreDevToolsEnabled(FALSE);

        ICoreWebView2Settings2* s2 = nullptr;
        s->QueryInterface(IID_ICoreWebView2Settings2, (void**)&s2);
        if (s2) {
            s2->put_UserAgent(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                L"AppleWebKit/537.36 (KHTML, like Gecko) "
                L"Chrome/128.0.0.0 Safari/537.36");
            s2->Release();
        }

        ICoreWebView2Settings6* s6 = nullptr;
        s->QueryInterface(IID_ICoreWebView2Settings6, (void**)&s6);
        if (s6) {
            s6->put_IsZoomControlEnabled(TRUE);
            s6->Release();
        }

        s->Release();
    }

    // Anti-detection scripts
    tv.wv->AddScriptToExecuteOnDocumentCreated(
        L"(function(){"
        L"try{"
        L"  // Capture postMessage BEFORE hiding chrome.webview"
        L"  var __wvMsg=null;"
        L"  try{var __wv=window.chrome&&window.chrome.webview;if(__wv)__wvMsg=__wv.postMessage.bind(__wv);}catch(ee){}"
        L"  window.__ancorePost=function(cmd){if(__wvMsg)__wvMsg(cmd);};"
        L"  // Core navigator overrides"
        L"  Object.defineProperty(navigator,'webdriver',{get:()=>undefined});"
        L"  Object.defineProperty(navigator,'languages',{get:()=>['ru-RU','ru','en-US','en']});"
        L"  Object.defineProperty(navigator,'platform',{get:()=>'Win32'});"
        L"  Object.defineProperty(navigator,'maxTouchPoints',{get:()=>0});"
        L"  Object.defineProperty(navigator,'deviceMemory',{get:()=>8});"
        L"  Object.defineProperty(navigator,'hardwareConcurrency',{get:()=>8});"
        L"  // userAgentData spoof"
        L"  try{Object.defineProperty(navigator,'userAgentData',{get:()=>{"
        L"    return {brands:[{brand:'Google Chrome',version:'128'},{brand:'Chromium',version:'128'},{brand:'Not=A?Brand',version:'99'}],mobile:false,platform:'Windows',getHighEntropyValues:function(){return Promise.resolve({platform:'Windows',platformVersion:'15.0.0',architecture:'x64',model:'',uaFullVersion:'128.0.0.0',fullVersionList:[{brand:'Google Chrome',version:'128'},{brand:'Chromium',version:'128'},{brand:'Not=A?Brand',version:'99'}]})}};"
        L"  }});}catch(ee){}"
        L"  // Plugins (realistic PluginArray-style)"
        L"  var __pl={length:4,item:function(i){return this[i]||null},namedItem:function(){return null}};"
        L"  __pl[0]={name:'Chrome PDF Plugin',filename:'internal-pdf-viewer',description:'Portable Document Format',length:1};"
        L"  __pl[1]={name:'Chrome PDF Viewer',filename:'mhjfbmdgcfjbbpaeojofohoefgiehjai',description:'',length:1};"
        L"  __pl[2]={name:'Native Client',filename:'internal-nacl-plugin',description:'',length:1};"
        L"  __pl[3]={name:'Widevine Content Decryption Module',filename:'widevinecdm.dll',description:'Enables Widevine licenses',length:1};"
        L"  Object.defineProperty(navigator,'plugins',{get:()=>__pl});"
        L"  // Permissions override"
        L"  if(navigator.permissions&&navigator.permissions.query){var __oq=navigator.permissions.query;navigator.permissions.query=function(p){"
        L"    if(p&&p.name&&(p.name==='notifications'||p.name==='geolocation'||p.name==='midi'||p.name==='camera'||p.name==='microphone'))"
        L"      return Promise.resolve({state:'prompt',onchange:null});"
        L"    return __oq.call(navigator.permissions,p);"
        L"  };}"
        L"  // Chrome runtime – WITHOUT webview"
        L"  window.chrome={runtime:{OnInstalledReason:{CHROME_UPDATE:'chrome_update',INSTALL:'install',SHARED_MODULE_UPDATE:'shared_module_update',UPDATE:'update'},OnRestartRequiredReason:{APP_UPDATE:'app_update',OS_UPDATE:'os_update',PERIODIC:'periodic'}},csi:function(){return{}}"
        L"    ,loadTimes:function(){return{}}"
        L"    ,app:{isInstalled:false,InstallState:{DISABLED:'disabled',INSTALLED:'installed',NOT_INSTALLED:'not_installed'},RunningState:{CANNOT_RUN:'cannot_run',READY_TO_RUN:'ready_to_run',RUNNING:'running'}}"
        L"    ,runtime:{id:'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'}"
        L"  };"
        L"  if(__wv)window.chrome.webview=__wv;"
        L"  for(var k in window){if(typeof k==='string'&&k.charAt(0)==='$')window[k]=undefined}"
        L"  // Timezone → UTC"
        L"  Date.prototype.getTimezoneOffset=function(){return 0};"
        L"  var __idf=Intl.DateTimeFormat.prototype.resolvedOptions;Intl.DateTimeFormat.prototype.resolvedOptions=function(){var r=__idf.call(this);r.timeZone='UTC';return r};"
        L"  // Screen"
        L"  Object.defineProperty(screen,'colorDepth',{get:()=>24});"
        L"  Object.defineProperty(screen,'pixelDepth',{get:()=>24});"
        L"  // Canvas fingerprinting"
        L"  var __gcp=HTMLCanvasElement.prototype.getContext;HTMLCanvasElement.prototype.getContext=function(){var ctx=__gcp.apply(this,arguments);if(ctx&&ctx.getImageData){var __gid=ctx.getImageData;ctx.getImageData=function(){var img=__gid.apply(this,arguments);if(img&&img.data){for(var i=0;i<img.data.length;i+=4){if(img.data[i]%2===0)img.data[i]++}}return img}}return ctx}"
        L"  // WebGL fingerprinting"
        L"  try{var __gl=document.createElement('canvas').getContext('webgl');if(__gl){var __gp=__gl.getParameter;__gl.getParameter=function(p){"
        L"    if(p===37445)return 'Intel Inc.';"
        L"    if(p===37446)return 'Intel Iris OpenGL Engine';"
        L"    if(p===7936||p===7937||p===35724||p===35725)return '';"
        L"    return __gp.call(this,p)"
        L"  }}}catch(ee){}"
        L"}catch(e){}"
        L"// Wheel zoom via closure – no global chrome.webview needed"
        L"if(__wvMsg){window.addEventListener('wheel',function(e){"
        L"  if(e.ctrlKey){e.preventDefault();__wvMsg(e.deltaY<0?'zoomIn':'zoomOut')}"
        L"},{capture:true,passive:false})}"
        L"// Link hover → status bar"
        L"document.addEventListener('mouseover',function(e){var a=e.target.closest('a');if(a&&a.href)__wvMsg&&__wvMsg('stbar:'+a.href)},{capture:true})"
        L"document.addEventListener('mouseout',function(e){if(e.target.closest('a'))__wvMsg&&__wvMsg('stbar:')},{capture:true})"
        L"function __ancoreLinkClick(e,a){if(e.button===1||e.ctrlKey||e.metaKey){e.preventDefault();e.stopImmediatePropagation();"
        L"var bg=(e.button===1);__wvMsg&&__wvMsg((bg?'linkBg:':'linkNew:')+a.href);return true}return false}"
        L"document.addEventListener('click',function(e){var a=e.target.closest('a[href]');if(a)__ancoreLinkClick(e,a)},true);"
        L"document.addEventListener('auxclick',function(e){var a=e.target.closest('a[href]');if(a&&e.button===1)__ancoreLinkClick(e,a)},true);"
        L"})();",
        nullptr);

    {
        {
            std::wstring ctxScript = contextMenuInjectScript();
            if (!ctxScript.empty())
                tv.wv->AddScriptToExecuteOnDocumentCreated(ctxScript.c_str(), nullptr);
        }
    }

    tv.ctrl->add_ZoomFactorChanged(
        Callback<ICoreWebView2ZoomFactorChangedEventHandler>(
            [this, tabIdx](ICoreWebView2Controller* sender, IUnknown*) -> HRESULT {
                if (m_suppressZoomSync) return S_OK;
                double z = 1.0;
                sender->get_ZoomFactor(&z);
                auto* b = BrowserCore::instance();
                if (b->currentTab() != tabIdx) return S_OK;
                m_zoomFactor = z;
                if (tabIdx >= 0 && tabIdx < b->tabCount())
                    b->saveSiteZoomForUrl(b->tab(tabIdx).url, z);
                b->sendStateToChrome();
                updateZoomDisplay();
                return S_OK;
            }).Get(), nullptr);

    // WebMessage received
    tv.wv->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* a) -> HRESULT {
                wchar_t* m = nullptr;
                a->TryGetWebMessageAsString(&m);
                if (m) {
                    BrowserCore::instance()->handleContentMessage(m, sender);
                    CoTaskMemFree(m);
                }
                return S_OK;
            }).Get(), nullptr); // token not stored, one per lifetime

    // Navigation events
    tv.wv->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [](ICoreWebView2* sender, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                auto bc = BrowserCore::instance();
                bc->setLoading(true);
                wchar_t* uri = nullptr;
                args->get_Uri(&uri);
                if (uri && bc->isAdBlockEnabled() && bc->isUrlBlocked(uri)) {
                    std::wstring u = uri;
                    if (u.find(L"blockpage.html") == std::wstring::npos) {
                        std::wstring block = bc->blockPageUrl() + L"?url=" +
                            BrowserCore::encodeUrlParam(u);
                        args->put_Cancel(TRUE);
                        if (sender) sender->Navigate(block.c_str());
                    }
                }
                if (uri) CoTaskMemFree(uri);
                bc->sendStateToChrome();
                return S_OK;
            }).Get(), &tv.navStart);

    tv.wv->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                BrowserCore::instance()->setLoading(false);
                // Find which tab this WebView belongs to
                for (size_t i = 0; i < m_tabViews.size(); i++) {
                    if (m_tabViews[i].wv == sender) {
                        BOOL back = FALSE, fwd = FALSE;
                        sender->get_CanGoBack(&back);
                        sender->get_CanGoForward(&fwd);
                        BrowserCore::instance()->setNavButtons(!!back, !!fwd);
                        break;
                    }
                }
                BrowserCore::instance()->sendStateToChrome();
                return S_OK;
            }).Get(), &tv.navEnd);

    tv.wv->add_SourceChanged(
        Callback<ICoreWebView2SourceChangedEventHandler>(
            [this](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                if (!sender) return S_OK;
                // Find which tab this WebView belongs to
                int tabIdx = -1;
                for (size_t i = 0; i < m_tabViews.size(); i++) {
                    if (m_tabViews[i].wv == sender) { tabIdx = (int)i; break; }
                }
                if (tabIdx < 0) return S_OK;
                wchar_t* u = nullptr;
                sender->get_Source(&u);
                if (u) {
                    auto bc = BrowserCore::instance();
                    bc->setTabUrl(tabIdx, u);
                    if (tabIdx == bc->currentTab() && bc->setZoomContent)
                        bc->setZoomContent(bc->siteZoomForUrl(u));
                    // Convert phantom to real when navigating away from newtab
                    if (tabIdx == bc->currentTab() && tabIdx < bc->tabCount() &&
                        bc->tab(tabIdx).phantom &&
                        wcsstr(u, L"newtab") == nullptr &&
                        wcsncmp(u, L"ancore:", 7) != 0) {
                        bc->setTabPhantom(tabIdx, false);
                    }
                    bc->addHistory(u, L"");
                    int sec = 0;
                    if (wcsncmp(u, L"https://", 8) == 0) sec = 1;
                    else if (wcsncmp(u, L"http://", 7) == 0) sec = 2;
                    BrowserCore::instance()->setSecurityState(sec);
                    CoTaskMemFree(u);
                }
                return S_OK;
            }).Get(), &tv.srcChanged);

    tv.wv->add_DocumentTitleChanged(
        Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                if (!sender) return S_OK;
                int tabIdx = -1;
                for (size_t i = 0; i < m_tabViews.size(); i++) {
                    if (m_tabViews[i].wv == sender) { tabIdx = (int)i; break; }
                }
                if (tabIdx < 0) return S_OK;
                wchar_t* t = nullptr;
                sender->get_DocumentTitle(&t);
                if (t) {
                    BrowserCore::instance()->setTabTitle(tabIdx, t);
                    BrowserCore::instance()->updateLastHistoryTitle(t);
                    if (tabIdx == BrowserCore::instance()->currentTab())
                        SetWindowTextW(m_hwnd, (std::wstring(t) + L" - AnCore Browser").c_str());
                    CoTaskMemFree(t);
                }
                return S_OK;
            }).Get(), &tv.titleChanged);

    // Favicon + Audio
    ICoreWebView2_15* wv15 = nullptr;
    if (SUCCEEDED(tv.wv->QueryInterface(IID_ICoreWebView2_15, (void**)&wv15))) {
        wv15->add_FaviconChanged(
            Callback<ICoreWebView2FaviconChangedEventHandler>(
                [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                    int tabIdx = -1;
                    for (size_t i = 0; i < m_tabViews.size(); i++) {
                        if (m_tabViews[i].wv == sender) { tabIdx = (int)i; break; }
                    }
                    if (tabIdx < 0) return S_OK;
                    ICoreWebView2_15* s15 = nullptr;
                    if (sender && SUCCEEDED(sender->QueryInterface(IID_ICoreWebView2_15, (void**)&s15))) {
                        wchar_t* uri = nullptr;
                        s15->get_FaviconUri(&uri);
                        if (uri) {
                            BrowserCore::instance()->setTabFavicon(tabIdx, uri);
                            CoTaskMemFree(uri);
                        }
                        auto* bc = BrowserCore::instance();
                        std::wstring host = BrowserCore::hostFromUrl(bc->tab(tabIdx).url);
                        s15->GetFavicon(
                            COREWEBVIEW2_FAVICON_IMAGE_FORMAT_PNG,
                            Callback<ICoreWebView2GetFaviconCompletedHandler>(
                                [bc, host](HRESULT hr, IStream* stream) -> HRESULT {
                                    if (SUCCEEDED(hr) && stream && !host.empty())
                                        bc->cacheFaviconStreamForHost(host, stream);
                                    return S_OK;
                                }).Get());
                        s15->Release();
                    }
                    return S_OK;
                }).Get(), &tv.favChanged);
        wv15->add_IsDocumentPlayingAudioChanged(
            Callback<ICoreWebView2IsDocumentPlayingAudioChangedEventHandler>(
                [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                    int tabIdx = -1;
                    for (size_t i = 0; i < m_tabViews.size(); i++) {
                        if (m_tabViews[i].wv == sender) { tabIdx = (int)i; break; }
                    }
                    if (tabIdx < 0) return S_OK;
                    BOOL playing = FALSE;
                    ICoreWebView2_15* s15 = nullptr;
                    if (sender && SUCCEEDED(sender->QueryInterface(IID_ICoreWebView2_15, (void**)&s15))) {
                        s15->get_IsDocumentPlayingAudio(&playing);
                        s15->Release();
                    }
                    BrowserCore::instance()->setTabAudio(tabIdx, !!playing);
                    return S_OK;
                }).Get(), &tv.audioChanged);
        wv15->Release();
    }

    // Fullscreen video
    tv.wv->add_ContainsFullScreenElementChanged(
        Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler>(
            [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                if (sender) {
                    BOOL fs = FALSE;
                    sender->get_ContainsFullScreenElement(&fs);
                    if (fs && !m_isFullscreen) {
                        ShowWindow(m_hwnd, SW_MAXIMIZE);
                        m_isFullscreen = true;
                    } else if (!fs && m_isFullscreen) {
                        ShowWindow(m_hwnd, SW_RESTORE);
                        m_isFullscreen = false;
                    }
                }
                return S_OK;
            }).Get(), &tv.fullscreenToken);

    // Downloads
    ICoreWebView2_4* wv4 = nullptr;
    if (SUCCEEDED(tv.wv->QueryInterface(IID_ICoreWebView2_4, (void**)&wv4))) {
        wv4->add_DownloadStarting(
            Callback<ICoreWebView2DownloadStartingEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* a) -> HRESULT {
                    auto bc = BrowserCore::instance();
                    std::wstring dlPath = bc->effectiveDownloadPath();
                    if (dlPath.empty()) return S_OK;

                    ICoreWebView2DownloadOperation* op = nullptr;
                    if (FAILED(a->get_DownloadOperation(&op)) || !op) return S_OK;

                    std::wstring fname;
                    wchar_t* suggested = nullptr;
                    if (SUCCEEDED(op->get_ResultFilePath(&suggested)) && suggested) {
                        std::wstring path = suggested;
                        CoTaskMemFree(suggested);
                        size_t pos = path.rfind(L'\\');
                        fname = (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
                    }
                    if (fname.empty()) {
                        wchar_t* uri = nullptr;
                        if (SUCCEEDED(op->get_Uri(&uri)) && uri) {
                            fname = fileNameFromUrl(uri);
                            CoTaskMemFree(uri);
                        }
                    }
                    if (fname.empty()) fname = L"download";

                    CreateDirectoryW(dlPath.c_str(), nullptr);
                    std::wstring fullPath = uniqueDownloadPath(dlPath, fname);

                    a->put_Cancel(FALSE);
                    a->put_Handled(FALSE);
                    a->put_ResultFilePath(fullPath.c_str());

                    INT64 total = 0;
                    op->get_TotalBytesToReceive(&total);
                    DownloadInfo di;
                    di.fileName = fname;
                    di.path = fullPath;
                    di.totalBytes = total;
                    di.receivedBytes = 0;
                    di.state = 0;
                    int dlIdx = (int)bc->downloads().size();
                    bc->addDownload(di);
                    if ((int)m_downloadOps.size() <= dlIdx)
                        m_downloadOps.resize(dlIdx + 1);
                    m_downloadOps[dlIdx] = op;

                    auto bytesHandler = Callback<ICoreWebView2BytesReceivedChangedEventHandler>(
                        [bc, dlIdx](ICoreWebView2DownloadOperation* dOp, IUnknown*) -> HRESULT {
                            INT64 recv = 0;
                            dOp->get_BytesReceived(&recv);
                            bc->updateDownload(dlIdx, recv, -1);
                            return S_OK;
                        });
                    auto stateHandler = Callback<ICoreWebView2StateChangedEventHandler>(
                        [bc, dlIdx](ICoreWebView2DownloadOperation* dOp, IUnknown*) -> HRESULT {
                            COREWEBVIEW2_DOWNLOAD_STATE state = COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS;
                            dOp->get_State(&state);
                            bc->updateDownload(dlIdx, -1, (int)state);
                            if (state == COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED)
                                bc->setStatusText(L"Загрузка завершена");
                            else if (state == COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED)
                                bc->setStatusText(L"Загрузка прервана");
                            return S_OK;
                        });
                    m_dlBytesHandlers.push_back(bytesHandler.Get());
                    m_dlStateHandlers.push_back(stateHandler.Get());
                    EventRegistrationToken bytesToken = {}, stateToken = {};
                    op->add_BytesReceivedChanged(bytesHandler.Get(), &bytesToken);
                    op->add_StateChanged(stateHandler.Get(), &stateToken);
                    return S_OK;
                }).Get(), &tv.downloadToken);
        wv4->Release();
    }

    ICoreWebView2_11* wv11 = nullptr;
    if (SUCCEEDED(tv.wv->QueryInterface(IID_ICoreWebView2_11, (void**)&wv11))) {
        wv11->add_ContextMenuRequested(
            Callback<ICoreWebView2ContextMenuRequestedEventHandler>(
                [this](ICoreWebView2* sender, ICoreWebView2ContextMenuRequestedEventArgs* args) -> HRESULT {
                    handleContextMenuRequested(this, sender, args);
                    return S_OK;
                }).Get(), &tv.contextMenuToken);
        wv11->Release();
    }

    // AdBlock: register WebResourceRequested if enabled
    if (BrowserCore::instance()->isAdBlockEnabled()) {
        BrowserCore::instance()->reloadAdBlock();
        ICoreWebView2Environment5* env5 = nullptr;
        if (SUCCEEDED(m_env->QueryInterface(IID_ICoreWebView2Environment5, (void**)&env5))) {
            tv.wv->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
            tv.wv->add_WebResourceRequested(
                Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                    [env5](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* a) -> HRESULT {
                        ICoreWebView2WebResourceRequest* req = nullptr;
                        a->get_Request(&req);
                        if (!req) return S_OK;
                        wchar_t* uri = nullptr;
                        req->get_Uri(&uri);
                        if (uri && BrowserCore::instance()->isUrlBlocked(uri)) {
                            ICoreWebView2WebResourceResponse* resp = nullptr;
                            IStream* stream = SHCreateMemStream(nullptr, 0);
                            if (stream) {
                                if (SUCCEEDED(env5->CreateWebResourceResponse(
                                    stream, 204, L"No Content", L"", &resp)) && resp) {
                                    a->put_Response(resp);
                                    resp->Release();
                                }
                                stream->Release();
                            }
                        }
                        if (uri) CoTaskMemFree(uri);
                        req->Release();
                        return S_OK;
                    }).Get(), &tv.webResourceToken);
            env5->Release();
        }
    }

    // NewWindowRequested — redirect to new tab instead of opening a separate window
    ICoreWebView2_6* wv6 = nullptr;
    if (SUCCEEDED(tv.wv->QueryInterface(IID_ICoreWebView2_6, (void**)&wv6))) {
        wv6->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* a) -> HRESULT {
                    a->put_Handled(TRUE);
                    wchar_t* uri = nullptr;
                    a->get_Uri(&uri);
                    if (uri) {
                        auto* bc = BrowserCore::instance();
                        BOOL inBg = FALSE;
                        ICoreWebView2WindowFeatures* wf = nullptr;
                        if (SUCCEEDED(a->get_WindowFeatures(&wf)) && wf) {
                            BOOL hasPos = FALSE, hasSize = FALSE;
                            wf->get_HasPosition(&hasPos);
                            wf->get_HasSize(&hasSize);
                            inBg = !hasPos && !hasSize;
                            wf->Release();
                        }
                        if (inBg) bc->addTabBackground(uri);
                        else bc->addTab(uri);
                        CoTaskMemFree(uri);
                    }
                    return S_OK;
                }).Get(), &tv.newWindowToken);
        wv6->Release();
    }
}

void MainWindow::ensureTabContent(int idx) {
    if (idx < 0) return;
    if (idx >= (int)m_tabViews.size())
        m_tabViews.resize(idx + 1);

    TabWebView& tv = m_tabViews[idx];
    if (tv.ctrl || tv.pending) return; // already created or being created

    tv.pending = true;

    auto cb = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [this, idx](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
            if (FAILED(hr) || !ctrl) {
                if (idx >= 0 && idx < (int)m_tabViews.size())
                    m_tabViews[idx].pending = false;
                return E_FAIL;
            }
            if (idx >= (int)m_tabViews.size()) {
                ctrl->Release();
                return E_FAIL;
            }

            TabWebView& tv = m_tabViews[idx];
            setupTabWebView(tv, ctrl, idx);

            // Position and initially hide
            RECT rc; GetClientRect(m_contentContainer, &rc);
            tv.ctrl->put_Bounds({0, 0, rc.right, rc.bottom});
            tv.ctrl->put_IsVisible(FALSE);

            auto bc = BrowserCore::instance();
            if (idx >= 0 && idx < bc->tabCount())
                tv.wv->Navigate(bc->tab(idx).url.c_str());
            if (bc->currentTab() == idx) {
                tv.ctrl->put_ZoomFactor(m_zoomFactor);
                tv.ctrl->put_IsVisible(TRUE);
            }
            return S_OK;
        });

    m_env->CreateCoreWebView2Controller(m_contentContainer, cb.Get());
}

void MainWindow::showOnlyTab(int idx) {
    for (int i = 0; i < (int)m_tabViews.size(); i++) {
        if (!m_tabViews[i].ctrl) continue;
        if (i == idx) {
            m_tabViews[i].ctrl->put_IsVisible(TRUE);
        } else {
            m_tabViews[i].ctrl->put_IsVisible(FALSE);
        }
    }
}

void MainWindow::reorderTabContent(int fromIdx, int toIdx) {
    if (fromIdx < 0 || fromIdx >= (int)m_tabViews.size() ||
        toIdx < 0 || toIdx >= (int)m_tabViews.size() || fromIdx == toIdx)
        return;
    TabWebView item = std::move(m_tabViews[fromIdx]);
    m_tabViews.erase(m_tabViews.begin() + fromIdx);
    m_tabViews.insert(m_tabViews.begin() + toIdx, std::move(item));
    showOnlyTab(BrowserCore::instance()->currentTab());
}

void MainWindow::destroyTabContent(int idx) {
    if (idx < 0 || idx >= (int)m_tabViews.size()) return;
    TabWebView& tv = m_tabViews[idx];
    if (tv.wv) {
        tv.wv->remove_NavigationStarting(tv.navStart);
        tv.wv->remove_NavigationCompleted(tv.navEnd);
        tv.wv->remove_SourceChanged(tv.srcChanged);
        tv.wv->remove_DocumentTitleChanged(tv.titleChanged);
        ICoreWebView2_15* wv15 = nullptr;
        if (SUCCEEDED(tv.wv->QueryInterface(IID_ICoreWebView2_15, (void**)&wv15))) {
            wv15->remove_FaviconChanged(tv.favChanged);
            wv15->Release();
        }
        ICoreWebView2_4* wv4 = nullptr;
        if (SUCCEEDED(tv.wv->QueryInterface(IID_ICoreWebView2_4, (void**)&wv4))) {
            wv4->remove_DownloadStarting(tv.downloadToken);
            wv4->Release();
        }
        ICoreWebView2_11* wv11 = nullptr;
        if (SUCCEEDED(tv.wv->QueryInterface(IID_ICoreWebView2_11, (void**)&wv11))) {
            wv11->remove_ContextMenuRequested(tv.contextMenuToken);
            wv11->Release();
        }
        ICoreWebView2_6* wv6_del = nullptr;
        if (SUCCEEDED(tv.wv->QueryInterface(IID_ICoreWebView2_6, (void**)&wv6_del))) {
            wv6_del->remove_NewWindowRequested(tv.newWindowToken);
            wv6_del->Release();
        }
        tv.wv->Release();
        tv.wv = nullptr;
    }
    if (tv.ctrl) {
        tv.ctrl->Close(); // close the WebView2
        tv.ctrl->Release();
        tv.ctrl = nullptr;
    }
    tv.pending = false;
}

ICoreWebView2* MainWindow::activeWv() {
    int idx = BrowserCore::instance()->currentTab();
    if (idx >= 0 && idx < (int)m_tabViews.size())
        return m_tabViews[idx].wv;
    return nullptr;
}

ICoreWebView2Controller* MainWindow::activeCtrl() {
    int idx = BrowserCore::instance()->currentTab();
    if (idx >= 0 && idx < (int)m_tabViews.size())
        return m_tabViews[idx].ctrl;
    return nullptr;
}

void MainWindow::toggleFullscreen() {
    m_isFullscreen = !m_isFullscreen;
    if (m_isFullscreen) {
        GetWindowRect(m_hwnd, &m_savedRect);
        SetWindowRgn(m_hwnd, nullptr, TRUE);
        HMONITOR mon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {sizeof(MONITORINFO)};
        if (GetMonitorInfoW(mon, &mi)) {
            SetWindowPos(m_hwnd, nullptr,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOZORDER);
        }
    } else {
        HRGN rgn = CreateRoundRectRgn(0, 0,
            m_savedRect.right - m_savedRect.left,
            m_savedRect.bottom - m_savedRect.top, 12, 12);
        SetWindowRgn(m_hwnd, rgn, TRUE);
        SetWindowPos(m_hwnd, nullptr,
            m_savedRect.left, m_savedRect.top,
            m_savedRect.right - m_savedRect.left,
            m_savedRect.bottom - m_savedRect.top,
            SWP_NOZORDER);
    }
    resizeViews();
    BrowserCore::instance()->sendStateToChrome();
}

static int scaleCssPx(HWND hwnd, int cssPx) {
    if (cssPx < 72) cssPx = 74;
    if (!hwnd) return cssPx;
    int dpi = GetDpiForWindow(hwnd);
    if (dpi <= 0) dpi = 96;
    return MulDiv(cssPx, dpi, 96);
}

int MainWindow::chromeBarHeight() const {
    auto* bc = BrowserCore::instance();
    return scaleCssPx(m_hwnd, bc ? bc->chromeBarHeightPx() : 0);
}

int MainWindow::chromeHeight() const {
    auto* bc = BrowserCore::instance();
    int cssPx = bc ? bc->chromeWndHeightPx() : 0;
    if (cssPx < 72) cssPx = bc ? bc->chromeBarHeightPx() : 0;
    return scaleCssPx(m_hwnd, cssPx);
}

void MainWindow::postToChrome(const std::wstring& json) {
    if (m_chromeWv)
        m_chromeWv->PostWebMessageAsString(json.c_str());
}

void MainWindow::applyWheelZoom(short wheelDelta) {
    if (wheelDelta == 0) return;
    auto* bc = BrowserCore::instance();
    if (bc->zoomContent)
        bc->zoomContent(wheelDelta > 0 ? 0.1 : -0.1);
}

void MainWindow::updateZoomDisplay() {
    wchar_t buf[64];
    swprintf(buf, 64, L"document.getElementById('zoomdisp').textContent='%d%%'", (int)(m_zoomFactor * 100 + 0.5));
    if (m_chromeWv) m_chromeWv->ExecuteScript(buf, nullptr);
}

void MainWindow::resizeViews() {
    if (!m_chromeContainer || !m_contentContainer) return;
    RECT rc; GetClientRect(m_hwnd, &rc);
    int barH = m_isFullscreen ? 0 : chromeBarHeight();
    int chromeH = m_isFullscreen ? 0 : chromeHeight();
    int ch2 = std::max<int>(rc.bottom - barH, 0);

    // Chrome may extend below barH for omnibox; content stays at barH (overlay, no page shift).
    SetWindowPos(m_chromeContainer, HWND_TOP, 0, 0, rc.right, chromeH, 0);
    if (m_chromeCtrl) m_chromeCtrl->put_Bounds({0, 0, rc.right, chromeH});

    SetWindowPos(m_contentContainer, nullptr, 0, barH, rc.right, ch2, SWP_NOZORDER);
    resizeAllTabContent();

    if (m_panelContainer)
        SetWindowPos(m_panelContainer, nullptr, 0, barH, rc.right, ch2, SWP_NOZORDER);
    if (m_panelCtrl)
        m_panelCtrl->put_Bounds({0, 0, rc.right, ch2});
}

void MainWindow::resizeAllTabContent() {
    RECT rc; GetClientRect(m_contentContainer, &rc);
    RECT bounds = {0, 0, rc.right, rc.bottom};
    for (auto& tv : m_tabViews) {
        if (tv.ctrl)
            tv.ctrl->put_Bounds(bounds);
    }
}

void MainWindow::clearPanelSurface() {
    if (!m_panelWv || !m_panelReady) return;
    m_panelWv->ExecuteScript(
        L"(function(){if(typeof resetPanelSurface==='function')resetPanelSurface();"
        L"else{var o=document.getElementById('overlay'),p=document.getElementById('panel');"
        L"if(p)p.innerHTML='';if(o)o.style.display='none';}})();",
        nullptr);
}

void MainWindow::postToPanel(const std::wstring& msg) {
    if (!m_panelWv) return;
    if (!m_panelReady) {
        m_pendingPanelMsg = msg;
        return;
    }
    m_panelWv->PostWebMessageAsString(msg.c_str());
}

void MainWindow::flushPendingPanelMessage() {
    if (!m_panelReady || m_pendingPanelMsg.empty() || !m_panelWv) return;
    m_panelWv->PostWebMessageAsString(m_pendingPanelMsg.c_str());
    m_pendingPanelMsg.clear();
}

void MainWindow::showPanelOverlay() {
    if (!m_panelWv) return;
    auto bc = BrowserCore::instance();

    if (!bc->isSettingsOpen() && !bc->isHistoryOpen() && !bc->isBookmarksOpen()) {
        ++m_panelEpoch;
        if (m_panelReady)
            postToPanel(L"__ancore__hide");
        ShowWindow(m_panelContainer, SW_HIDE);
        m_panelCtrl->put_IsVisible(FALSE);
        m_pendingPanelMsg.clear();
        return;
    }

    ShowWindow(m_panelContainer, SW_SHOWNA);
    SetWindowPos(m_panelContainer, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    m_panelCtrl->put_IsVisible(TRUE);

    const int ep = ++m_panelEpoch;
    wchar_t hdr[48];

    if (bc->isSettingsOpen()) {
        auto esc = [](const std::wstring& s) {
            std::wstring o;
            for (wchar_t c : s) {
                if (c == L'"' || c == L'\\') o += L'\\';
                o += c;
            }
            return o;
        };
        std::wstring json = L"{\"engine\":\"" + esc(bc->searchEngine()) +
            L"\",\"homePage\":\"" + esc(bc->homePage()) +
            L"\",\"downloadPath\":\"" + esc(bc->downloadPath()) +
            L"\",\"downloadFolder\":\"" + esc(bc->effectiveDownloadPath()) +
            L"\",\"startup\":" + std::to_wstring(bc->startupBehavior()) +
            L",\"adblock\":" + std::wstring(bc->isAdBlockEnabled() ? L"true" : L"false") +
            L",\"clearOnExit\":" + std::wstring(bc->clearOnExit() ? L"true" : L"false");
        std::wstring focus = bc->takeSettingsFocus();
        if (!focus.empty())
            json += L",\"focus\":\"" + esc(focus) + L"\"";
        json += L"}";
        swprintf(hdr, 48, L"__ancore__@%d@settings", ep);
        postToPanel(std::wstring(hdr) + json);
    } else if (bc->isHistoryOpen()) {
        swprintf(hdr, 48, L"__ancore__@%d@history", ep);
        postToPanel(hdr);
    } else if (bc->isBookmarksOpen()) {
        std::wstring json = L"[";
        for (size_t i = 0; i < bc->bookmarks().size(); i++) {
            if (i) json += L',';
            json += L"{\"name\":\"";
            for (wchar_t c : bc->bookmarks()[i].name) {
                if (c == L'"' || c == L'\\') json += L'\\';
                json += c;
            }
            json += L"\",\"url\":\"";
            for (wchar_t c : bc->bookmarks()[i].url) {
                if (c == L'"' || c == L'\\') json += L'\\';
                json += c;
            }
            json += L"\"}";
        }
        json += L"]";
        swprintf(hdr, 48, L"__ancore__@%d@bookmarks", ep);
        postToPanel(std::wstring(hdr) + json);
    }
}

LRESULT MainWindow::handleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)wp, RGB(0x0F, 0x0F, 0x0F));
        return (LRESULT)brBg;
    case WM_SIZE:
        resizeViews();
        return 0;
    case WM_DESTROY: {
        ICoreWebView2* wv = activeWv();
        if (wv && BrowserCore::instance()->clearOnExit()) {
            ICoreWebView2_4* wv4 = nullptr;
            if (SUCCEEDED(wv->QueryInterface(IID_ICoreWebView2_4, (void**)&wv4)) && wv4) {
                ICoreWebView2CookieManager* mgr = nullptr;
                if (SUCCEEDED(wv4->get_CookieManager(&mgr)) && mgr) {
                    mgr->DeleteAllCookies();
                    mgr->Release();
                }
                wv4->Release();
            }
            wv->ExecuteScript(
                L"try{localStorage.clear();sessionStorage.clear();}catch(e){}", nullptr);
        }
        PostQuitMessage(0);
        return 0;
    }
    case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT wrc; GetWindowRect(m_hwnd, &wrc);
        int x = pt.x - wrc.left, y = pt.y - wrc.top;
        int bw = wrc.right - wrc.left, bh = wrc.bottom - wrc.top;
        if (IsZoomed(m_hwnd)) return HTCLIENT;
        const int b = 5;
        if (x <= b && y <= b) return HTTOPLEFT;
        if (x >= bw - b && y <= b) return HTTOPRIGHT;
        if (x <= b && y >= bh - b) return HTBOTTOMLEFT;
        if (x >= bw - b && y >= bh - b) return HTBOTTOMRIGHT;
        if (y <= b) return HTTOP;
        if (y >= bh - b) return HTBOTTOM;
        if (x <= b) return HTLEFT;
        if (x >= bw - b) return HTRIGHT;
        return DefWindowProcW(m_hwnd, msg, wp, lp);
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_GETMINMAXINFO: {
        DefWindowProcW(m_hwnd, msg, wp, lp);
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 600;
        mmi->ptMinTrackSize.y = 300;
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(m_hwnd, &pt);
            int top = m_isFullscreen ? 0 : chromeBarHeight();
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            if (pt.y >= top && pt.x >= 0 && pt.x < rc.right) {
                applyWheelZoom((short)GET_WHEEL_DELTA_WPARAM(wp));
                return 0;
            }
        }
        break;
    }
    case WM_HOTKEY: {
        auto bc = BrowserCore::instance();
        switch (wp) {
        case 1: bc->addTab(); return 0;
        case 2: bc->closeTab(bc->currentTab()); return 0;
        case 3: bc->reload(); if (bc->reloadContent) bc->reloadContent(); return 0;
        case 4: bc->switchTab((bc->currentTab() + 1) % bc->tabCount()); return 0;
        case 5: bc->switchTab((bc->currentTab() - 1 + bc->tabCount()) % bc->tabCount()); return 0;
        case 6: toggleFullscreen(); return 0;
        case 7:
            if (m_chromeWv) m_chromeWv->ExecuteScript(L"document.getElementById('urlinp').focus();", nullptr);
            return 0;
        case 8: bc->goBack(); if (bc->goBackContent) bc->goBackContent(); return 0;
        case 9: bc->goForward(); if (bc->goForwardContent) bc->goForwardContent(); return 0;
        case 10: case 13:
            if (bc->zoomContent) bc->zoomContent(0.1);
            return 0;
        case 11: case 14:
            if (bc->zoomContent) bc->zoomContent(-0.1);
            return 0;
        case 12:
            if (bc->zoomContent) bc->zoomContent(-999);
            return 0;
        case 15:
            if (m_chromeWv) m_chromeWv->ExecuteScript(L"doFind(false);", nullptr);
            return 0;
        case 16:
            if (m_chromeWv) m_chromeWv->ExecuteScript(L"doFind(true);", nullptr);
            return 0;
        case 17:
            if (m_chromeWv) m_chromeWv->ExecuteScript(L"showFind();", nullptr);
            return 0;
        }
        return 0;
    }
    default:
        return DefWindowProcW(m_hwnd, msg, wp, lp);
    }
    return 0;
}
