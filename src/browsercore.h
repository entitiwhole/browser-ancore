#pragma once
#include <windows.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <map>
#include <functional>
struct ICoreWebView2;
struct IStream;

struct Bookmark {
    std::wstring name;
    std::wstring url;
};

struct HistoryEntry {
    std::wstring url;
    std::wstring title;
    FILETIME time;
    int hits = 1;
};

struct TabInfo {
    std::wstring title = L"New Tab";
    std::wstring url = L"ancore:newtab";
    std::wstring favicon;
    bool canGoBack = false;
    bool canGoForward = false;
    bool audio = false;
    bool phantom = false;
    bool pinned = false;
};

struct DownloadInfo {
    std::wstring fileName;
    std::wstring path;
    INT64 totalBytes = 0;
    INT64 receivedBytes = 0;
    // COREWEBVIEW2_DOWNLOAD_STATE: 0=in progress, 1=interrupted, 2=completed
    int state = 0;
};

class BrowserCore {
public:
    static BrowserCore* instance();

    void initialize(HINSTANCE hInst);
    void shutdown();
    HINSTANCE hInstance() const { return m_hInst; }

    std::wstring settingsPath() const;

    // Bookmarks
    void addBookmark(const std::wstring& name, const std::wstring& url);
    void removeBookmark(int idx);
    const std::vector<Bookmark>& bookmarks() const { return m_bookmarks; }
    bool isUrlBookmarked(const std::wstring& url) const;
    int bookmarkIndex(const std::wstring& url) const;
    void loadBookmarks();
    void saveBookmarks();

    // History
    void addHistory(const std::wstring& url, const std::wstring& title);
    void updateLastHistoryTitle(const std::wstring& title);
    void clearHistory();
    void loadHistory();
    void saveHistory();
    const std::vector<HistoryEntry>& history() const { return m_history; }

    // Settings
    std::wstring homePage() const { return m_homePage; }
    void setHomePage(const std::wstring& p);
    std::wstring searchEngine() const { return m_searchEngine; }
    void setSearchEngine(const std::wstring& s);
    bool isAdBlockEnabled() const { return m_adBlock; }
    void setAdBlockEnabled(bool b);
    int startupBehavior() const { return m_startupBehavior; }
    void setStartupBehavior(int b);
    std::wstring downloadPath() const { return m_downloadPath; }
    std::wstring effectiveDownloadPath() const;
    void setDownloadPath(const std::wstring& p);
    void requestSettingsFocus(const std::wstring& section) { m_settingsFocusSection = section; }
    std::wstring takeSettingsFocus() {
        std::wstring f = m_settingsFocusSection;
        m_settingsFocusSection.clear();
        return f;
    }
    bool clearOnExit() const { return m_clearOnExit; }
    void setClearOnExit(bool b);

    std::wstring searchUrl(const std::wstring& query) const;
    std::wstring resolveSearchQuery(const std::wstring& query) const;
    std::wstring resolveUrl(const std::wstring& url) const;
    std::wstring newTabUrl() const;
    std::wstring chromeUrl() const;
    std::wstring panelUrl() const;
    std::wstring blockPageUrl() const;
    static std::wstring resourcesDirectory();
    static std::wstring encodeUrlParam(const std::wstring& s);

    bool isSettingsOpen() const { return m_settingsOpen; }
    void setSettingsOpen(bool o) {
        m_settingsOpen = o;
        if (o) { m_historyOpen = false; m_bookmarksOpen = false; m_downloadsOpen = false; }
        sendStateToChrome();
        if (settingsOpenChanged) settingsOpenChanged(o);
        if (panelOpenChanged) panelOpenChanged(m_settingsOpen || m_historyOpen || m_bookmarksOpen);
    }
    bool isHistoryOpen() const { return m_historyOpen; }
    void setHistoryOpen(bool o) {
        m_historyOpen = o;
        if (o) { m_settingsOpen = false; m_bookmarksOpen = false; m_downloadsOpen = false; }
        sendStateToChrome();
        if (historyOpenChanged) historyOpenChanged(o);
        if (panelOpenChanged) panelOpenChanged(m_settingsOpen || m_historyOpen || m_bookmarksOpen);
    }
    bool isBookmarksOpen() const { return m_bookmarksOpen; }
    void setBookmarksOpen(bool o) {
        m_bookmarksOpen = o;
        if (o) { m_settingsOpen = false; m_historyOpen = false; m_downloadsOpen = false; }
        sendStateToChrome();
        if (bookmarksOpenChanged) bookmarksOpenChanged(o);
        if (panelOpenChanged) panelOpenChanged(m_settingsOpen || m_historyOpen || m_bookmarksOpen);
    }
    int securityState() const { return m_securityState; }
    void setSecurityState(int s) { m_securityState = s; sendStateToChrome(); }
    void setStatusText(const std::wstring& t) { m_statusText = t; sendStateToChrome(); }

