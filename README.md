# AnCore Browser

Кастомный браузер для Windows на **Microsoft Edge WebView2** (движок Chromium). Собственная оболочка: вкладки, адресная строка, новая вкладка, закладки, история и загрузки — без Electron и без полного дерева Chromium в репозитории.

![Platform](https://img.shields.io/badge/platform-Windows%2010%2B-blue)
![Build](https://img.shields.io/badge/build-CMake%20%2B%20MSVC-purple)
![License](https://img.shields.io/badge/license-MIT-green)

## Возможности

- **Вкладки** — создание, закрытие, перетаскивание, закрепление, восстановление закрытой (`Ctrl+Shift+T`)
- **Omnibox** — URL и поиск, подсказки по словам и **частым запросам из истории**
- **Новая вкладка** — поиск, быстрые виджеты (перетаскивание), частые сайты, favicon с сайтов
- **Закладки** — добавление, панель, импорт из HTML
- **История** — поисковые запросы и посещения, счётчик повторов для подсказок
- **Загрузки** — панель, отмена, открытие файла и папки
- **Масштаб** — на сайт, `Ctrl` + колесо мыши, горячие клавиши
- **Блокировка рекламы** — встроенные правила + свой список `adblock.txt`
- **Контекстное меню** — ссылки, картинки, сохранение изображений
- **Поиск на странице**, **DevTools**, полноэкранный режим
- **Установщик** — Inno Setup, WebView2 Runtime и VC++ Redistributable при необходимости

> Нет расширений Chrome, синхронизации между устройствами и профилей — данные хранятся локально на ПК.

## Требования

| Компонент | Версия |
|-----------|--------|
| ОС | Windows 10/11 x64 |
| Сборка | Visual Studio 2022 (или Build Tools) с **Desktop development with C++** |
| CMake | 3.20+ |
| Runtime | [WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/) (Evergreen) |
| Установщик (опционально) | [Inno Setup 6](https://jrsoftware.org/isinfo.php) |

Для запуска готового `AnCoreBrowser.exe` также нужен **Visual C++ 2015–2022 Redistributable (x64)** — установщик может поставить его автоматически.

## Сборка из исходников

```powershell
git clone https://github.com/entitiwhole/browser-ancore.git
cd browser-ancore
.\build.ps1
```

Готовый файл:

```text
build\Release\AnCoreBrowser.exe
```

Рядом с exe должна лежать папка `resources\` — CMake копирует её при сборке автоматически.

### Ручная сборка (CMake)

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

### Запуск без установки

```powershell
.\build\Release\AnCoreBrowser.exe
```

## Установщик (Inno Setup)

```powershell
cd installer
.\prepare-installer.ps1 -DownloadRedist   # один раз: скачивает WebView2 и VC++ redist
.\build-installer.ps1 -DownloadRedist     # сборка + compile .iss
```

Результат: `installer\output\AnCoreBrowser-Setup-1.0.0-x64.exe`

Подробнее: [installer/README.txt](installer/README.txt).

## Структура проекта

```text
browser-ancore/
├── src/                    # C++: MainWindow, BrowserCore, меню, сохранение файлов
│   ├── mainwindow.cpp      # WebView2, вкладки, chrome + content
│   ├── browsercore.cpp     # История, закладки, omnibox, настройки
│   └── AnCoreBrowser.rc    # Иконка приложения
├── resources/html/         # UI оболочки (HTML/CSS/JS → IPC в C++)
│   ├── chrome.html         # Панель вкладок и адресная строка
│   ├── newtab.html         # Новая вкладка
│   └── panel.html          # Настройки, история, закладки, загрузки
├── installer/              # Inno Setup
├── WebView2LoaderStatic.lib
├── CMakeLists.txt
└── build.ps1
```

## Данные пользователя

Все настройки и профиль — в каталоге:

```text
%AppData%\AnCoreBrowser\
```

Там же: `history.txt`, `bookmarks.txt`, `settings.txt`, `session.txt`, `widgets.json`, кэш favicon и т.д. При удалении браузера эта папка **не** удаляется автоматически.

## Горячие клавиши (основные)

| Клавиши | Действие |
|---------|----------|
| `Ctrl+T` | Новая вкладка |
| `Ctrl+W` | Закрыть вкладку |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Следующая / предыдущая вкладка |
| `Ctrl+L` | Фокус в адресную строку |
| `Ctrl+D` | Закладка |
| `Ctrl+Shift+T` | Восстановить закрытую вкладку |
| `Ctrl+0` / `Ctrl+-` | Масштаб |
| `F5` | Обновить |
| `F12` | DevTools |

## Лицензия

[MIT](LICENSE) © [entitiwhole](https://github.com/entitiwhole)

## Ссылки

- Репозиторий: https://github.com/entitiwhole/browser-ancore
- WebView2: https://learn.microsoft.com/microsoft-edge/webview2/
