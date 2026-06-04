#include "browsercore.h"
#include <objidl.h>
#include <vector>
#include <shlobj.h>
#include <shellapi.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cctype>
#include <cmath>
#include <algorithm>
#include <map>
#include <commdlg.h>
#include <WebView2.h>

#pragma comment(lib, "shell32.lib")
static std::wstring fileUrl(const std::wstring& f);

static BrowserCore* s_instance = nullptr;

BrowserCore* BrowserCore::instance() {
    if (!s_instance) s_instance = new BrowserCore();
    return s_instance;
}

void BrowserCore::initialize(HINSTANCE hInst) {
    m_hInst = hInst;
    loadBookmarks();
    loadHistory();
    loadSettings();
    loadSiteZoom();
    loadNewTabWidgets();
    loadHostFavicons();
    pruneSmallFaviconCache();
    if (m_adBlock) loadAdBlockFilters();
    if (m_startupBehavior == 1 && hasSessionFile()) {
        loadSession();
    } else {
        addTab();
        if (m_startupBehavior == 2) {
            std::wstring home = resolveUrl(m_homePage.c_str());
            if (!m_tabs.empty()) {
                m_tabs[0].url = home;
                m_tabs[0].title = L"Loading...";
                m_tabs[0].phantom = (home.find(L"newtab.html") != std::wstring::npos);
            }
        }
    }
    if (m_tabs.empty())
        addTab();
    if (m_tabs.size() == 1 && !m_tabs[0].phantom &&
        m_tabs[0].url.find(L"newtab.html") != std::wstring::npos)
        m_tabs[0].phantom = true;
}

void BrowserCore::shutdown() {
    saveBookmarks();
    saveHistory();
    saveSettings();
    saveSiteZoom();
    saveNewTabWidgets();
    saveHostFavicons();
    saveSession();
}

std::wstring BrowserCore::settingsPath() const {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        wcscat_s(path, L"\\AnCoreBrowser");
        CreateDirectoryW(path, nullptr);
        return path;
    }
    return L"";
}

// ── Bookmarks ──────────────────────────────────────────────────
void BrowserCore::loadBookmarks() {
    m_bookmarks.clear();
    std::wstring p = settingsPath() + L"\\bookmarks.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"r,ccs=UTF-8") == 0 && f) {
        wchar_t line[4096];
        while (fgetws(line, 4096, f)) {
            wchar_t* sep = wcschr(line, L'|');
            if (sep) {
                *sep = 0;
                m_bookmarks.push_back({line, sep + 1});
                auto& u = m_bookmarks.back().url;
                while (!u.empty() && (u.back() == L'\n' || u.back() == L'\r')) u.pop_back();
            }
        }
        fclose(f);
    }
}

void BrowserCore::saveBookmarks() {
    std::wstring p = settingsPath() + L"\\bookmarks.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"w,ccs=UTF-8") == 0 && f) {
        for (auto& b : m_bookmarks)
            fwprintf(f, L"%s|%s\n", b.name.c_str(), b.url.c_str());
        fclose(f);
    }
}

void BrowserCore::addBookmark(const std::wstring& name, const std::wstring& url) {
    if (url.empty()) return;
    for (auto& b : m_bookmarks)
        if (b.url == url) return;
    m_bookmarks.push_back({name, url});
    saveBookmarks();
    sendStateToChrome();
}

void BrowserCore::removeBookmark(int idx) {
    if (idx >= 0 && idx < (int)m_bookmarks.size()) {
        m_bookmarks.erase(m_bookmarks.begin() + idx);
        saveBookmarks();
        sendStateToChrome();
        if (m_bookmarksOpen && panelOpenChanged)
            panelOpenChanged(true);
    }
}

bool BrowserCore::isUrlBookmarked(const std::wstring& url) const {
    for (auto& b : m_bookmarks)
        if (b.url == url) return true;
    return false;
}

int BrowserCore::bookmarkIndex(const std::wstring& url) const {
    for (size_t i = 0; i < m_bookmarks.size(); i++)
        if (m_bookmarks[i].url == url) return (int)i;
    return -1;
}

// ── History ────────────────────────────────────────────────────
static bool isSearchEngineUrl(const std::wstring& url) {
    return url.find(L"google.com/search") != std::wstring::npos ||
           url.find(L"yandex.ru/search") != std::wstring::npos ||
           url.find(L"yandex.com/search") != std::wstring::npos ||
           url.find(L"bing.com/search") != std::wstring::npos ||
           (url.find(L"duckduckgo.com/") != std::wstring::npos &&
            (url.find(L"q=") != std::wstring::npos || url.find(L"query=") != std::wstring::npos));
}

static std::wstring trimWs(std::wstring s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.pop_back();
    return s;
}

static std::wstring toLowerWs(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) c = (wchar_t)towlower(c);
    return r;
}

static std::wstring urlDecodeQueryParam(const std::wstring& enc) {
    std::wstring o;
    for (size_t i = 0; i < enc.size(); i++) {
        if (enc[i] == L'%' && i + 2 < enc.size()) {
            wchar_t hex[3] = { enc[i + 1], enc[i + 2], 0 };
            wchar_t* end = nullptr;
            int v = wcstol(hex, &end, 16);
            if (v >= 0) { o += (wchar_t)v; i += 2; continue; }
        }
        if (enc[i] == L'+') o += L' ';
        else o += enc[i];
    }
    return o;
}

static std::wstring searchQueryFromUrl(const std::wstring& url) {
    auto extract = [&](const wchar_t* key) -> std::wstring {
        std::wstring needle = key;
        size_t pos = url.find(needle);
        if (pos == std::wstring::npos) return L"";
        pos += needle.size();
        size_t end = url.find(L'&', pos);
        return urlDecodeQueryParam(url.substr(pos, end == std::wstring::npos ? url.size() - pos : end - pos));
    };
    std::wstring q = extract(L"q=");
    if (!q.empty()) return q;
    q = extract(L"text=");
    if (!q.empty()) return q;
    return extract(L"query=");
}

static bool looksLikeNavigableUrl(const std::wstring& q) {
    if (q.empty()) return false;
    if (q.find(L' ') != std::wstring::npos) return false;
    if (q.find(L"://") != std::wstring::npos) return true;
    if (q.find(L'.') != std::wstring::npos) return true;
    if (q.find(L"localhost") != std::wstring::npos) return true;
    return false;
}

void BrowserCore::addHistory(const std::wstring& url, const std::wstring& title) {
    if (url.empty() || url.find(L"ancore:") == 0 || url.find(L"file:///") == 0) return;
    std::wstring entryTitle = title;
    if (isSearchEngineUrl(url)) {
        std::wstring q = searchQueryFromUrl(url);
        if (!q.empty()) entryTitle = q;
    }
    for (int i = (int)m_history.size() - 1; i >= 0; i--) {
        if (m_history[i].url == url) {
            if (!entryTitle.empty()) m_history[i].title = entryTitle;
            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            m_history[i].time = ft;
            m_history[i].hits++;
            HistoryEntry e = m_history[i];
            m_history.erase(m_history.begin() + i);
            m_history.push_back(e);
            return;
        }
    }
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    m_history.push_back({url, entryTitle, ft, 1});
    if (m_history.size() > 1000) m_history.erase(m_history.begin());
}

void BrowserCore::updateLastHistoryTitle(const std::wstring& title) {
    if (m_history.empty()) return;
    if (isSearchEngineUrl(m_history.back().url)) return;
    if (m_history.back().title.empty())
        m_history.back().title = title;
}

void BrowserCore::clearHistory() {
    m_history.clear();
    // Delete history file too
    std::wstring p = settingsPath() + L"\\history.txt";
    DeleteFileW(p.c_str());
    sendStateToChrome();
}

void BrowserCore::loadHistory() {
    m_history.clear();
    std::wstring p = settingsPath() + L"\\history.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"r,ccs=UTF-8") == 0 && f) {
        wchar_t line[8192];
        while (fgetws(line, 8192, f)) {
            wchar_t* sep1 = wcschr(line, L'|');
            if (!sep1) continue;
            *sep1 = 0;
            std::wstring url = line;
            wchar_t* sep2 = wcschr(sep1 + 1, L'|');
            if (!sep2) continue;
            *sep2 = 0;
            std::wstring title = sep1 + 1;
            FILETIME ft;
            ft.dwLowDateTime = _wtoi(sep2 + 1);
            wchar_t* sep3 = wcschr(sep2 + 1, L'|');
            if (sep3) {
                *sep3 = 0;
                ft.dwHighDateTime = _wtoi(sep3 + 1);
            } else {
                ft.dwHighDateTime = 0;
            }
            int hits = 1;
            if (sep3) {
                wchar_t* sep4 = wcschr(sep3 + 1, L'|');
                if (sep4) {
                    hits = _wtoi(sep4 + 1);
                    if (hits < 1) hits = 1;
                }
            }
            // Remove trailing newline from title
            while (!title.empty() && (title.back() == L'\n' || title.back() == L'\r')) title.pop_back();
            m_history.push_back({url, title, ft, hits});
        }
        fclose(f);
    }
}

void BrowserCore::saveHistory() {
    std::wstring p = settingsPath() + L"\\history.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"w,ccs=UTF-8") == 0 && f) {
        for (auto& h : m_history) {
            // Escape pipe characters in URL and title
            std::wstring url = h.url;
            for (size_t i = 0; i < url.size(); i++) if (url[i] == L'|') url[i] = L' ';
            std::wstring title = h.title;
            for (size_t i = 0; i < title.size(); i++) if (title[i] == L'|') title[i] = L' ';
            int hits = h.hits > 0 ? h.hits : 1;
            fwprintf(f, L"%s|%s|%lu|%lu|%d\n",
                url.c_str(), title.c_str(),
                h.time.dwLowDateTime, h.time.dwHighDateTime, hits);
        }
        fclose(f);
    }
}

// ── Settings ───────────────────────────────────────────────────
void BrowserCore::loadSettings() {
    std::wstring p = settingsPath() + L"\\settings.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"r,ccs=UTF-8") == 0 && f) {
        wchar_t line[4096];
        while (fgetws(line, 4096, f)) {
            wchar_t* eq = wcschr(line, L'=');
            if (!eq) continue;
            *eq = 0;
            std::wstring key = line;
            std::wstring val = eq + 1;
            while (!val.empty() && (val.back() == L'\n' || val.back() == L'\r')) val.pop_back();
            if (key == L"homepage") m_homePage = val;
            else if (key == L"search") m_searchEngine = val;
            else if (key == L"adblock") m_adBlock = (val == L"1");
            else if (key == L"startup") m_startupBehavior = _wtoi(val.c_str());
            else if (key == L"downloadpath") m_downloadPath = val;
            else if (key == L"clearonexit") m_clearOnExit = (val == L"1");
        }
        fclose(f);
    }
}

