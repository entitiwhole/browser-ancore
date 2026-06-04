# AnCore Browser - Сборка проекта

## Требования

1. **Visual Studio 2022** (или BuildTools) с C++ workload
   - Установи через Visual Studio Installer: "Разработка классических приложений на C++"
   - Включи: MSVC v143, Windows 10/11 SDK, C++ CMake tools

2. **Qt 6.7+** (Open Source)
   - Скачай: https://www.qt.io/download-open-source
   - Установи через Qt Online Installer:
     - Qt 6.7.x → MSVC 2022 64-bit
     - Компоненты: Qt WebEngine, Qt Widgets, Qt Network, Qt Sql

3. **CMake 3.20+**
   - Скачай: https://cmake.org/download/
   - При установке отметь "Add CMake to system PATH"

4. **Ninja** (опционально, для быстрой сборки)
   - `winget install Ninja-build.Ninja`

## Быстрая сборка

1. Установи все зависимости
2. Запусти `build.bat` (откроет окружение VS)

Или вручную:

```cmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Готовый `.exe` появится в `build\Release\AnCoreBrowser.exe`.

## Структура проекта

```
AnCoreBrowser/
├── src/
│   ├── main.cpp              # Точка входа
│   ├── mainwindow.h/cpp      # Главное окно
│   ├── webtab.h/cpp          # Вкладка с QWebEngine
│   ├── addressbar.h/cpp      # Адресная строка
│   ├── tabwidget.h/cpp       # Управление вкладками
│   ├── bookmarkmanager.h/cpp # Закладки
│   ├── downloadmanager.h/cpp # Загрузки
│   ├── historymanager.h/cpp  # История
│   ├── adblocker.h/cpp       # Блокировка рекламы
│   ├── settingsdialog.h/cpp  # Настройки
│   └── browsercore.h/cpp     # Ядро браузера
├── resources/
│   ├── html/newtab.html      # Страница новой вкладки
│   └── html/blockpage.html   # Страница блокировки
└── CMakeLists.txt
```