    // ── Tab management (like Chromium's TabStripModel) ──
    int  tabCount() const { return (int)m_tabs.size(); }
    int  currentTab() const { return m_currentTab; }
    bool isLoading() const { return m_isLoading; }
    const TabInfo& tab(int idx) const { return m_tabs[idx]; }
    const std::vector<TabInfo>& tabs() const { return m_tabs; }

    void addTab(const wchar_t* url = nullptr);
    void addTabBackground(const wchar_t* url);
    void switchTab(int idx);
    void closeTab(int idx);
    void closeOtherTabs(int keepIdx);
    void duplicateTab(int idx);
    void restoreClosedTab();
    void copyTabUrl(int idx);
    void navigateTo(const wchar_t* url);
    void goBack();
    void goForward();
    void reload();
    void stop();
    void goHome();

    // Update tab state from WebView2 navigation events
    void setTabUrl(int idx, const std::wstring& url);
    void setTabTitle(int idx, const std::wstring& title);
    void setTabFavicon(int idx, const std::wstring& favicon);
    void setTabAudio(int idx, bool playing);
    void setTabPhantom(int idx, bool phantom) { if (idx >= 0 && idx < (int)m_tabs.size()) { m_tabs[idx].phantom = phantom; sendStateToChrome(); } }
    void setTabPinned(int idx, bool pinned) { if (idx >= 0 && idx < (int)m_tabs.size()) { m_tabs[idx].pinned = pinned; sendStateToChrome(); } }
    void setLoading(bool loading) { m_isLoading = loading; }
    void setNavButtons(bool back, bool fwd) { m_canGoBack = back; m_canGoForward = fwd; }

    // ── Downloads ──
    void addDownload(const DownloadInfo& dl);
    void updateDownload(int idx, INT64 received, int state) {
        if (idx >= 0 && idx < (int)m_downloads.size()) {
            if (received >= 0) m_downloads[idx].receivedBytes = received;
            if (state >= 0) m_downloads[idx].state = state;
            sendStateToChrome();
        }
    }
    void clearDownloads();
    void cancelDownload(int idx);

    void nextTab();
    void prevTab();

    static std::wstring hostFromUrl(const std::wstring& url);
    static std::wstring homepageUrl(const std::wstring& url);
    double siteZoomForUrl(const std::wstring& url) const;
    void saveSiteZoomForUrl(const std::wstring& url, double factor);

    int importBookmarksFromFile(HWND owner);
    std::wstring topSitesJson(int limit = 8) const;
    std::wstring historyPickJson(int limit = 60) const;

    struct NewTabWidget {
        std::wstring title;
        std::wstring url;
        std::wstring favicon;
    };
    const std::vector<NewTabWidget>& newTabWidgets() const { return m_newTabWidgets; }
    void setNewTabWidgets(std::vector<NewTabWidget> widgets);
    std::wstring newTabDataJson() const;
    void loadNewTabWidgets();
    void saveNewTabWidgets();
    void setNewTabWidgetsFromJson(const std::wstring& json);
    std::wstring faviconForHost(const std::wstring& host) const;
    void cacheFaviconStreamForHost(const std::wstring& host, IStream* stream);
    void setDownloadsPanelOpen(bool open);
    bool isDownloadsOpen() const { return m_downloadsOpen; }
    int chromeBarHeightPx() const { return m_chromeBarPx; }
    int chromeWndHeightPx() const { return m_chromeWndPx; }
    void setChromeLayoutPx(int barPx, int totalPx);
    std::wstring downloadsPanelJson() const;
    const std::vector<DownloadInfo>& downloads() const { return m_downloads; }

    // ── IPC: messages from chrome and content HTML ──
    void handleChromeMessage(const std::wstring& msg);
    void handleContentMessage(const std::wstring& msg, ICoreWebView2* sender);