void BrowserCore::saveSettings() {
    std::wstring p = settingsPath() + L"\\settings.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"w,ccs=UTF-8") == 0 && f) {
        fwprintf(f, L"homepage=%s\n", m_homePage.c_str());
        fwprintf(f, L"search=%s\n", m_searchEngine.c_str());
        fwprintf(f, L"adblock=%d\n", m_adBlock ? 1 : 0);
        fwprintf(f, L"startup=%d\n", m_startupBehavior);
        fwprintf(f, L"downloadpath=%s\n", m_downloadPath.c_str());
        fwprintf(f, L"clearonexit=%d\n", m_clearOnExit ? 1 : 0);
        fclose(f);
    }
}

void BrowserCore::setHomePage(const std::wstring& p) { m_homePage = p; saveSettings(); sendStateToChrome(); }
void BrowserCore::setSearchEngine(const std::wstring& s) { m_searchEngine = s; saveSettings(); sendStateToChrome(); }
void BrowserCore::setAdBlockEnabled(bool b) {
    m_adBlock = b; saveSettings();
    if (b) loadAdBlockFilters();
    sendStateToChrome();
}

// ── AdBlock ────────────────────────────────────────────────────
struct AdRule {
    std::wstring pattern;
    bool isDomain; // true == domain match, false == substring
};

static std::vector<AdRule> g_adRules;

void BrowserCore::loadAdBlockFilters() {
    m_adBlockFilters.clear();
    g_adRules.clear();
    std::wstring p = settingsPath() + L"\\adblock.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"r,ccs=UTF-8") == 0 && f) {
        wchar_t line[4096];
        while (fgetws(line, 4096, f)) {
            size_t len = wcslen(line);
            while (len > 0 && (line[len-1] == L'\n' || line[len-1] == L'\r')) line[--len] = 0;
            if (len == 0 || line[0] == L'!' || line[0] == L'[') continue;
            std::wstring s = line;
            m_adBlockFilters.push_back(s);
            AdRule r;
            if (s.find(L"://") != std::wstring::npos) {
                // full URL pattern — extract domain
                size_t pos = s.find(L"://");
                if (pos != std::wstring::npos) {
                    std::wstring domain = s.substr(pos + 3);
                    pos = domain.find(L'/');
                    if (pos != std::wstring::npos) domain = domain.substr(0, pos);
                    r.pattern = domain;
                    r.isDomain = true;
                } else {
                    r.pattern = s;
                    r.isDomain = false;
                }
            } else if (s.find(L"||") == 0) {
                // domain name filter (EasyList format: ||domain^)
                r.pattern = s.substr(2);
                size_t end = r.pattern.find(L'^');
                if (end != std::wstring::npos) r.pattern = r.pattern.substr(0, end);
                r.isDomain = true;
            } else {
                r.pattern = s;
                r.isDomain = false;
            }
            if (!r.pattern.empty()) g_adRules.push_back(r);
        }
        fclose(f);
    }
    // Fallback built-in rules if file is empty/unavailable
    if (g_adRules.empty()) {
        static const wchar_t* builtin[] = {
            L"doubleclick.net", L"googlesyndication.com", L"googleadservices.com",
            L"googletagservices.com", L"googletagmanager.com", L"adzerk.net",
            L"exponential.com", L"criteo.com", L"criteo.net", L"casalemedia.com",
            L"adsrvr.org", L"adnxs.com", L"rubiconproject.com", L"pubmatic.com",
            L"openx.net", L"bidswitch.net", L"agkn.com", L"amazon-adsystem.com",
            L"adservice.google.com", L"pagead2.googlesyndication.com",
            L"ad.doubleclick.net", L"securepubads.g.doubleclick.net",
            L"tpc.googlesyndication.com", L"partner.googleadservices.com",
            L"analytics.google.com", L"www.google-analytics.com",
            L"ssl.google-analytics.com", L"googleadservices.com",
            L"yandex.ru/ads", L"an.yandex.ru", L"mc.yandex.ru",
            L"moikrug.ru/ads", L"vk.com/ads", L"facebook.com/ads",
            L"connect.facebook.net", L"www.facebook.com/tr",
            L"cdn.viglink.com", L"adtago.s3.amazonaws.com",
            L"adroll.com", L"adsymptotic.com", L"scorecardresearch.com",
            L"quantserve.com", L"exelator.com", L"bluekai.com",
            L"demdex.net", L"adsafeprotected.com", L"burstnet.com",
            L"contextweb.com", L"contextu.al", L"kontera.com",
            // Russian-specific
            L"yandex.ru/clck", L"yabs.yandex.ru", L"yastatic.net/ads",
            L"ads.adfox.ru", L"an.yandex.ru", L"ad.csdnav.com",
        };
        for (auto* rule : builtin) {
            m_adBlockFilters.push_back(rule);
            g_adRules.push_back({rule, true});
        }
    }
}

bool BrowserCore::isUrlBlocked(const std::wstring& url) const {
    if (!m_adBlock || g_adRules.empty()) return false;
    for (auto& r : g_adRules) {
        if (r.isDomain) {
            // Check if URL contains the domain
            if (url.find(r.pattern) != std::wstring::npos) return true;
        } else {
            if (url.find(r.pattern) != std::wstring::npos) return true;
        }
    }
    return false;
}

void BrowserCore::reloadAdBlock() {
    g_adRules.clear();
    if (m_adBlock) loadAdBlockFilters();
}
void BrowserCore::setClearOnExit(bool b) { m_clearOnExit = b; saveSettings(); sendStateToChrome(); }
void BrowserCore::setStartupBehavior(int b) { m_startupBehavior = b; saveSettings(); sendStateToChrome(); }
void BrowserCore::setDownloadPath(const std::wstring& p) { m_downloadPath = p; saveSettings(); sendStateToChrome(); }

std::wstring BrowserCore::effectiveDownloadPath() const {
    if (!m_downloadPath.empty()) return m_downloadPath;
    wchar_t* path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path))) {
        std::wstring r = path;
        CoTaskMemFree(path);
        return r;
    }
    return L"";
}

static std::wstring urlEncode(const std::wstring& s) {
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return s;
    std::string utf8(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, utf8.data(), len, nullptr, nullptr);
    std::wstring enc;
    for (char c : utf8) {
        if (c == '\0') break;
        unsigned char uc = (unsigned char)c;
        if (isalnum(uc) || uc == '-' || uc == '_' || uc == '.' || uc == '~')
            enc += (wchar_t)uc;
        else {
            wchar_t buf[4];
            swprintf(buf, 4, L"%%%02X", uc);
            enc += buf;
        }
    }
    return enc;
}

std::wstring BrowserCore::encodeUrlParam(const std::wstring& s) {
    return urlEncode(s);
}

std::wstring BrowserCore::searchUrl(const std::wstring& query) const {
    if (m_searchEngine == L"yandex")
        return L"https://yandex.ru/search/?text=" + urlEncode(query);
    if (m_searchEngine == L"bing")
        return L"https://www.bing.com/search?q=" + urlEncode(query);
    if (m_searchEngine == L"duckduckgo")
        return L"https://duckduckgo.com/?q=" + urlEncode(query);
    return L"https://www.google.com/search?q=" + urlEncode(query);
}

std::wstring BrowserCore::resolveSearchQuery(const std::wstring& query) const {
    if (query.find(L'.') != std::wstring::npos && query.find(L' ') == std::wstring::npos) {
        if (query.find(L"://") == std::wstring::npos)
            return L"https://" + query;
        return query;
    }
    return searchUrl(query);
}

std::wstring BrowserCore::resolveUrl(const std::wstring& url) const {
    if (url == L"ancore:newtab") return newTabUrl();
    if (url == L"ancore:history") return fileUrl(L"history.html");
    if (url.find(L"://") != std::wstring::npos) return url;
    if (url.find(L'.') != std::wstring::npos && url.find(L' ') == std::wstring::npos)
        return L"https://" + url;
    return resolveSearchQuery(url);
}

static std::wstring resourcesDir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    wchar_t* s = wcsrchr(p, L'\\');
    if (s) *s = 0;
    auto check = [](const std::wstring& d) -> std::wstring {
        DWORD a = GetFileAttributesW(d.c_str());
        return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) ? d : L"";
    };
    std::wstring r;
    r = check(std::wstring(p) + L"\\resources"); if (!r.empty()) return r;
    r = check(std::wstring(p) + L"\\..\\..\\resources"); if (!r.empty()) return r;
#ifdef ANCORE_RESOURCES_DIR
    r = check(std::wstring(L"" ANCORE_RESOURCES_DIR));
    if (!r.empty()) return r;
#endif
    return L"";
}

std::wstring BrowserCore::resourcesDirectory() {
    return resourcesDir();
}

static std::wstring fileUrl(const std::wstring& f) {
    std::wstring d = resourcesDir();
    if (d.empty()) return L"about:blank";
    for (auto& c : d) if (c == L'\\') c = L'/';
    return L"file:///" + d + L"/html/" + f;
}

std::wstring BrowserCore::newTabUrl() const {
    return fileUrl(L"newtab.html");
}

std::wstring BrowserCore::chromeUrl() const {
    return fileUrl(L"chrome.html");
}

std::wstring BrowserCore::panelUrl() const {
    return fileUrl(L"panel.html");
}

std::wstring BrowserCore::blockPageUrl() const {
    return fileUrl(L"blockpage.html");
}

// ── Tab management (like Chromium's TabStripModel) ──────────────
static bool isRestorableTabUrl(const std::wstring& url) {
    if (url.empty()) return false;
    if (url.find(L"ancore:") == 0) return false;
    if (url.find(L"newtab.html") != std::wstring::npos) return false;
    return true;
}

