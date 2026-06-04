#include "saveas.h"
#include "browsercore.h"
#include <WebView2.h>
#include <wrl.h>
#include <commdlg.h>
#include <urlmon.h>
#include <shlwapi.h>
#include <algorithm>
#include <vector>
#include <gdiplus.h>
#include <wincrypt.h>

#pragma comment(lib, "shlwapi.lib")

using Microsoft::WRL::Callback;

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "crypt32.lib")

namespace {

ULONG_PTR g_gdiplusToken = 0;
bool g_gdiplusInit = false;

void ensureGdiplus() {
    if (!g_gdiplusInit) {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr);
        g_gdiplusInit = true;
    }
}

std::wstring toLower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

int getEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (!size) return -1;
    std::vector<BYTE> buf(size);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, size, codecs);
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(codecs[i].MimeType, format) == 0) {
            *pClsid = codecs[i].Clsid;
            return i;
        }
    }
    return -1;
}

const wchar_t* mimeForExt(const std::wstring& ext) {
    if (ext == L".png") return L"image/png";
    if (ext == L".jpg" || ext == L".jpeg") return L"image/jpeg";
    if (ext == L".gif") return L"image/gif";
    if (ext == L".bmp") return L"image/bmp";
    if (ext == L".tif" || ext == L".tiff") return L"image/tiff";
    if (ext == L".webp") return L"image/webp";
    return nullptr;
}

void addFmt(std::vector<SaveFormat>& out, const wchar_t* title, const wchar_t* pattern, const wchar_t* ext) {
    for (auto& f : out) if (f.ext == ext) return;
    out.push_back({title, pattern, ext});
}

} // namespace

std::wstring extensionFromUrl(const std::wstring& url) {
    std::wstring path = url;
    size_t q = path.find(L'?');
    if (q != std::wstring::npos) path = path.substr(0, q);
    size_t slash = path.rfind(L'/');
    if (slash == std::wstring::npos) slash = path.rfind(L'\\');
    std::wstring name = (slash != std::wstring::npos) ? path.substr(slash + 1) : path;
    size_t dot = name.rfind(L'.');
    if (dot == std::wstring::npos || dot == name.size() - 1) return L"";
    return toLower(name.substr(dot));
}

namespace {

bool writeBytesToFile(const std::wstring& path, const BYTE* data, DWORD size) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    fwrite(data, 1, size, f);
    fclose(f);
    return true;
}

bool saveDataUrlImage(const std::wstring& dataUrl, const std::wstring& destPath) {
    size_t comma = dataUrl.find(L',');
    if (comma == std::wstring::npos) return false;
    std::wstring payload = dataUrl.substr(comma + 1);
    DWORD size = 0;
    if (!CryptStringToBinaryW(payload.c_str(), (DWORD)payload.size(), CRYPT_STRING_BASE64,
            nullptr, &size, nullptr, nullptr) || size == 0)
        return false;
    std::vector<BYTE> bytes(size);
    if (!CryptStringToBinaryW(payload.c_str(), (DWORD)payload.size(), CRYPT_STRING_BASE64,
            bytes.data(), &size, nullptr, nullptr))
        return false;

    wchar_t tempPath[MAX_PATH] = {};
    wchar_t tempFile[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tempPath) || !GetTempFileNameW(tempPath, L"anc", 0, tempFile))
        return false;
    if (!writeBytesToFile(tempFile, bytes.data(), size)) {
        DeleteFileW(tempFile);
        return false;
    }

    std::wstring destExt = ::extensionFromUrl(destPath);
    std::wstring mimeExt = L".png";
    if (dataUrl.find(L"image/jpeg") != std::wstring::npos || dataUrl.find(L"image/jpg") != std::wstring::npos)
        mimeExt = L".jpg";
    else if (dataUrl.find(L"image/gif") != std::wstring::npos) mimeExt = L".gif";
    else if (dataUrl.find(L"image/webp") != std::wstring::npos) mimeExt = L".webp";
    else if (dataUrl.find(L"image/bmp") != std::wstring::npos) mimeExt = L".bmp";

    if (destExt.empty()) destExt = L".png";
    bool ok = false;
    if (destExt == mimeExt) {
        ok = MoveFileExW(tempFile, destPath.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
        if (!ok) ok = CopyFileW(tempFile, destPath.c_str(), FALSE) != FALSE;
    } else {
        ensureGdiplus();
        Gdiplus::Bitmap bmp(tempFile);
        if (bmp.GetLastStatus() == Gdiplus::Ok) {
            const wchar_t* mime = mimeForExt(destExt);
            CLSID clsid = {};
            if (mime && getEncoderClsid(mime, &clsid) >= 0)
                ok = bmp.Save(destPath.c_str(), &clsid) == Gdiplus::Ok;
        }
    }
    DeleteFileW(tempFile);
    return ok;
}