    // ── Navigation callbacks (set by MainWindow) ──
    std::function<void(const std::wstring&)> navigateContent;
    std::function<void(int)> switchTabContent;
    std::function<void(int, const std::wstring&)> addTabContent;
    std::function<void(int)> removeTabContent;
    std::function<void(int, int)> reorderTabContent;
    std::function<void()> goBackContent;
    std::function<void()> goForwardContent;
    std::function<void()> reloadContent;
    std::function<void(int)> reloadTabContent;
    std::function<void()> stopContent;
    std::function<void(double)> zoomContent;
    std::function<void(double)> setZoomContent;
    std::function<void(int)> cancelDownloadContent;
    std::function<void()> clearDownloadOpsContent;

    // ── Find in page (set by MainWindow) ──
    std::function<void(const std::wstring& text, bool backward)> findContent;
    std::function<void()> findClearContent;

    // ── DevTools (set by MainWindow) ──
    std::function<void()> devtoolsContent;

    // ── Expanded UI callbacks (set by MainWindow) ──
    std::function<void(bool)> settingsOpenChanged;
    std::function<void(bool)> historyOpenChanged;
    std::function<void(bool)> bookmarksOpenChanged;
    std::function<void(bool)> panelOpenChanged;
    std::function<void(const std::wstring&, ICoreWebView2*)> contextMenuAction;

    // ── Session restore ──
    void saveSession();
    void loadSession();
    bool hasSessionFile() const;

    // ── UI state callbacks ──
    std::function<bool()> getFullscreen;
    std::function<double()> getZoom;
    std::function<void()> toggleFullscreen;
    std::function<void()> dlPopoverChanged;

    // ── AdBlock ──
    void loadAdBlockFilters();
    bool isUrlBlocked(const std::wstring& url) const;
    const std::vector<std::wstring>& adBlockFilters() const { return m_adBlockFilters; }
    void reloadAdBlock();

    // ── Omnibox suggestions ──
    void sendUrlSuggestions(const std::wstring& query);

    // ── IPC: send state to chrome HTML (called from main thread) ──
    void sendStateToChrome();

    // Function pointer for delivering messages to chrome WebView2
    std::function<void(const std::wstring&)> postToChrome;

    // Main window handle
    void setMainWindow(HWND hwnd) { m_mainWnd = hwnd; }
    HWND mainWindow() const { return m_mainWnd; }

private:
    BrowserCore() = default;
    void loadSettings();
    void saveSettings();
    void loadSiteZoom();
    void saveSiteZoom();
    void loadHostFavicons();
    void saveHostFavicons();
    void pruneSmallFaviconCache();
    void closeAllPanels();
    void openPanelView(const wchar_t* view);

    HINSTANCE m_hInst = nullptr;
    HWND m_mainWnd = nullptr;
    std::vector<Bookmark> m_bookmarks;
    std::vector<HistoryEntry> m_history;
    std::wstring m_homePage = L"ancore:newtab";
    std::wstring m_searchEngine = L"google";
    std::wstring m_downloadPath;
    std::wstring m_settingsFocusSection;
    bool m_clearOnExit = false;
    bool m_adBlock = false;
    int m_startupBehavior = 0; // 0=newtab, 1=continue, 2=specific
    bool m_settingsOpen = false;
    bool m_historyOpen = false;
    bool m_bookmarksOpen = false;
    int m_securityState = 0; // 0=unknown, 1=secure(HTTPS), 2=insecure(HTTP)
    std::vector<std::wstring> m_adBlockFilters; // adblock URL patterns

    // ── Status bar text ──
    std::wstring m_statusText;

    // ── Downloads ──
    std::vector<DownloadInfo> m_downloads;

    std::map<std::wstring, double> m_siteZoom;
    std::map<std::wstring, std::wstring> m_hostFavicons;
    std::vector<NewTabWidget> m_newTabWidgets;

    bool m_downloadsOpen = false;
    int m_chromeBarPx = 0;
    int m_chromeWndPx = 0;

    struct ClosedTabEntry {
        std::wstring url;
        std::wstring title;
        bool pinned = false;
    };

    // ── Tab state (like Chromium's TabStripModel) ──
    std::vector<ClosedTabEntry> m_closedTabs;
    std::vector<TabInfo> m_tabs;
    int  m_currentTab = -1;
    bool m_isLoading = false;
    bool m_canGoBack = false;
    bool m_canGoForward = false;
};