static void copyToClipboard(HWND hwnd, const std::wstring& text) {
    if (text.empty() || !OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem) {
        void* p = GlobalLock(mem);
        if (p) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

void BrowserCore::addTab(const wchar_t* url) {
    size_t idx = m_tabs.size();
    m_tabs.push_back({});
    m_tabs.back().url = resolveUrl(url ? url : L"ancore:newtab");
    m_tabs.back().phantom = (!url || std::wstring(url) == L"ancore:newtab");
    switchTab((int)idx);
    saveSession();
}

void BrowserCore::addTabBackground(const wchar_t* url) {
    if (!url || !*url) return;
    const int prev = m_currentTab;
    const size_t idx = m_tabs.size();
    m_tabs.push_back({});
    m_tabs.back().url = resolveUrl(url);
    m_tabs.back().title = L"Loading...";
    m_tabs.back().phantom = false;
    if (addTabContent) addTabContent((int)idx, m_tabs.back().url.c_str());
    if (prev >= 0 && prev < (int)m_tabs.size())
        m_currentTab = prev;
    sendStateToChrome();
    saveSession();
}

void BrowserCore::switchTab(int idx) {
    if (idx < 0 || idx >= (int)m_tabs.size()) return;
    m_currentTab = idx;
    if (switchTabContent) switchTabContent(idx);
    sendStateToChrome();
}

void BrowserCore::closeTab(int idx) {
    if (idx < 0 || idx >= (int)m_tabs.size()) return;
    const TabInfo& closing = m_tabs[idx];
    if (!closing.phantom && isRestorableTabUrl(closing.url)) {
        m_closedTabs.push_back({closing.url, closing.title, closing.pinned});
        if (m_closedTabs.size() > 25)
            m_closedTabs.erase(m_closedTabs.begin());
    }
    if (removeTabContent) removeTabContent(idx);
    m_tabs.erase(m_tabs.begin() + idx);
    if (m_tabs.empty()) {
        addTab();
        if (!m_tabs.empty()) m_tabs[0].phantom = true;
    } else {
        switchTab(std::min<int>(idx, (int)m_tabs.size() - 1));
    }
    saveSession();
}

void BrowserCore::closeOtherTabs(int keepIdx) {
    if (keepIdx < 0 || keepIdx >= (int)m_tabs.size()) return;
    for (int i = (int)m_tabs.size() - 1; i >= 0; i--) {
        if (i != keepIdx) closeTab(i);
    }
}

void BrowserCore::duplicateTab(int idx) {
    if (idx < 0 || idx >= (int)m_tabs.size()) return;
    TabInfo copy = m_tabs[idx];
    copy.phantom = false;
    const size_t newIdx = m_tabs.size();
    m_tabs.push_back(copy);
    switchTab((int)newIdx);
    saveSession();
}

void BrowserCore::restoreClosedTab() {
    if (m_closedTabs.empty()) return;
    ClosedTabEntry e = m_closedTabs.back();
    m_closedTabs.pop_back();
    addTab(e.url.c_str());
    const int idx = m_currentTab;
    if (idx >= 0 && idx < (int)m_tabs.size()) {
        m_tabs[idx].title = e.title.empty() ? L"Loading..." : e.title;
        m_tabs[idx].pinned = e.pinned;
        sendStateToChrome();
    }
}

void BrowserCore::copyTabUrl(int idx) {
    if (idx < 0 || idx >= (int)m_tabs.size()) return;
    const std::wstring& u = m_tabs[idx].url;
    if (u.empty() || u.find(L"ancore:") == 0) return;
    copyToClipboard(mainWindow(), u);
    setStatusText(L"Адрес скопирован");
}

void BrowserCore::addDownload(const DownloadInfo& dl) {
    m_downloads.push_back(dl);
    m_statusText = L"Загрузка: " + dl.fileName;
    sendStateToChrome();
}

void BrowserCore::clearDownloads() {
    m_downloads.clear();
    if (clearDownloadOpsContent) clearDownloadOpsContent();
    sendStateToChrome();
}

void BrowserCore::cancelDownload(int idx) {
    if (cancelDownloadContent) cancelDownloadContent(idx);
}

void BrowserCore::nextTab() {
    if (m_tabs.empty()) return;
    int n = m_currentTab + 1;
    if (n >= (int)m_tabs.size()) n = 0;
    switchTab(n);
}

void BrowserCore::prevTab() {
    if (m_tabs.empty()) return;
    int n = m_currentTab - 1;
    if (n < 0) n = (int)m_tabs.size() - 1;
    switchTab(n);
}

std::wstring BrowserCore::hostFromUrl(const std::wstring& url) {
    if (url.empty()) return {};
    size_t start = 0;
    if (url.find(L"https://") == 0) start = 8;
    else if (url.find(L"http://") == 0) start = 7;
    else if (url.find(L"file:///") == 0) return {};
    size_t end = url.find(L'/', start);
    if (end == std::wstring::npos) end = url.size();
    std::wstring host = url.substr(start, end - start);
    size_t port = host.find(L':');
    if (port != std::wstring::npos) host = host.substr(0, port);
    if (host.size() > 2 && host.front() == L'w' && host.find(L"www.") == 0)
        host = host.substr(4);
    return host;
}

std::wstring BrowserCore::homepageUrl(const std::wstring& url) {
    if (url.empty()) return url;
    std::wstring host = hostFromUrl(url);
    if (host.empty()) return url;
    std::wstring scheme = L"https://";
    if (url.size() >= 7 && url.compare(0, 7, L"http://") == 0)
        scheme = L"http://";
    return scheme + host + L"/";
}

void BrowserCore::loadSiteZoom() {
    m_siteZoom.clear();
    std::wstring p = settingsPath() + L"\\zoom.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"r,ccs=UTF-8") == 0 && f) {
        wchar_t line[1024];
        while (fgetws(line, 1024, f)) {
            wchar_t* eq = wcschr(line, L'=');
            if (!eq) continue;
            *eq = 0;
            std::wstring host = line;
            std::wstring val = eq + 1;
            while (!host.empty() && (host.back() == L'\n' || host.back() == L'\r')) host.pop_back();
            while (!val.empty() && (val.back() == L'\n' || val.back() == L'\r')) val.pop_back();
            if (!host.empty()) {
                double z = _wtof(val.c_str());
                if (z >= 0.3 && z <= 3.0) m_siteZoom[host] = z;
            }
        }
        fclose(f);
    }
}

void BrowserCore::saveSiteZoom() {
    std::wstring p = settingsPath() + L"\\zoom.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"w,ccs=UTF-8") == 0 && f) {
        for (auto& kv : m_siteZoom)
            fwprintf(f, L"%s=%.4f\n", kv.first.c_str(), kv.second);
        fclose(f);
    }
}

double BrowserCore::siteZoomForUrl(const std::wstring& url) const {
    std::wstring host = hostFromUrl(url);
    if (host.empty()) return 1.0;
    auto it = m_siteZoom.find(host);
    if (it != m_siteZoom.end()) return it->second;
    return 1.0;
}

void BrowserCore::loadHostFavicons() {
    m_hostFavicons.clear();
    std::wstring p = settingsPath() + L"\\hostfavicons.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"r,ccs=UTF-8") != 0 || !f) return;
    wchar_t line[8192];
    while (fgetws(line, 8192, f)) {
        wchar_t* tab = wcschr(line, L'\t');
        if (!tab) continue;
        *tab = 0;
        std::wstring host = line;
        std::wstring uri = tab + 1;
        while (!host.empty() && (host.back() == L'\n' || host.back() == L'\r')) host.pop_back();
        while (!uri.empty() && (uri.back() == L'\n' || uri.back() == L'\r')) uri.pop_back();
        if (!host.empty() && !uri.empty()) m_hostFavicons[host] = uri;
    }
    fclose(f);
}

void BrowserCore::saveHostFavicons() {
    std::wstring p = settingsPath() + L"\\hostfavicons.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"w,ccs=UTF-8") != 0 || !f) return;
    for (auto& kv : m_hostFavicons)
        fwprintf(f, L"%s\t%s\n", kv.first.c_str(), kv.second.c_str());
    fclose(f);
}

static std::wstring safeHostFileName(const std::wstring& host) {
    std::wstring s;
    for (wchar_t c : host) {
        if (iswalnum(c) || c == L'.' || c == L'-') s += c;
        else s += L'_';
    }
    return s.empty() ? L"site" : s;
}

static std::wstring faviconPngPathForHost(const std::wstring& settingsDir, const std::wstring& host) {
    return settingsDir + L"\\favicons\\" + safeHostFileName(host) + L".png";
}

static std::wstring pathToFileUrl(const std::wstring& path) {
    std::wstring d = path;
    for (auto& c : d)
        if (c == L'\\') c = L'/';
    return L"file:///" + d;
}

static bool pngSizeFromBuffer(const BYTE* data, size_t n, int& w, int& h) {
    w = h = 0;
    if (!data || n < 24) return false;
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (memcmp(data, sig, 8) != 0) return false;
    w = (int)((data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19]);
    h = (int)((data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23]);
    return w > 0 && h > 0;
}

static bool pngSizeFromFile(const std::wstring& path, int& w, int& h) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    unsigned char hdr[24] = {};
    size_t n = fread(hdr, 1, 24, f);
    fclose(f);
    return pngSizeFromBuffer(hdr, n, w, h);
}

static const int kMinCachedFaviconPx = 72;

void BrowserCore::cacheFaviconStreamForHost(const std::wstring& host, IStream* stream) {
    if (host.empty() || !stream) return;
    STATSTG st = {};
    if (FAILED(stream->Stat(&st, STATFLAG_NONAME))) return;
    ULONG cap = st.cbSize.LowPart;
    if (cap < 32 || cap > 512 * 1024) return;
    std::vector<BYTE> buf(cap);
    LARGE_INTEGER zero = {};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    if (FAILED(stream->Read(buf.data(), cap, &read)) || read < 32) return;
    int pw = 0, ph = 0;
    if (!pngSizeFromBuffer(buf.data(), read, pw, ph)) return;
    if (pw < kMinCachedFaviconPx && ph < kMinCachedFaviconPx) return;
    std::wstring dir = settingsPath() + L"\\favicons";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = faviconPngPathForHost(settingsPath(), host);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
    fwrite(buf.data(), 1, read, f);
    fclose(f);
}

std::wstring BrowserCore::faviconForHost(const std::wstring& host) const {
    if (host.empty()) return L"";
    std::wstring png = faviconPngPathForHost(settingsPath(), host);
    if (GetFileAttributesW(png.c_str()) != INVALID_FILE_ATTRIBUTES) {
        int pw = 0, ph = 0;
        if (pngSizeFromFile(png, pw, ph) && pw >= kMinCachedFaviconPx && ph >= kMinCachedFaviconPx)
            return pathToFileUrl(png);
    }
    auto it = m_hostFavicons.find(host);
    if (it != m_hostFavicons.end()) return it->second;
    return L"";
}