bool saveImageUrlAsImpl(const std::wstring& imageUrl, const std::wstring& destPath) {
    if (imageUrl.rfind(L"data:", 0) == 0)
        return saveDataUrlImage(imageUrl, destPath);

    wchar_t tempPath[MAX_PATH] = {};
    wchar_t tempFile[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tempPath) || !GetTempFileNameW(tempPath, L"anc", 0, tempFile))
        return false;

    if (!::downloadUrlToPath(imageUrl, tempFile)) {
        DeleteFileW(tempFile);
        return false;
    }

    std::wstring destExt = ::extensionFromUrl(destPath);
    std::wstring srcExt = ::extensionFromUrl(imageUrl);
    if (destExt.empty()) destExt = L".png";

    if (destExt == srcExt || srcExt.empty()) {
        BOOL ok = MoveFileExW(tempFile, destPath.c_str(), MOVEFILE_REPLACE_EXISTING);
        if (!ok) ok = CopyFileW(tempFile, destPath.c_str(), FALSE);
        DeleteFileW(tempFile);
        return ok;
    }

    const wchar_t* mime = mimeForExt(destExt);
    if (!mime) {
        DeleteFileW(tempFile);
        return ::downloadUrlToPath(imageUrl, destPath);
    }

    ensureGdiplus();
    Gdiplus::Bitmap bmp(tempFile);
    if (bmp.GetLastStatus() != Gdiplus::Ok) {
        DeleteFileW(tempFile);
        return false;
    }
    CLSID clsid = {};
    if (getEncoderClsid(mime, &clsid) < 0) {
        DeleteFileW(tempFile);
        return false;
    }
    bool ok = bmp.Save(destPath.c_str(), &clsid) == Gdiplus::Ok;
    DeleteFileW(tempFile);
    return ok;
}

} // namespace

std::wstring decodeUrlParam(const std::wstring& value) {
    if (value.empty()) return {};
    std::vector<wchar_t> buf(value.begin(), value.end());
    buf.push_back(L'\0');
    DWORD cch = (DWORD)buf.size();
    if (UrlUnescapeW(buf.data(), nullptr, &cch, URL_UNESCAPE_INPLACE) == S_OK)
        return std::wstring(buf.data());
    return value;
}

bool hasImageFileExtension(const std::wstring& url) {
    std::wstring e = extensionFromUrl(url);
    return e == L".png" || e == L".jpg" || e == L".jpeg" || e == L".gif" ||
        e == L".webp" || e == L".bmp" || e == L".svg" || e == L".ico" ||
        e == L".avif" || e == L".tif" || e == L".tiff" || e == L".jfif";
}

bool isDirectImagePageUrl(const std::wstring& url) {
    if (url.empty()) return false;
    if (url.rfind(L"data:image/", 0) == 0) return true;
    if (url.rfind(L"blob:", 0) == 0) return true;
    if (url.rfind(L"http://", 0) != 0 && url.rfind(L"https://", 0) != 0)
        return false;
    return hasImageFileExtension(url);
}

