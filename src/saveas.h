#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

struct ICoreWebView2;

struct SaveFormat {
    std::wstring title;
    std::wstring pattern;
    std::wstring ext;
};

std::wstring extensionFromUrl(const std::wstring& url);
std::wstring fileNameFromUrl(const std::wstring& url);
std::wstring suggestedImageFileName(const std::wstring& url);
std::wstring suggestedDownloadFileName(const std::wstring& url);

bool isImageUrl(const std::wstring& url);
std::wstring imageUrlFromPageLink(const std::wstring& link);
std::wstring resolveDownloadUrl(const std::wstring& url);
bool isWebPageUrl(const std::wstring& url);
bool isPhotoGalleryUrl(const std::wstring& url);
bool isYandexImageViewerPage(const std::wstring& url);
bool hasImageFileExtension(const std::wstring& url);
bool isDirectImagePageUrl(const std::wstring& url);
std::vector<SaveFormat> formatsForImageUrl(const std::wstring& url);
std::vector<SaveFormat> formatsForMediaUrl(const std::wstring& url, bool isVideo);
std::vector<SaveFormat> formatsForFileUrl(const std::wstring& url);
std::vector<SaveFormat> formatsForPage();
std::vector<SaveFormat> formatsForSelectedText();

bool promptSaveFile(HWND owner, const std::vector<SaveFormat>& formats,
    const std::wstring& suggestedName, std::wstring& outPath);

bool downloadUrlToPath(const std::wstring& url, const std::wstring& destPath);
bool saveImageUrlAs(const std::wstring& imageUrl, const std::wstring& destPath);
void saveImageUrlAsWithWebView(ICoreWebView2* wv, const std::wstring& imageUrl,
    const std::wstring& destPath, std::function<void(bool success)> onComplete);
bool savePage(ICoreWebView2* wv, HWND owner);