void BrowserCore::pruneSmallFaviconCache() {
    std::wstring dir = settingsPath() + L"\\favicons";
    std::wstring pattern = dir + L"\\*.png";
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring path = dir + L"\\" + fd.cFileName;
        int pw = 0, ph = 0;
        if (!pngSizeFromFile(path, pw, ph) || pw < kMinCachedFaviconPx || ph < kMinCachedFaviconPx)
            DeleteFileW(path.c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void BrowserCore::saveSiteZoomForUrl(const std::wstring& url, double factor) {
    std::wstring host = hostFromUrl(url);
    if (host.empty()) return;
    factor = (std::max)(0.3, (std::min)(3.0, factor));
    if (fabs(factor - 1.0) < 0.01)
        m_siteZoom.erase(host);
    else
        m_siteZoom[host] = factor;
    saveSiteZoom();
}

std::wstring BrowserCore::topSitesJson(int limit) const {
    struct Entry { int count = 0; std::wstring url; std::wstring title; };
    std::map<std::wstring, Entry> byHost;
    for (int i = (int)m_history.size() - 1; i >= 0; i--) {
        auto& h = m_history[i];
        if (!isRestorableTabUrl(h.url)) continue;
        std::wstring host = hostFromUrl(h.url);
        if (host.empty()) continue;
        auto& e = byHost[host];
        e.count++;
        if (e.url.empty()) {
            e.url = homepageUrl(h.url);
            e.title = host;
        }
    }
    std::vector<std::pair<std::wstring, Entry>> sorted;
    for (auto& kv : byHost) sorted.push_back(kv);
    std::sort(sorted.begin(), sorted.end(),
        [](auto& a, auto& b) { return a.second.count > b.second.count; });

    auto esc = [](const std::wstring& s) {
        std::wstring o;
        for (wchar_t c : s) {
            if (c == L'"' || c == L'\\') o += L'\\';
            o += c;
        }
        return o;
    };
    std::wstring j = L"[";
    int n = 0;
    for (auto& kv : sorted) {
        if (n >= limit) break;
        if (n) j += L',';
        std::wstring home = homepageUrl(kv.second.url);
        j += L"{\"url\":\"" + esc(home) + L"\",\"title\":\"" + esc(kv.second.title) +
             L"\",\"host\":\"" + esc(kv.first) + L"\"";
        std::wstring fav = faviconForHost(kv.first);
        if (!fav.empty()) j += L",\"favicon\":\"" + esc(fav) + L"\"";
        j += L"}";
        n++;
    }
    j += L"]";
    return j;
}

static std::wstring jsonEscWide(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        if (c == L'"' || c == L'\\') o += L'\\';
        o += c;
    }
    return o;
}

static std::wstring jsonFieldInChunk(const std::wstring& chunk, const wchar_t* key) {
    std::wstring needle = L"\"";
    needle += key;
    needle += L"\":\"";
    size_t pos = chunk.find(needle);
    if (pos == std::wstring::npos) return L"";
    pos += needle.size();
    std::wstring out;
    for (size_t i = pos; i < chunk.size(); i++) {
        wchar_t c = chunk[i];
        if (c == L'\\' && i + 1 < chunk.size()) {
            out += chunk[++i];
            continue;
        }
        if (c == L'"') break;
        out += c;
    }
    return out;
}

std::wstring BrowserCore::historyPickJson(int limit) const {
    struct Entry { std::wstring url; std::wstring title; };
    std::map<std::wstring, Entry> byHost;
    std::vector<std::wstring> order;
    for (int i = (int)m_history.size() - 1; i >= 0; i--) {
        auto& h = m_history[i];
        if (!isRestorableTabUrl(h.url)) continue;
        std::wstring host = hostFromUrl(h.url);
        if (host.empty()) continue;
        if (byHost.count(host)) continue;
        byHost[host] = {homepageUrl(h.url), host};
        order.push_back(host);
        if ((int)order.size() >= limit) break;
    }
    std::wstring j = L"[";
    for (size_t n = 0; n < order.size(); n++) {
        if (n) j += L',';
        auto& e = byHost[order[n]];
        j += L"{\"url\":\"" + jsonEscWide(e.url) + L"\",\"title\":\"" + jsonEscWide(e.title) +
             L"\",\"host\":\"" + jsonEscWide(order[n]) + L"\"";
        std::wstring fav = faviconForHost(order[n]);
        if (!fav.empty()) j += L",\"favicon\":\"" + jsonEscWide(fav) + L"\"";
        j += L"}";
    }
    j += L"]";
    return j;
}

void BrowserCore::loadNewTabWidgets() {
    m_newTabWidgets.clear();
    std::wstring p = settingsPath() + L"\\widgets.json";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"r,ccs=UTF-8") != 0 || !f) return;
    std::wstring json;
    wchar_t buf[4096];
    while (fgetws(buf, 4096, f)) json += buf;
    fclose(f);
    size_t pos = 0;
    while ((pos = json.find(L'{', pos)) != std::wstring::npos) {
        size_t end = json.find(L'}', pos);
        if (end == std::wstring::npos) break;
        std::wstring chunk = json.substr(pos, end - pos + 1);
        std::wstring url = jsonFieldInChunk(chunk, L"url");
        if (!url.empty())
            m_newTabWidgets.push_back({jsonFieldInChunk(chunk, L"title"), url,
                                       jsonFieldInChunk(chunk, L"favicon")});
        pos = end + 1;
    }
}

void BrowserCore::saveNewTabWidgets() {
    std::wstring p = settingsPath() + L"\\widgets.json";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"w,ccs=UTF-8") != 0 || !f) return;
    std::wstring j = L"[";
    for (size_t i = 0; i < m_newTabWidgets.size(); i++) {
        if (i) j += L',';
        std::wstring fav = m_newTabWidgets[i].favicon;
        if (fav.empty()) fav = faviconForHost(hostFromUrl(m_newTabWidgets[i].url));
        j += L"{\"title\":\"" + jsonEscWide(m_newTabWidgets[i].title) +
             L"\",\"url\":\"" + jsonEscWide(m_newTabWidgets[i].url) + L"\"";
        if (!fav.empty()) j += L",\"favicon\":\"" + jsonEscWide(fav) + L"\"";
        j += L"}";
    }
    j += L"]";
    fputws(j.c_str(), f);
    fclose(f);
}

void BrowserCore::setNewTabWidgets(std::vector<NewTabWidget> widgets) {
    for (auto& w : widgets) {
        w.url = homepageUrl(w.url);
        if (w.title.empty())
            w.title = hostFromUrl(w.url);
    }
    m_newTabWidgets = std::move(widgets);
    saveNewTabWidgets();
}

void BrowserCore::setNewTabWidgetsFromJson(const std::wstring& json) {
    std::vector<NewTabWidget> w;
    size_t pos = 0;
    while ((pos = json.find(L'{', pos)) != std::wstring::npos) {
        size_t end = json.find(L'}', pos);
        if (end == std::wstring::npos) break;
        std::wstring chunk = json.substr(pos, end - pos + 1);
        std::wstring url = jsonFieldInChunk(chunk, L"url");
        if (!url.empty())
            w.push_back({jsonFieldInChunk(chunk, L"title"), url,
                         jsonFieldInChunk(chunk, L"favicon")});
        pos = end + 1;
    }
    setNewTabWidgets(std::move(w));
}

std::wstring BrowserCore::newTabDataJson() const {
    std::wstring j = L"{\"widgets\":[";
    for (size_t i = 0; i < m_newTabWidgets.size(); i++) {
        if (i) j += L',';
        std::wstring fav = m_newTabWidgets[i].favicon;
        if (fav.empty()) fav = faviconForHost(hostFromUrl(m_newTabWidgets[i].url));
        std::wstring wurl = homepageUrl(m_newTabWidgets[i].url);
        j += L"{\"title\":\"" + jsonEscWide(m_newTabWidgets[i].title) +
             L"\",\"url\":\"" + jsonEscWide(wurl) + L"\"";
        if (!fav.empty()) j += L",\"favicon\":\"" + jsonEscWide(fav) + L"\"";
        j += L"}";
    }
    j += L"],\"topSites\":";
    j += topSitesJson(8);
    j += L",\"historyPick\":";
    j += historyPickJson(60);
    j += L"}";
    return j;
}

static std::wstring decodeHtmlEntities(const std::wstring& s) {
    std::wstring o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == L'&' && i + 3 < s.size() && s.substr(i, 4) == L"&amp;") {
            o += L'&'; i += 4; continue;
        }
        if (s[i] == L'&' && i + 5 < s.size() && s.substr(i, 6) == L"&quot;") {
            o += L'"'; i += 6; continue;
        }
        o += s[i];
    }
    return o;
}

int BrowserCore::importBookmarksFromFile(HWND owner) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"HTML закладки\0*.html;*.htm\0Все файлы\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Импорт закладок";
    if (!GetOpenFileNameW(&ofn)) return 0;

    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 50 * 1024 * 1024) { fclose(f); return 0; }
    std::string raw((size_t)sz, '\0');
    fread(raw.data(), 1, (size_t)sz, f);
    fclose(f);

    int added = 0;
    std::wstring html;
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        int len = MultiByteToWideChar(CP_UTF8, 0, raw.data() + 3, (int)raw.size() - 3, nullptr, 0);
        if (len > 0) {
            html.resize(len);
            MultiByteToWideChar(CP_UTF8, 0, raw.data() + 3, (int)raw.size() - 3, html.data(), len);
        }
    } else {
        int len = MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), nullptr, 0);
        if (len > 0) {
            html.resize(len);
            MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), html.data(), len);
        }
    }

    for (size_t pos = 0; pos < html.size();) {
        size_t a = html.find(L"<A ", pos);
        if (a == std::wstring::npos) break;
        size_t hrefKey = html.find(L"HREF=\"", a);
        if (hrefKey == std::wstring::npos) { pos = a + 2; continue; }
        size_t u0 = hrefKey + 6;
        size_t u1 = html.find(L'"', u0);
        if (u1 == std::wstring::npos) break;
        std::wstring url = html.substr(u0, u1 - u0);
        size_t gt = html.find(L'>', u1);
        size_t end = html.find(L"</A>", gt == std::wstring::npos ? u1 : gt);
        if (gt == std::wstring::npos || end == std::wstring::npos) { pos = a + 2; continue; }
        std::wstring name = html.substr(gt + 1, end - gt - 1);
        name = decodeHtmlEntities(name);
        url = decodeHtmlEntities(url);
        while (!name.empty() && (name.front() == L' ' || name.front() == L'\t')) name.erase(name.begin());
        while (!name.empty() && (name.back() == L' ' || name.back() == L'\t')) name.pop_back();
        if (!url.empty() && url.find(L"javascript:") != 0) {
            if (bookmarkIndex(url) < 0) {
                addBookmark(name.empty() ? url : name, url);
                added++;
            }
        }
        pos = end + 4;
    }
    return added;
}