bool isImageUrl(const std::wstring& url) {
    if (url.empty()) return false;
    if (url.rfind(L"data:image/", 0) == 0) return true;
    if (hasImageFileExtension(url)) return true;
    std::wstring u = toLower(url);
    if (u.find(L"gstatic.com/") != std::wstring::npos) return true;
    if (u.find(L"googleusercontent.com/") != std::wstring::npos) return true;
    if (u.find(L"ggpht.com/") != std::wstring::npos) return true;
    if (u.find(L"ytimg.com/") != std::wstring::npos) return true;
    if (u.find(L"pinimg.com/") != std::wstring::npos) return true;
    if (u.find(L"fbcdn.net/") != std::wstring::npos) return true;
    if (u.find(L"cdninstagram.com/") != std::wstring::npos) return true;
    if (u.find(L"/images") != std::wstring::npos && u.find(L"google.") != std::wstring::npos)
        return true;
    if (u.find(L"imgurl=") != std::wstring::npos || u.find(L"img_url=") != std::wstring::npos ||
        u.find(L"mediaurl=") != std::wstring::npos)
        return true;
    if (u.find(L"avatars.mds.yandex.net") != std::wstring::npos) return true;
    if (u.find(L"yandex.net/get-") != std::wstring::npos) return true;
    if (u.find(L"imgs.yandex") != std::wstring::npos) return true;
    if (u.find(L"im0-tub") != std::wstring::npos && u.find(L"yandex") != std::wstring::npos) return true;
    if (u.find(L"vecteezy.com") != std::wstring::npos) return true;
    if (u.find(L"i.imgur.com") != std::wstring::npos) return true;
    if (u.find(L"wikimedia.org") != std::wstring::npos) return true;
    if (u.find(L"/previews/") != std::wstring::npos && extensionFromUrl(url).empty()) {
        if (u.find(L".png") != std::wstring::npos || u.find(L".jpg") != std::wstring::npos ||
            u.find(L".webp") != std::wstring::npos)
            return true;
    }
    return false;
}

static std::wstring queryParamValue(const std::wstring& link, const wchar_t* key) {
    std::wstring lk = toLower(link);
    std::wstring lkKey = toLower(std::wstring(key));
    size_t p = lk.find(lkKey);
    if (p == std::wstring::npos) return {};
    p += lkKey.size();
    size_t e = link.find(L'&', p);
    std::wstring raw = (e == std::wstring::npos) ? link.substr(p) : link.substr(p, e - p);
    return decodeUrlParam(raw);
}

std::wstring imageUrlFromPageLink(const std::wstring& link) {
    for (const wchar_t* key :
         {L"imgurl=", L"img_url=", L"mediaurl=", L"orig_url=", L"original_url=", L"url="}) {
        std::wstring decoded = queryParamValue(link, key);
        if (!decoded.empty() && isImageUrl(decoded)) return decoded;
    }
    std::wstring u = toLower(link);
    if (u.find(L"yandex.") != std::wstring::npos && u.find(L"rpt=imageview") != std::wstring::npos) {
        std::wstring decoded = queryParamValue(link, L"url=");
        if (!decoded.empty() && (isImageUrl(decoded) || decoded.rfind(L"http", 0) == 0))
            return decoded;
    }
    return {};
}

std::wstring resolveDownloadUrl(const std::wstring& url) {
    std::wstring img = imageUrlFromPageLink(url);
    return img.empty() ? url : img;
}

bool isWebPageUrl(const std::wstring& url) {
    if (url.empty()) return false;
    if (url.rfind(L"http://", 0) != 0 && url.rfind(L"https://", 0) != 0) return false;
    if (isDirectImagePageUrl(url) || isImageUrl(url)) return false;
    return extensionFromUrl(url).empty();
}

bool isYandexImageViewerPage(const std::wstring& url) {
    std::wstring u = toLower(url);
    if (u.find(L"yandex.") == std::wstring::npos) return false;
    if (u.find(L"/images") == std::wstring::npos && u.find(L"images.yandex") == std::wstring::npos)
        return false;
    if (u.find(L"img_url=") != std::wstring::npos || u.find(L"imgurl=") != std::wstring::npos)
        return true;
    if (u.find(L"rpt=imageview") != std::wstring::npos) return true;
    if (u.find(L"/images/touch") != std::wstring::npos) return true;
    return false;
}

