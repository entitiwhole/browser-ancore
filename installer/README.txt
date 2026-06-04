AnCore Browser — установщик (Inno Setup 6)

1) Подготовка (один раз или после обновления redist):
   powershell -ExecutionPolicy Bypass -File prepare-installer.ps1 -DownloadRedist

2) Полная сборка установщика:
   powershell -ExecutionPolicy Bypass -File build-installer.ps1 -DownloadRedist

   Или вручную:
   - ..\build.ps1
   - prepare-installer.ps1 -DownloadRedist
   - "E:\Inno Setup 6\ISCC.exe" /Qp AnCoreBrowser.iss

3) Через GUI Inno Setup (Compil32.exe):
   File → Open → AnCoreBrowser.iss → Build → Compile
   (сначала выполните prepare-installer.ps1)

Результат: installer\output\AnCoreBrowser-Setup-1.0.0-x64.exe

В установку входят:
- AnCoreBrowser.exe
- папка resources\ (интерфейс, new tab, panel)
- по задачам: WebView2 Runtime и VC++ 2015–2022 x64 (если ещё не установлены)

Данные пользователя (история, закладки) — в %AppData%\AnCoreBrowser, не в папку программы.