static int downloadUiState(int webviewState) {
    if (webviewState == 2) return 1; // completed
    if (webviewState == 1) return 3; // interrupted
    return 0; // in progress
}

std::wstring BrowserCore::downloadsPanelJson() const {
    auto esc = [](const std::wstring& s) {
        std::wstring o;
        for (wchar_t c : s) {
            if (c == L'"' || c == L'\\') o += L'\\';
            o += c;
        }
        return o;
    };
    std::wstring j = L"{\"folder\":\"" + esc(effectiveDownloadPath()) + L"\",\"items\":[";
    for (size_t i = 0; i < m_downloads.size(); i++) {
        if (i) j += L',';
        j += L"{\"name\":\"" + esc(m_downloads[i].fileName) +
            L"\",\"path\":\"" + esc(m_downloads[i].path) +
            L"\",\"received\":" + std::to_wstring(m_downloads[i].receivedBytes) +
            L",\"total\":" + std::to_wstring(m_downloads[i].totalBytes) +
            L",\"state\":" + std::to_wstring(downloadUiState(m_downloads[i].state)) + L"}";
    }
    j += L"]}";
    return j;
}

void BrowserCore::setChromeLayoutPx(int barPx, int totalPx) {
    if (barPx < 72) barPx = 72;
    if (totalPx < barPx) totalPx = barPx;
    if (m_chromeBarPx == barPx && m_chromeWndPx == totalPx) return;
    m_chromeBarPx = barPx;
    m_chromeWndPx = totalPx;
    if (dlPopoverChanged) dlPopoverChanged();
}

void BrowserCore::closeAllPanels() {
    if (!m_settingsOpen && !m_historyOpen && !m_bookmarksOpen) return;
    m_settingsOpen = false;
    m_historyOpen = false;
    m_bookmarksOpen = false;
    sendStateToChrome();
    if (panelOpenChanged) panelOpenChanged(false);
}

void BrowserCore::openPanelView(const wchar_t* view) {
    const bool wantSettings = view && wcscmp(view, L"settings") == 0;
    const bool wantHistory = view && wcscmp(view, L"history") == 0;
    const bool wantBookmarks = view && wcscmp(view, L"bookmarks") == 0;
    if (!wantSettings && !wantHistory && !wantBookmarks) return;

    m_settingsOpen = wantSettings;
    m_historyOpen = wantHistory;
    m_bookmarksOpen = wantBookmarks;
    m_downloadsOpen = false;
    sendStateToChrome();
    if (panelOpenChanged) panelOpenChanged(true);
}

void BrowserCore::setDownloadsPanelOpen(bool open) {
    if (m_downloadsOpen == open) return;
    m_downloadsOpen = open;
    if (open) {
        m_settingsOpen = false;
        m_historyOpen = false;
        m_bookmarksOpen = false;
        constexpr int kDlPanelCssPx = 280;
        int bar = m_chromeBarPx > 0 ? m_chromeBarPx : 74;
        int need = bar + kDlPanelCssPx;
        if (m_chromeWndPx < need) {
            m_chromeWndPx = need;
            if (dlPopoverChanged) dlPopoverChanged();
        }
    } else {
        m_chromeWndPx = m_chromeBarPx;
        if (dlPopoverChanged) dlPopoverChanged();
    }
    sendStateToChrome();
}

void BrowserCore::navigateTo(const wchar_t* url) {
    if (m_currentTab < 0) return;
    std::wstring raw = url ? url : L"";
    std::wstring trimmed = trimWs(raw);
    if (!trimmed.empty() && !looksLikeNavigableUrl(trimmed)) {
        addHistory(searchUrl(trimmed), trimmed);
        saveHistory();
    }
    std::wstring u = resolveUrl(raw.c_str());
    m_tabs[m_currentTab].url = u;
    m_tabs[m_currentTab].title = L"Loading...";
    m_tabs[m_currentTab].phantom = false;
    if (navigateContent) navigateContent(u);
    sendStateToChrome();
    saveSession();
}

void BrowserCore::goBack() {
    sendStateToChrome();
}

void BrowserCore::goForward() {
    sendStateToChrome();
}

void BrowserCore::reload() {
    sendStateToChrome();
}

void BrowserCore::stop() {
    m_isLoading = false;
    sendStateToChrome();
}

void BrowserCore::goHome() {
    navigateTo(m_homePage.c_str());
}

void BrowserCore::setTabUrl(int idx, const std::wstring& url) {
    if (idx >= 0 && idx < (int)m_tabs.size()) {
        m_tabs[idx].url = url;
        sendStateToChrome();
        saveSession();
    }
}

void BrowserCore::setTabTitle(int idx, const std::wstring& title) {
    if (idx >= 0 && idx < (int)m_tabs.size()) {
        m_tabs[idx].title = title;
        sendStateToChrome();
    }
}

void BrowserCore::setTabFavicon(int idx, const std::wstring& favicon) {
    if (idx >= 0 && idx < (int)m_tabs.size()) {
        m_tabs[idx].favicon = favicon;
        if (!favicon.empty()) {
            std::wstring host = hostFromUrl(m_tabs[idx].url);
            if (!host.empty() && favicon.find(L"ancore:") != 0 && favicon.find(L"file:") != 0) {
                m_hostFavicons[host] = favicon;
                saveHostFavicons();
            }
        }
        sendStateToChrome();
    }
}

void BrowserCore::setTabAudio(int idx, bool playing) {
    if (idx >= 0 && idx < (int)m_tabs.size()) {
        m_tabs[idx].audio = playing;
        sendStateToChrome();
    }
}

// ── Session restore ────────────────────────────────────────────
void BrowserCore::saveSession() {
    std::wstring p = settingsPath() + L"\\session.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"w,ccs=UTF-8") == 0 && f) {
        for (auto& t : m_tabs)
            fwprintf(f, L"%s\n", t.url.c_str());
        fclose(f);
    }
}

void BrowserCore::loadSession() {
    std::wstring p = settingsPath() + L"\\session.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"r,ccs=UTF-8") == 0 && f) {
        wchar_t line[4096];
        while (fgetws(line, 4096, f)) {
            std::wstring s = line;
            while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();
            if (!s.empty()) {
                size_t idx = m_tabs.size();
                m_tabs.push_back({});
                m_tabs.back().url = s;
                m_tabs.back().title = L"Loading...";
            }
        }
        fclose(f);
        if (!m_tabs.empty()) switchTab(0);
    }
}

bool BrowserCore::hasSessionFile() const {
    std::wstring p = settingsPath() + L"\\session.txt";
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// ── Omnibox ────────────────────────────────────────────────────
static void jsonAppendEscaped(std::wstring& j, const std::wstring& s) {
    for (wchar_t wc : s) {
        if (wc == L'"' || wc == L'\\') j += L'\\';
        else if (wc == L'\n') { j += L"\\n"; continue; }
        else if (wc == L'\r') continue;
        j += wc;
    }
}

static bool icontainsWs(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) return true;
    return toLowerWs(hay).find(toLowerWs(needle)) != std::wstring::npos;
}

static bool isHistoryOmniboxCandidate(const std::wstring& url) {
    if (url.empty()) return false;
    if (url.find(L"ancore:") == 0) return false;
    if (url.find(L"file:///") == 0) return false;
    if (url.find(L"newtab.html") != std::wstring::npos) return false;
    if (url.find(L"blockpage.html") != std::wstring::npos) return false;
    return true;
}

static std::wstring searchEngineLabel(const std::wstring& engine) {
    if (engine == L"yandex") return L"\u042f\u043d\u0434\u0435\u043a\u0441";
    if (engine == L"bing") return L"Bing";
    if (engine == L"duckduckgo") return L"DuckDuckGo";
    return L"Google";
}

static void suggestAppendItem(std::wstring& j, int& count, int maxItems,
    const wchar_t* kind, const std::wstring& title, const std::wstring& subtitle, const std::wstring& url) {
    if (count >= maxItems) return;
    if (count) j += L',';
    j += L"{\"kind\":\"";
    j += kind;
    j += L"\",\"title\":\"";
    jsonAppendEscaped(j, title);
    j += L"\",\"subtitle\":\"";
    jsonAppendEscaped(j, subtitle);
    j += L"\",\"url\":\"";
    jsonAppendEscaped(j, url);
    j += L"\"}";
    count++;
}

static void suggestAppendWord(std::wstring& j, int& count, int maxItems,
    const std::wstring& title, const std::wstring& fill, const wchar_t* mode,
    const std::wstring& subtitle = L"") {
    if (count >= maxItems) return;
    if (count) j += L',';
    j += L"{\"kind\":\"word\",\"title\":\"";
    jsonAppendEscaped(j, title);
    j += L"\",\"fill\":\"";
    jsonAppendEscaped(j, fill);
    j += L"\",\"mode\":\"";
    j += mode;
    j += L"\"";
    if (!subtitle.empty()) {
        j += L",\"subtitle\":\"";
        jsonAppendEscaped(j, subtitle);
        j += L"\"";
    }
    j += L"}";
    count++;
}

static std::wstring formatSearchHitsSubtitle(int hits) {
    if (hits <= 1) return L"";
    int mod10 = hits % 10, mod100 = hits % 100;
    const wchar_t* word = L"\u0440\u0430\u0437";
    if (mod10 == 1 && mod100 != 11) word = L"\u0440\u0430\u0437";
    else if (mod10 >= 2 && mod10 <= 4 && (mod100 < 10 || mod100 >= 20)) word = L"\u0440\u0430\u0437\u0430";
    return std::to_wstring(hits) + L" " + word;
}

struct QueryCompletion {
    std::wstring display;
    int hits = 0;
    ULONGLONG time64 = 0;
};