bool isPhotoGalleryUrl(const std::wstring& url) {
    std::wstring u = toLower(url);
    if (u.find(L"google.com/imgres") != std::wstring::npos) return true;
    if (u.find(L"tbm=isch") != std::wstring::npos) return true;
    if (u.find(L"yandex.") != std::wstring::npos &&
        (u.find(L"/images") != std::wstring::npos || u.find(L"images/search") != std::wstring::npos ||
         u.find(L"images/touch") != std::wstring::npos || u.find(L"images.yandex") != std::wstring::npos))
        return true;
    if (u.find(L"shutterstock.") != std::wstring::npos) return true;
    if (u.find(L"stock.adobe.") != std::wstring::npos) return true;
    if (u.find(L"gettyimages.") != std::wstring::npos) return true;
    if (u.find(L"istockphoto.") != std::wstring::npos) return true;
    if (u.find(L"depositphotos.") != std::wstring::npos) return true;
    if (u.find(L"123rf.") != std::wstring::npos) return true;
    if (u.find(L"unsplash.com/photos") != std::wstring::npos) return true;
    if (u.find(L"pexels.com/photo") != std::wstring::npos) return true;
    if (u.find(L"/image-photo") != std::wstring::npos) return true;
    if (u.find(L"/photo/") != std::wstring::npos) return true;
    return false;
}

std::wstring suggestedDownloadFileName(const std::wstring& url) {
    std::wstring resolved = resolveDownloadUrl(url);
    if (isImageUrl(resolved)) return suggestedImageFileName(resolved);
    std::wstring name = fileNameFromUrl(url);
    if (extensionFromUrl(name).empty() && extensionFromUrl(url).empty()) {
        if (name.find(L'.') == std::wstring::npos)
            name += L".html";
    }
    return name;
}

std::wstring fileNameFromUrl(const std::wstring& url) {
    std::wstring path = url;
    size_t q = path.find(L'?');
    if (q != std::wstring::npos) path = path.substr(0, q);
    size_t slash = path.rfind(L'/');
    if (slash == std::wstring::npos) slash = path.rfind(L'\\');
    std::wstring name = (slash != std::wstring::npos) ? path.substr(slash + 1) : path;
    if (name.empty()) name = L"download";
    return name;
}

std::wstring suggestedImageFileName(const std::wstring& url) {
    if (url.rfind(L"data:image/", 0) == 0) {
        if (url.find(L"image/png") != std::wstring::npos) return L"image.png";
        if (url.find(L"image/jpeg") != std::wstring::npos || url.find(L"image/jpg") != std::wstring::npos)
            return L"image.jpg";
        if (url.find(L"image/gif") != std::wstring::npos) return L"image.gif";
        if (url.find(L"image/webp") != std::wstring::npos) return L"image.webp";
        return L"image.png";
    }
    std::wstring ext = extensionFromUrl(url);
    std::wstring name = fileNameFromUrl(url);
    if (ext.empty()) {
        if (name == L"download" || name.find(L'.') == std::wstring::npos)
            name = L"image.png";
        else
            name += L".png";
    }
    return name;
}

std::vector<SaveFormat> formatsForImageUrl(const std::wstring& url) {
    std::wstring ext = extensionFromUrl(url);
    std::vector<SaveFormat> f;
    if (ext == L".png") {
        addFmt(f, L"PNG (*.png)", L"*.png", L".png");
        addFmt(f, L"BMP (*.bmp)", L"*.bmp", L".bmp");
    } else if (ext == L".jpg" || ext == L".jpeg") {
        addFmt(f, L"JPEG (*.jpg;*.jpeg)", L"*.jpg;*.jpeg", L".jpg");
    } else if (ext == L".gif") {
        addFmt(f, L"GIF (*.gif)", L"*.gif", L".gif");
    } else if (ext == L".bmp") {
        addFmt(f, L"BMP (*.bmp)", L"*.bmp", L".bmp");
    } else if (ext == L".webp") {
        addFmt(f, L"WebP (*.webp)", L"*.webp", L".webp");
    } else if (ext == L".tif" || ext == L".tiff") {
        addFmt(f, L"TIFF (*.tif;*.tiff)", L"*.tif;*.tiff", L".tif");
    } else {
        addFmt(f, L"PNG (*.png)", L"*.png", L".png");
        addFmt(f, L"JPEG (*.jpg;*.jpeg)", L"*.jpg;*.jpeg", L".jpg");
        addFmt(f, L"WebP (*.webp)", L"*.webp", L".webp");
        addFmt(f, L"GIF (*.gif)", L"*.gif", L".gif");
        addFmt(f, L"BMP (*.bmp)", L"*.bmp", L".bmp");
    }
    return f;
}

