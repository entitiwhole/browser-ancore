#pragma once
#include <string>

struct ICoreWebView2;
struct ICoreWebView2ContextMenuRequestedEventArgs;
struct ICoreWebView2ContextMenuTarget;

class MainWindow;

std::wstring contextMenuInjectScript();
void handleContextMenuRequested(MainWindow* mw, ICoreWebView2* sender,
    ICoreWebView2ContextMenuRequestedEventArgs* args);
void handleContextMenuAction(MainWindow* mw, const std::wstring& action,
    ICoreWebView2* sender);