static void mergeSearchQueryStat(std::map<std::wstring, QueryCompletion>& stats,
    const std::wstring& sq, int hits, FILETIME ft) {
    std::wstring key = toLowerWs(sq);
    if (key.empty()) return;
    ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    auto it = stats.find(key);
    if (it == stats.end()) {
        stats[key] = {sq, hits, t};
    } else {
        it->second.hits += hits;
        if (t >= it->second.time64) {
            it->second.time64 = t;
            it->second.display = sq;
        }
    }
}

static std::vector<QueryCompletion> rankedSearchCompletions(
    const std::vector<HistoryEntry>& history, const std::wstring& needleLower) {
    std::map<std::wstring, QueryCompletion> stats;
    for (int i = (int)history.size() - 1; i >= 0; i--) {
        auto& h = history[i];
        if (!isSearchEngineUrl(h.url)) continue;
        std::wstring sq = trimWs(h.title.empty() ? searchQueryFromUrl(h.url) : h.title);
        if (sq.empty()) continue;
        std::wstring key = toLowerWs(sq);
        if (!needleLower.empty() &&
            key.compare(0, needleLower.size(), needleLower) != 0)
            continue;
        int hHits = h.hits > 0 ? h.hits : 1;
        mergeSearchQueryStat(stats, sq, hHits, h.time);
    }
    std::vector<QueryCompletion> out;
    out.reserve(stats.size());
    for (auto& kv : stats) out.push_back(kv.second);
    std::sort(out.begin(), out.end(), [](const QueryCompletion& a, const QueryCompletion& b) {
        if (a.hits != b.hits) return a.hits > b.hits;
        return a.time64 > b.time64;
    });
    return out;
}

static void collectPrefixWords(const std::wstring& text, const std::wstring& lastWordLower,
    std::map<std::wstring, std::wstring>& out) {
    if (lastWordLower.empty()) return;
    std::wstring trimmed = trimWs(text);
    if (trimmed.empty()) return;
    bool hasSpace = trimmed.find(L' ') != std::wstring::npos;
    if (!hasSpace) {
        std::wstring key = toLowerWs(trimmed);
        if (key.size() > lastWordLower.size() && key.compare(0, lastWordLower.size(), lastWordLower) == 0)
            out[key] = trimmed;
    }
    std::wstring token;
    auto flush = [&]() {
        if (token.size() <= lastWordLower.size()) { token.clear(); return; }
        std::wstring key = toLowerWs(token);
        if (key.compare(0, lastWordLower.size(), lastWordLower) == 0 && !out.count(key))
            out[key] = token;
        token.clear();
    };
    for (wchar_t c : trimmed) {
        if (iswalnum(c) || c > 127 || c == L'-') token += c;
        else flush();
    }
    flush();
}

void BrowserCore::sendUrlSuggestions(const std::wstring& query) {
    if (!postToChrome) return;

    const int kMaxWordComplete = 5;
    const int kMaxHist = 10;
    std::wstring q = trimWs(query);

    std::wstring prefix;
    std::wstring lastWord;
    size_t sp = q.find_last_of(L" \t");
    if (sp == std::wstring::npos) {
        lastWord = q;
        prefix = L"";
    } else {
        prefix = q.substr(0, sp + 1);
        lastWord = trimWs(q.substr(sp + 1));
    }
    std::wstring lastWordLower = toLowerWs(lastWord);
    std::wstring needleLower = toLowerWs(q);
    bool endsWithSpace = !query.empty() && (query.back() == L' ' || query.back() == L'\t');
    bool partialInput = !q.empty() && !endsWithSpace;

    std::wstring j = L"{\"type\":\"suggest\",\"query\":\"";
    jsonAppendEscaped(j, q);
    j += L"\",\"words\":[";

    static const wchar_t* kWordBank[] = {
        L"\u043a\u043e\u0436\u0430", L"\u043a\u043e\u0436\u0443\u0445", L"\u043a\u043e\u0436\u0430\u043d\u044b\u0439",
        L"\u043f\u043e\u0433\u043e\u0434\u0430", L"\u043f\u0435\u0440\u0435\u0432\u043e\u0434", L"\u043d\u043e\u0432\u043e\u0441\u0442\u0438",
        L"\u043a\u0430\u0440\u0442\u044b", L"\u043c\u0443\u0437\u044b\u043a\u0430", L"\u0440\u0435\u0446\u0435\u043f\u0442",
        L"\u0444\u0438\u043b\u044c\u043c", L"\u0438\u0433\u0440\u0430", L"\u043a\u043d\u0438\u0433\u0430",
        L"\u043e\u0434\u0435\u0436\u0434\u0430", L"\u0430\u0432\u0442\u043e", L"\u0434\u043e\u043c",
        L"\u0440\u0430\u0431\u043e\u0442\u0430", L"\u043a\u0443\u0440\u0441", L"\u0448\u043a\u043e\u043b\u0430",
        L"youtube", L"google", L"\u044f\u043d\u0434\u0435\u043a\u0441", L"\u0432\u043a"
    };

    int wordCount = 0;
    std::map<std::wstring, bool> wordKeysUsed;

    if (partialInput) {
        auto ranked = rankedSearchCompletions(m_history, needleLower);
        for (auto& qc : ranked) {
            if (wordCount >= kMaxWordComplete) break;
            std::wstring key = toLowerWs(qc.display);
            if (key == needleLower) continue;
            wordKeysUsed[key] = true;
            std::wstring sub = formatSearchHitsSubtitle(qc.hits);
            if (sub.empty()) sub = L"\u0418\u0437 \u0438\u0441\u0442\u043e\u0440\u0438\u0438";
            suggestAppendWord(j, wordCount, kMaxWordComplete, qc.display, qc.display,
                L"replace_all", sub);
        }

        if (wordCount < kMaxWordComplete) {
            std::map<std::wstring, std::wstring> completions;
            for (int i = (int)m_history.size() - 1; i >= 0 && (int)completions.size() < 60; i--) {
                auto& h = m_history[i];
                if (!isHistoryOmniboxCandidate(h.url)) continue;
                if (!h.title.empty()) collectPrefixWords(h.title, lastWordLower, completions);
                if (isSearchEngineUrl(h.url)) {
                    std::wstring sq = h.title.empty() ? searchQueryFromUrl(h.url) : h.title;
                    collectPrefixWords(sq, lastWordLower, completions);
                }
            }
            for (auto w : kWordBank)
                collectPrefixWords(w, lastWordLower, completions);

            for (auto& kv : completions) {
                if (wordCount >= kMaxWordComplete) break;
                if (wordKeysUsed.count(kv.first)) continue;
                std::wstring fill = prefix + kv.second;
                suggestAppendWord(j, wordCount, kMaxWordComplete, kv.second, fill,
                    prefix.empty() ? L"replace_all" : L"replace_last", L"");
            }
        }
    } else if (q.empty()) {
        static const wchar_t* kPopular[] = {
            L"youtube", L"\u043f\u043e\u0433\u043e\u0434\u0430", L"\u044f\u043d\u0434\u0435\u043a\u0441", L"\u0432\u043a",
            L"\u043d\u043e\u0432\u043e\u0441\u0442\u0438"
        };
        for (auto phrase : kPopular) {
            if (wordCount >= kMaxWordComplete) break;
            suggestAppendWord(j, wordCount, kMaxWordComplete, phrase, phrase, L"replace_all");
        }
    }

    if (!q.empty() && !partialInput && looksLikeNavigableUrl(q)) {
        std::wstring resolved = resolveUrl(q);
        suggestAppendWord(j, wordCount, kMaxWordComplete + 2,
            L"\u041e\u0442\u043a\u0440\u044b\u0442\u044c " + resolved, resolved, L"open");
    }

    j += L"],\"history\":[";
    int histCount = 0;
    std::vector<std::wstring> urlSeen;

    auto urlSeenFn = [&](const std::wstring& u) {
        for (auto& s : urlSeen) if (s == u) return true;
        urlSeen.push_back(u);
        return false;
    };

    if (!q.empty() && !looksLikeNavigableUrl(q)) {
        std::wstring label = searchEngineLabel(m_searchEngine);
        std::wstring sub = L"\u041f\u043e\u0438\u0441\u043a \u00b7 " + label;
        suggestAppendItem(j, histCount, kMaxHist, L"search",
            L"\u041f\u043e\u0438\u0441\u043a: " + q, sub, searchUrl(q));
    }

    if (!q.empty()) {
        for (auto& b : m_bookmarks) {
            if (histCount >= kMaxHist) break;
            if (!icontainsWs(b.name, q) && !icontainsWs(b.url, q)) continue;
            if (urlSeenFn(b.url)) continue;
            std::wstring sub = b.url;
            if (sub.size() > 72) sub = sub.substr(0, 69) + L"...";
            suggestAppendItem(j, histCount, kMaxHist, L"bookmark",
                b.name.empty() ? b.url : b.name, sub, b.url);
        }
    }

    for (int i = (int)m_history.size() - 1; i >= 0; i--) {
        if (histCount >= kMaxHist) break;
        auto& h = m_history[i];
        if (!isHistoryOmniboxCandidate(h.url)) continue;
        bool isSearch = isSearchEngineUrl(h.url);
        std::wstring sq = isSearch ? (h.title.empty() ? searchQueryFromUrl(h.url) : h.title) : L"";
        if (!q.empty()) {
            if (isSearch) {
                if (sq.empty() || !icontainsWs(sq, q)) continue;
            } else if (!icontainsWs(h.url, q) && !icontainsWs(h.title, q) &&
                !icontainsWs(hostFromUrl(h.url), q))
                continue;
        }
        if (urlSeenFn(h.url)) continue;
        std::wstring title = isSearch && !sq.empty() ? sq :
            (h.title.empty() ? hostFromUrl(h.url) : h.title);
        if (title.empty()) title = h.url;
        std::wstring sub = isSearch ?
            (L"\u041f\u043e\u0438\u0441\u043a \u00b7 " + searchEngineLabel(m_searchEngine)) : h.url;
        if (sub.size() > 72) sub = sub.substr(0, 69) + L"...";
        suggestAppendItem(j, histCount, kMaxHist, isSearch ? L"search" : L"history", title, sub, h.url);
    }

    j += L"]}";
    postToChrome(j);
}