std::vector<SaveFormat> formatsForMediaUrl(const std::wstring& url, bool isVideo) {
    std::wstring ext = extensionFromUrl(url);
    std::vector<SaveFormat> f;
    if (isVideo) {
        if (ext == L".mp4") addFmt(f, L"MP4 (*.mp4)", L"*.mp4", L".mp4");
        else if (ext == L".webm") addFmt(f, L"WebM (*.webm)", L"*.webm", L".webm");
        else if (ext == L".mkv") addFmt(f, L"MKV (*.mkv)", L"*.mkv", L".mkv");
        else {
            addFmt(f, L"MP4 (*.mp4)", L"*.mp4", L".mp4");
            addFmt(f, L"WebM (*.webm)", L"*.webm", L".webm");
        }
    } else {
        if (ext == L".mp3") addFmt(f, L"MP3 (*.mp3)", L"*.mp3", L".mp3");
        else if (ext == L".wav") addFmt(f, L"WAV (*.wav)", L"*.wav", L".wav");
        else if (ext == L".ogg") addFmt(f, L"Ogg (*.ogg)", L"*.ogg", L".ogg");
        else if (ext == L".m4a") addFmt(f, L"M4A (*.m4a)", L"*.m4a", L".m4a");
        else {
            addFmt(f, L"MP3 (*.mp3)", L"*.mp3", L".mp3");
            addFmt(f, L"WAV (*.wav)", L"*.wav", L".wav");
        }
    }
    return f;
}

std::vector<SaveFormat> formatsForFileUrl(const std::wstring& url) {
    std::wstring ext = extensionFromUrl(url);
    std::vector<SaveFormat> f;
    if (ext.empty()) {
        addFmt(f, L"All files (*.*)", L"*.*", L"");
        return f;
    }
    if (ext == L".pdf") addFmt(f, L"PDF (*.pdf)", L"*.pdf", L".pdf");
    else if (ext == L".docx") addFmt(f, L"Word (*.docx)", L"*.docx", L".docx");
    else if (ext == L".doc") addFmt(f, L"Word 97-2003 (*.doc)", L"*.doc", L".doc");
    else if (ext == L".xlsx") addFmt(f, L"Excel (*.xlsx)", L"*.xlsx", L".xlsx");
    else if (ext == L".xls") addFmt(f, L"Excel 97-2003 (*.xls)", L"*.xls", L".xls");
    else if (ext == L".csv") addFmt(f, L"CSV (*.csv)", L"*.csv", L".csv");
    else if (ext == L".txt") addFmt(f, L"Text (*.txt)", L"*.txt", L".txt");
    else if (ext == L".zip") addFmt(f, L"ZIP (*.zip)", L"*.zip", L".zip");
    else if (ext == L".rar") addFmt(f, L"RAR (*.rar)", L"*.rar", L".rar");
    else if (ext == L".7z") addFmt(f, L"7-Zip (*.7z)", L"*.7z", L".7z");
    else if (ext == L".json") addFmt(f, L"JSON (*.json)", L"*.json", L".json");
    else if (ext == L".xml") addFmt(f, L"XML (*.xml)", L"*.xml", L".xml");
    else if (ext == L".html" || ext == L".htm") addFmt(f, L"HTML (*.html;*.htm)", L"*.html;*.htm", ext.c_str());
    else {
        std::wstring title = L"File (*" + ext + L")";
        std::wstring pat = L"*" + ext;
        addFmt(f, title.c_str(), pat.c_str(), ext.c_str());
    }
    return f;
}

std::vector<SaveFormat> formatsForPage() {
    return {
        {L"Web page (*.html;*.htm)", L"*.html;*.htm", L".html"},
        {L"Web archive (*.mhtml)", L"*.mhtml", L".mhtml"},
    };
}

std::vector<SaveFormat> formatsForSelectedText() {
    return {{L"Text (*.txt)", L"*.txt", L".txt"}};
}

bool promptSaveFile(HWND owner, const std::vector<SaveFormat>& formats,
    const std::wstring& suggestedName, std::wstring& outPath) {
    if (formats.empty()) return false;
    std::wstring filter;
    for (auto& f : formats) {
        filter += f.title + L'\0' + f.pattern + L'\0';
    }
    filter += L'\0';

    wchar_t file[MAX_PATH] = {};
    std::wstring name = suggestedName;
    size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos) name = name.substr(0, dot);
    if (!formats[0].ext.empty())
        name += formats[0].ext;
    wcsncpy_s(file, name.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = formats[0].ext.empty() ? nullptr : formats[0].ext.c_str() + 1;
    if (!GetSaveFileNameW(&ofn)) return false;
    outPath = file;
    return true;
}

bool downloadUrlToPath(const std::wstring& url, const std::wstring& destPath) {
    return SUCCEEDED(URLDownloadToFileW(nullptr, url.c_str(), destPath.c_str(), 0, nullptr));
}

namespace {

std::wstring unquoteScriptResult(LPCWSTR result) {
    if (!result) return {};
    std::wstring s = result;
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"')
        s = s.substr(1, s.size() - 2);
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            wchar_t n = s[++i];
            if (n == L'n') out += L'\n';
            else if (n == L'r') out += L'\r';
            else if (n == L't') out += L'\t';
            else if (n == L'"') out += L'"';
            else if (n == L'\\') out += L'\\';
            else { out += L'\\'; out += n; }
        } else {
            out += s[i];
        }
    }
    return out;
}

bool writeUtf8File(const std::wstring& path, const std::wstring& text) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    fwrite(bom, 1, 3, f);
    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
        nullptr, 0, nullptr, nullptr);
    if (len > 0) {
        std::string utf8(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
            utf8.data(), len, nullptr, nullptr);
        fwrite(utf8.data(), 1, len, f);
    }
    fclose(f);
    return true;
}

void notifySaved(const std::wstring& path) {
    size_t pos = path.rfind(L'\\');
    std::wstring fname = (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
    auto bc = BrowserCore::instance();
    if (!bc) return;
    DownloadInfo di;
    di.fileName = fname;
    di.path = path;
    di.state = 2; // COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED
    di.receivedBytes = di.totalBytes = 0;
    bc->addDownload(di);
    bc->setStatusText(L"Сохранено: " + fname);
}

static const wchar_t kSavePageScript[] =
    L"(function(){"
    L"var h=document.documentElement.cloneNode(true);"
    L"var hd=h.querySelector('head');"
    L"if(!hd){hd=document.createElement('head');h.insertBefore(hd,h.firstChild);}"
    L"if(!hd.querySelector('meta[charset]')){"
    L"var m=document.createElement('meta');m.setAttribute('charset','utf-8');"
    L"hd.insertBefore(m,hd.firstChild);}"
    L"var u=document.baseURI||location.href;"
    L"if(!hd.querySelector('base')){"
    L"var b=document.createElement('base');b.href=u;hd.insertBefore(b,hd.firstChild);}"
    L"return '<!DOCTYPE html>\\n'+h.outerHTML;"
    L"})()";

bool savePageHtmlFallback(ICoreWebView2* wv, HWND owner) {
    if (!wv) return false;
    std::wstring path;
    if (!promptSaveFile(owner, formatsForPage(), L"page.html", path))
        return false;
    if (extensionFromUrl(path) == L".mhtml") {
        BrowserCore::instance()->setStatusText(
            L"Для MHTML нужен WebView2 Runtime 1.0.2535+ (диалог «Сохранить как» Edge)");
        return false;
    }

    wv->ExecuteScript(
        kSavePageScript,
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [path](HRESULT hr, LPCWSTR result) -> HRESULT {
                if (FAILED(hr) || !result) {
                    BrowserCore::instance()->setStatusText(L"Не удалось получить HTML страницы");
                    return S_OK;
                }
                std::wstring html = unquoteScriptResult(result);
                if (writeUtf8File(path, html)) {
                    notifySaved(path);
                } else {
                    BrowserCore::instance()->setStatusText(L"Ошибка записи файла");
                }
                return S_OK;
            }).Get());
    return true;
}

} // namespace