// ── IPC: messages from chrome HTML ─────────────────────────────
void BrowserCore::handleChromeMessage(const std::wstring& msg) {
    auto cmd = msg;
    std::wstring val;
    auto p = msg.find(L':');
    if (p != std::wstring::npos) { cmd = msg.substr(0, p); val = msg.substr(p + 1); }

    if      (cmd == L"goBack")       { if (goBackContent) goBackContent(); }
    else if (cmd == L"goForward")    { if (goForwardContent) goForwardContent(); }
    else if (cmd == L"reload")       { if (m_isLoading) { if (stopContent) stopContent(); } else { if (reloadContent) reloadContent(); } }
    else if (cmd == L"goHome")       { navigateTo(m_homePage.c_str()); }
    else if (cmd == L"addTab")       { addTab(); }
    else if (cmd == L"phantomTab")   {
        // Commit current phantom if it has real content (not a blank newtab)
        if (m_currentTab >= 0 && m_tabs[m_currentTab].phantom &&
            m_tabs[m_currentTab].url.find(L"newtab") == std::wstring::npos &&
            m_tabs[m_currentTab].url.find(L"ancore:") == std::wstring::npos) {
            m_tabs[m_currentTab].phantom = false;
        }
        addTab(); // creates a new phantom tab and switches to it
    }
    else if (cmd == L"addBookmark") {
        if (m_currentTab >= 0)
            addBookmark(m_tabs[m_currentTab].title, m_tabs[m_currentTab].url);
    }
    else if (cmd == L"toggleBookmark") {
        if (m_currentTab >= 0) {
            int bi = bookmarkIndex(m_tabs[m_currentTab].url);
            if (bi >= 0) removeBookmark(bi);
            else addBookmark(m_tabs[m_currentTab].title, m_tabs[m_currentTab].url);
        }
    }
    else if (cmd == L"urlSuggest")   { sendUrlSuggestions(val); }
    else if (cmd == L"nav")          { navigateTo(val.c_str()); }
    else if (cmd == L"navNewTab")    { addTab(val.empty() ? nullptr : val.c_str()); }
    else if (cmd == L"restoreClosedTab") { restoreClosedTab(); }
    else if (cmd == L"switchTab")    { switchTab(_wtoi(val.c_str())); }
    else if (cmd == L"closeTab")     { closeTab(_wtoi(val.c_str())); }
    else if (cmd == L"duplicateTab") { duplicateTab(_wtoi(val.c_str())); }
    else if (cmd == L"closeOtherTabs") { closeOtherTabs(_wtoi(val.c_str())); }
    else if (cmd == L"copyTabUrl")   { copyTabUrl(_wtoi(val.c_str())); }
    else if (cmd == L"reloadTab") {
        int idx = _wtoi(val.c_str());
        if (idx == m_currentTab) {
            if (reloadContent) reloadContent();
        } else if (reloadTabContent) {
            reloadTabContent(idx);
        }
    }
    else if (cmd == L"min")          { ShowWindow(mainWindow(), SW_MINIMIZE); }
    else if (cmd == L"max")          {
        WINDOWPLACEMENT wp = {sizeof(WINDOWPLACEMENT)};
        GetWindowPlacement(mainWindow(), &wp);
        ShowWindow(mainWindow(), (wp.showCmd == SW_SHOWMAXIMIZED) ? SW_RESTORE : SW_MAXIMIZE);
        sendStateToChrome();
    }
    else if (cmd == L"close")        { PostMessage(mainWindow(), WM_CLOSE, 0, 0); }
    else if (cmd == L"dragStart")    {
        ReleaseCapture();
        SendMessageW(mainWindow(), WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
    else if (cmd == L"settings") {
        if (m_settingsOpen && !m_historyOpen && !m_bookmarksOpen)
            closeAllPanels();
        else
            openPanelView(L"settings");
    }
    else if (cmd == L"showHistory") {
        if (m_historyOpen && !m_settingsOpen && !m_bookmarksOpen)
            closeAllPanels();
        else
            openPanelView(L"history");
    }
    else if (cmd == L"setEngine")    { setSearchEngine(val); }
    else if (cmd == L"setHomepage")  { setHomePage(val); }
    else if (cmd == L"setStartup")   { setStartupBehavior(_wtoi(val.c_str())); }
    else if (cmd == L"setAdblock")   { setAdBlockEnabled(val == L"1"); }
    else if (cmd == L"setDownload")  { setDownloadPath(val); }
    else if (cmd == L"clearHistory") { clearHistory(); if (historyOpenChanged) historyOpenChanged(false); }
    else if (cmd == L"clearBookmarks") {
        m_bookmarks.clear();
        saveBookmarks();
        sendStateToChrome();
        if (m_bookmarksOpen && panelOpenChanged)
            panelOpenChanged(true);
    }
    else if (cmd == L"zoomIn")         { if (zoomContent) zoomContent(0.1); }
    else if (cmd == L"zoomOut")        { if (zoomContent) zoomContent(-0.1); }
    else if (cmd == L"zoomReset")      { if (zoomContent) zoomContent(-999); }
    else if (cmd == L"find")           { if (findContent) findContent(val, false); }
    else if (cmd == L"findBack")       { if (findContent) findContent(val, true); }
    else if (cmd == L"findClear")      { if (findClearContent) findClearContent(); }
    else if (cmd == L"setClearOnExit") { setClearOnExit(val == L"1"); }
    else if (cmd == L"showBookmarks") {
        if (m_bookmarksOpen && !m_settingsOpen && !m_historyOpen)
            closeAllPanels();
        else
            openPanelView(L"bookmarks");
    }
    else if (cmd == L"openBookmark") { navigateTo(val.c_str()); }
    else if (cmd == L"removeBookmark") {
        int idx = _wtoi(val.c_str());
        if (idx >= 0 && idx < (int)m_bookmarks.size()) removeBookmark(idx);
    }
    else if (cmd == L"reorderTab") {
        // val = "fromIdx:toIdx"
        auto colon = val.find(L':');
        if (colon != std::wstring::npos) {
            int fromIdx = _wtoi(val.substr(0, colon).c_str());
            int toIdx = _wtoi(val.substr(colon + 1).c_str());
            if (fromIdx >= 0 && fromIdx < (int)m_tabs.size() &&
                toIdx >= 0 && toIdx < (int)m_tabs.size() && fromIdx != toIdx) {
                // Reorder tab data
                TabInfo ti = m_tabs[fromIdx];
                m_tabs.erase(m_tabs.begin() + fromIdx);
                m_tabs.insert(m_tabs.begin() + toIdx, ti);
                // Update current tab index
                if (m_currentTab == fromIdx) m_currentTab = toIdx;
                else if (fromIdx < m_currentTab && toIdx >= m_currentTab) m_currentTab--;
                else if (fromIdx > m_currentTab && toIdx <= m_currentTab) m_currentTab++;
                if (reorderTabContent) reorderTabContent(fromIdx, toIdx);
                else if (switchTabContent) switchTabContent(m_currentTab);
                sendStateToChrome();
            }
        }
    }
    else if (cmd == L"devtools") {
        if (devtoolsContent) devtoolsContent();
    }
    else if (cmd == L"pinTab") {
        int idx = _wtoi(val.c_str());
        if (idx >= 0 && idx < (int)m_tabs.size()) {
            m_tabs[idx].pinned = !m_tabs[idx].pinned;
            sendStateToChrome();
        }
    }
    else if (cmd == L"clearDownloads") { clearDownloads(); }
    else if (cmd == L"openDownload") {
        int idx = _wtoi(val.c_str());
        if (idx >= 0 && idx < (int)m_downloads.size() && m_downloads[idx].state == 2) {
            ShellExecuteW(mainWindow(), L"open", m_downloads[idx].path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
    else if (cmd == L"openDownloadFolder") {
        std::wstring path = effectiveDownloadPath();
        if (!path.empty())
            ShellExecuteW(mainWindow(), L"explore", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    else if (cmd == L"chromeLayout") {
        auto colon = val.find(L':');
        if (colon != std::wstring::npos) {
            int barPx = _wtoi(val.substr(0, colon).c_str());
            int totalPx = _wtoi(val.substr(colon + 1).c_str());
            setChromeLayoutPx(barPx, totalPx);
        }
    }
    else if (cmd == L"showDownloads") {
        setDownloadsPanelOpen(!m_downloadsOpen);
    }
    else if (cmd == L"openDownloadSettings") {
        setDownloadsPanelOpen(false);
        requestSettingsFocus(L"downloads");
        openPanelView(L"settings");
    }
    else if (cmd == L"dlPopupClose") {
        setDownloadsPanelOpen(false);
    }
    else if (cmd == L"fullscreen") {
        if (getFullscreen) {
            toggleFullscreen();
        }
    }
    else if (cmd == L"nextTab") { nextTab(); }
    else if (cmd == L"prevTab") { prevTab(); }
    else if (cmd == L"cancelDownload") { cancelDownload(_wtoi(val.c_str())); }
    else if (cmd == L"importBookmarks") {
        int n = importBookmarksFromFile(mainWindow());
        if (n > 0) {
            setStatusText(L"Импортировано закладок: " + std::to_wstring(n));
            if (m_bookmarksOpen && panelOpenChanged) panelOpenChanged(true);
        } else {
            setStatusText(L"Импорт закладок: файл не выбран или закладки не найдены");
        }
    }
}

// ── IPC: messages from content HTML ────────────────────────────
void BrowserCore::handleContentMessage(const std::wstring& msg, ICoreWebView2* sender) {
    const std::wstring saveWidgetsPrefix = L"saveNewTabWidgets";
    if (msg.size() >= saveWidgetsPrefix.size() &&
        msg.compare(0, saveWidgetsPrefix.size(), saveWidgetsPrefix) == 0) {
        setNewTabWidgetsFromJson(msg.substr(saveWidgetsPrefix.size()));
        if (sender) {
            std::wstring ack = L"__ancore__widgetsSaved" + newTabDataJson();
            sender->PostWebMessageAsString(ack.c_str());
        }
        return;
    }

    auto cmd = msg;
    std::wstring val;
    auto p = msg.find(L':');
    if (p != std::wstring::npos) { cmd = msg.substr(0, p); val = msg.substr(p + 1); }

    if (cmd == L"ctx") {
        if (contextMenuAction) contextMenuAction(val, sender);
        return;
    }
    if (cmd == L"nav") {
        navigateTo(val.c_str());
    } else if (cmd == L"linkNew") {
        if (!val.empty()) addTab(val.c_str());
    } else if (cmd == L"linkBg") {
        if (!val.empty()) addTabBackground(val.c_str());
    } else if (cmd == L"stbar") {
        m_statusText = val;
        sendStateToChrome();
    } else if (cmd == L"getHistory") {
        if (!m_historyOpen) return;
        // Build history JSON and send to content
        std::wstring j = L"__ancore__historyData[";
        for (size_t i = 0; i < m_history.size(); i++) {
            if (i) j += L',';
            j += L"{\"url\":\"";
            for (auto wc : m_history[i].url) {
                if (wc == L'"' || wc == L'\\') j += L'\\';
                j += wc;
            }
            j += L"\",\"title\":\"";
            for (auto wc : m_history[i].title) {
                if (wc == L'"' || wc == L'\\') j += L'\\';
                j += wc;
            }
            // Format time
            FILETIME lft; FileTimeToLocalFileTime(&m_history[i].time, &lft);
            SYSTEMTIME st; FileTimeToSystemTime(&lft, &st);
            wchar_t buf[32];
            wchar_t dbuf[16];
            swprintf(buf, 32, L"%02d.%02d %02d:%02d", st.wMonth, st.wDay, st.wHour, st.wMinute);
            swprintf(dbuf, 16, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
            j += L"\",\"time\":\"";
            j += buf;
            j += L"\",\"date\":\"";
            j += dbuf;
            j += L"\"}";
        }
        j += L"]";
        if (sender) sender->PostWebMessageAsString(j.c_str());
    } else if (cmd == L"getTopSites") {
        if (sender) {
            std::wstring j = L"__ancore__topSites" + topSitesJson(8);
            sender->PostWebMessageAsString(j.c_str());
        }
    } else if (cmd == L"getNewTabData") {
        if (sender) {
            std::wstring j = L"__ancore__newTabData" + newTabDataJson();
            sender->PostWebMessageAsString(j.c_str());
        }
    } else if (cmd == L"zoomIn") {
        if (zoomContent) zoomContent(0.1);
    } else if (cmd == L"zoomOut") {
        if (zoomContent) zoomContent(-0.1);
    } else if (cmd == L"zoomReset") {
        if (zoomContent) zoomContent(-999);
    } else if (cmd == L"getBookmarks") {
        if (!m_bookmarksOpen) return;
        std::wstring j = L"__ancore__bookmarksData[";
        for (size_t i = 0; i < m_bookmarks.size(); i++) {
            if (i) j += L',';
            j += L"{\"name\":\"";
            for (auto wc : m_bookmarks[i].name) {
                if (wc == L'"' || wc == L'\\') j += L'\\';
                j += wc;
            }
            j += L"\",\"url\":\"";
            for (auto wc : m_bookmarks[i].url) {
                if (wc == L'"' || wc == L'\\') j += L'\\';
                j += wc;
            }
            j += L"\"}";
        }
        j += L"]";
        if (sender) sender->PostWebMessageAsString(j.c_str());
    } else if (cmd == L"clearHistory") {
        clearHistory();
        if (panelOpenChanged) panelOpenChanged(m_settingsOpen || m_historyOpen || m_bookmarksOpen);
    } else if (cmd == L"openBookmark") {
        navigateTo(val.c_str());
    } else {
        // Forward all other messages to chrome handler (settings, bookmarks, etc.)
        handleChromeMessage(msg);
    }
}

// ── IPC: send state to chrome HTML ─────────────────────────────
void BrowserCore::sendStateToChrome() {
    if (!postToChrome) return;

    HWND mw = mainWindow();
    int visibleCurrent = -1;
    int visibleIdx = 0;
    bool hasPhantom = false;
    for (size_t i = 0; i < m_tabs.size(); i++) {
        if (m_tabs[i].phantom) { hasPhantom = true; continue; }
        if ((int)i == m_currentTab) visibleCurrent = visibleIdx;
        visibleIdx++;
    }
    std::wstring j = L"{\"type\":\"state\",\"current\":" + std::to_wstring(visibleCurrent) +
                     L",\"loading\":" + std::wstring(m_isLoading ? L"true" : L"false") +
                     L",\"maximized\":" + std::wstring(mw && IsZoomed(mw) ? L"true" : L"false") +
                     L",\"bookmarked\":" + std::wstring(
                         (m_currentTab >= 0 && !m_tabs[m_currentTab].phantom && isUrlBookmarked(m_tabs[m_currentTab].url)) ? L"true" : L"false") +
                     L",\"canGoBack\":" + std::wstring(m_canGoBack ? L"true" : L"false") +
                     L",\"canGoForward\":" + std::wstring(m_canGoForward ? L"true" : L"false") +
                     L",\"url\":\"";
    if (m_currentTab >= 0) {
        for (auto wc : m_tabs[m_currentTab].url) {
            if (wc == L'"' || wc == L'\\') j += L'\\';
            j += wc;
        }
    }
    // Compute security state from current URL as fallback
    int sec = m_securityState;
    if (sec == 0 && m_currentTab >= 0 && m_currentTab < (int)m_tabs.size()) {
        auto& u = m_tabs[m_currentTab].url;
        if (u.find(L"https://") == 0) sec = 1;
        else if (u.find(L"http://") == 0) sec = 2;
    }
    j += L"\",\"security\":" + std::to_wstring(sec) +
         L",\"settingsOpen\":" + std::wstring(m_settingsOpen ? L"true" : L"false") +
        L",\"searchEngine\":\"" + m_searchEngine + L"\""
        L",\"homePage\":\"";
    for (auto wc : m_homePage) {
        if (wc == L'"' || wc == L'\\') j += L'\\';
        j += wc;
    }
    j += L"\",\"adBlock\":" + std::wstring(m_adBlock ? L"true" : L"false") +
                     L",\"zoom\":" + std::wstring(getZoom ? std::to_wstring((int)(getZoom() * 100 + 0.5)) : L"100") +
                     L",\"fullscreen\":" + std::wstring(getFullscreen && getFullscreen() ? L"true" : L"false") +
                     L",\"startupBehavior\":" + std::to_wstring(m_startupBehavior) +
        L",\"clearOnExit\":" + std::wstring(m_clearOnExit ? L"true" : L"false") +
        L",\"downloadPath\":\"";
    for (auto wc : m_downloadPath) {
        if (wc == L'"' || wc == L'\\') j += L'\\';
        j += wc;
    }
    j += L"\",\"tabs\":[";
    for (size_t i = 0; i < m_tabs.size(); i++) {
        if (m_tabs[i].phantom) continue;
        if (j.back() != L'[') j += L',';
        j += L"{\"title\":\"";
        for (auto wc : m_tabs[i].title) {
            if (wc == L'"' || wc == L'\\') j += L'\\';
            j += wc;
        }
        j += L"\",\"favicon\":\"";
        for (auto wc : m_tabs[i].favicon) {
            if (wc == L'"' || wc == L'\\') j += L'\\';
            j += wc;
        }
        j += L"\",\"url\":\"";
        for (auto wc : m_tabs[i].url) {
            if (wc == L'"' || wc == L'\\') j += L'\\';
            j += wc;
        }
        j += L"\",\"audio\":" + std::wstring(m_tabs[i].audio ? L"true" : L"false") +
             L",\"pinned\":" + std::wstring(m_tabs[i].pinned ? L"true" : L"false") + L"}";
    }
    j += L"],\"phantomTab\":" + std::wstring(hasPhantom ? L"true" : L"false") +
        L",\"bookmarksOpen\":" + std::wstring(m_bookmarksOpen ? L"true" : L"false") +
        L",\"historyOpen\":" + std::wstring(m_historyOpen ? L"true" : L"false");
    if (m_historyOpen) {
        j += L",\"history\":[";
        for (size_t i = 0; i < m_history.size(); i++) {
            if (i) j += L',';
            j += L"{\"url\":\"";
            for (auto wc : m_history[i].url) {
                if (wc == L'"' || wc == L'\\') j += L'\\';
                j += wc;
            }
            j += L"\",\"title\":\"";
            for (auto wc : m_history[i].title) {
                if (wc == L'"' || wc == L'\\') j += L'\\';
                j += wc;
            }
            // Format time for display
            FILETIME lft; FileTimeToLocalFileTime(&m_history[i].time, &lft);
            SYSTEMTIME st; FileTimeToSystemTime(&lft, &st);
            wchar_t buf[32];
            wchar_t dbuf[16];
            swprintf(buf, 32, L"%02d.%02d %02d:%02d", st.wMonth, st.wDay, st.wHour, st.wMinute);
            swprintf(dbuf, 16, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
            j += L"\",\"timeDisplay\":\"";
            j += buf;
            j += L"\",\"date\":\"";
            j += dbuf;
            j += L"\"}";
        }
        j += L"]";
    }
    // Bookmarks
    j += L",\"bookmarks\":[";
    for (size_t i = 0; i < m_bookmarks.size(); i++) {
        if (i) j += L',';
        j += L"{\"name\":\"";
        for (auto wc : m_bookmarks[i].name) {
            if (wc == L'"' || wc == L'\\') j += L'\\';
            j += wc;
        }
        j += L"\",\"url\":\"";
        for (auto wc : m_bookmarks[i].url) {
            if (wc == L'"' || wc == L'\\') j += L'\\';
            j += wc;
        }
        j += L"\"}";
    }
    j += L"]";
    // Status bar text
    j += L",\"status\":\"";
    for (auto wc : m_statusText) {
        if (wc == L'"' || wc == L'\\') j += L'\\';
        j += wc;
    }
    j += L"\"";
    // Active downloads
    j += L",\"downloads\":[";
    for (size_t i = 0; i < m_downloads.size(); i++) {
        if (i) j += L',';
        j += L"{\"name\":\"";
        for (auto wc : m_downloads[i].fileName) { if (wc == L'"' || wc == L'\\') j += L'\\'; j += wc; }
        j += L"\",\"received\":" + std::to_wstring(m_downloads[i].receivedBytes) +
            L",\"total\":" + std::to_wstring(m_downloads[i].totalBytes) +
            L",\"state\":" + std::to_wstring(
                m_downloads[i].state == 2 ? 1 : (m_downloads[i].state == 1 ? 3 : 0)) +
            L",\"path\":\"";
        for (auto wc : m_downloads[i].path) { if (wc == L'"' || wc == L'\\') j += L'\\'; j += wc; }
        j += L"\"}";
    }
    j += L"]";
    j += L",\"downloadFolder\":\"";
    for (auto wc : effectiveDownloadPath()) {
        if (wc == L'"' || wc == L'\\') j += L'\\';
        j += wc;
    }
    j += L"\",\"downloadsOpen\":" + std::wstring(m_downloadsOpen ? L"true" : L"false");
    j += L"}";
    postToChrome(j);
}