bool savePage(ICoreWebView2* wv, HWND owner) {
    if (!wv) return false;
    ICoreWebView2_25* wv25 = nullptr;
    if (SUCCEEDED(wv->QueryInterface(IID_ICoreWebView2_25, (void**)&wv25))) {
        HRESULT hr = wv25->ShowSaveAsUI(
            Callback<ICoreWebView2ShowSaveAsUICompletedHandler>(
                [](HRESULT errorCode, COREWEBVIEW2_SAVE_AS_UI_RESULT result) -> HRESULT {
                    if (FAILED(errorCode) || result != COREWEBVIEW2_SAVE_AS_UI_RESULT_SUCCESS) {
                        if (result == COREWEBVIEW2_SAVE_AS_UI_RESULT_CANCELLED)
                            return S_OK;
                        BrowserCore::instance()->setStatusText(L"Сохранение страницы отменено или не удалось");
                    } else {
                        BrowserCore::instance()->setStatusText(L"Страница сохранена");
                    }
                    return S_OK;
                }).Get());
        wv25->Release();
        if (SUCCEEDED(hr)) return true;
    }
    return savePageHtmlFallback(wv, owner);
}

bool saveImageUrlAs(const std::wstring& imageUrl, const std::wstring& destPath) {
    return saveImageUrlAsImpl(imageUrl, destPath);
}

namespace {

std::wstring escapeForJsString(const std::wstring& s) {
    std::wstring o;
    o.reserve(s.size() + 8);
    for (wchar_t c : s) {
        if (c == L'\\') o += L"\\\\";
        else if (c == L'\'') o += L"\\'";
        else if (c == L'\n') o += L"\\n";
        else if (c == L'\r') o += L"\\r";
        else o += c;
    }
    return o;
}

std::wstring buildFetchImageDataUrlScript(const std::wstring& imageUrl) {
    static const wchar_t kFetchPrefix[] =
        LR"JS((async function(u){
try{
var r=await fetch(u,{mode:'cors',credentials:'omit',cache:'no-store',redirect:'follow'});
if(!r.ok)return '';
var b=await r.blob();
return await new Promise(function(res){
var fr=new FileReader();
fr.onload=function(){res(fr.result||'');};
fr.onerror=function(){res('');};
fr.readAsDataURL(b);
});
}catch(e){return '';}
})JS";
    return std::wstring(kFetchPrefix) + L"('" + escapeForJsString(imageUrl) + L"')";
}

bool isHttpImageUrl(const std::wstring& url) {
    return url.rfind(L"https://", 0) == 0 || url.rfind(L"http://", 0) == 0;
}

} // namespace

void saveImageUrlAsWithWebView(ICoreWebView2* wv, const std::wstring& imageUrl,
    const std::wstring& destPath, std::function<void(bool success)> onComplete) {
    if (!onComplete) return;
    if (imageUrl.empty() || destPath.empty()) {
        onComplete(false);
        return;
    }
    if (imageUrl.rfind(L"data:", 0) == 0 || !wv || !isHttpImageUrl(imageUrl)) {
        onComplete(saveImageUrlAsImpl(imageUrl, destPath));
        return;
    }

    std::wstring script = buildFetchImageDataUrlScript(imageUrl);
    wv->ExecuteScript(
        script.c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [imageUrl, destPath, onComplete](HRESULT hr, LPCWSTR result) -> HRESULT {
                bool ok = false;
                if (SUCCEEDED(hr) && result) {
                    std::wstring data = unquoteScriptResult(result);
                    if (data.rfind(L"data:image/", 0) == 0)
                        ok = saveImageUrlAsImpl(data, destPath);
                }
                if (!ok)
                    ok = saveImageUrlAsImpl(imageUrl, destPath);
                onComplete(ok);
                return S_OK;
            }).Get());
}
