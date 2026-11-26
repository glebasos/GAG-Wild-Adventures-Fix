# GAG Modern

Патч для «ГЭГ: Отвязное приключение» (ZES't Corp. / Auric Vision, 1997, Russian Edition 2.51), чтобы игра нормально работала в Windows 10 и 11:

- **Без режима совместимости и без 16-битного цвета.** Рабочий стол остаётся в обычном 32-битном цвете, разрешение экрана не меняется.
- **Настоящий полный экран** (borderless) в родном разрешении монитора, картинка 640×480 масштабируется с сохранением пропорций 4:3.
- **Оконный режим** любого размера: по умолчанию 2× / 3× (самый большой целый масштаб, который помещается), окно можно растягивать и разворачивать.
- **Alt+Enter** переключает полный экран и окно. Кнопка игры «Полный экран / Вид в окне» тоже работает.
- Мышь корректно попадает в нужные места при любом масштабе.
- Игра больше не сворачивается сама, когда теряет фокус (старая проблема «уходит в трей»).
- Чёткие пиксели (nearest) или сглаживание, целочисленный масштаб или растягивание. Всё это настраивается в `gagfix.ini`.
- Сохранения (`GAG000.GSF`…, `AutoSave.cdf`) лежат прямо в папке игры.
- Игра больше не пишет в `HKLM` (для этого нужны права администратора). Её настройки сохраняются в `gagfix.ini`, сохранения лежат в папке игры. Записи установщика в реестре не нужны.

[English below](#english)

## Установка

1. Скопируйте папку `GAG_Modern` в папку с игрой (туда, где лежат `Gag01.cdf`, `Gag02.cdf` и `Gag_patched.exe` или `Gag.exe`).
2. Запустите `GAG_Modern\install.bat`. Чтобы заодно создать ярлык на рабочем столе, запустите `install.bat -Shortcut`.
3. Играйте через **`GagModern.exe`**. Режим совместимости для него ставить **не нужно**.

Установщик не трогает файлы игры. Он только создаёт рядом `GagModern.exe` (копию exe с одним изменённым байтовым полем), `gagfx.dll` и `gagfix.ini`.

Поддерживаемые exe:

| Файл | Версия | SHA-256 |
|---|---|---|
| `Gag_patched.exe` | 2.51 RU, распространённый no-CD патч | `CC069AEE…A7786D` |
| `Gag.exe` | 2.51 RU, оригинал с диска (нужен CD в приводе) | `5E74C0A4…6A4FC8` |

### Сохранения

Сохранения (`AutoSave.cdf` и сохранения, сделанные из игры) теперь лежат в папке игры. Старые сохранения лежали в папке, указанной установщиком в реестре (`HKLM\SOFTWARE\WOW6432Node\ZES't Corp.\GAG`, параметр `path`). Если они нужны, скопируйте их оттуда в папку игры.

### Известные мелочи

- Окна сохранения и загрузки (стандартные диалоги Windows 1997 года) на мониторах с масштабированием 150–200% выглядят мелковато, но полностью работают.
- Звук идёт через обычный `winmm.dll`, без изменений.

### Удаление

Запустите `uninstall.ps1`. Он удалит `GagModern.exe` и `gagfx.dll`. С ключом `-All` будет удалён и `gagfix.ini`.

## Настройки (`gagfix.ini`)

| Параметр | Значения |
|---|---|
| `mode` | `fullscreen` / `windowed` |
| `window_scale` | `0` = авто, `1` = 640×480, `2` = 1280×960, … |
| `aspect` | `keep` (4:3), `integer` (только целый масштаб), `stretch` |
| `filter` | `nearest` (чёткие пиксели) / `smooth` |
| `alt_enter` | `1` / `0` |
| `log` (секция `[debug]`) | `1` = писать `gagfix.log` |

---

## English

A Windows 10/11 fix for the Russian adventure game *GAG* (1997).

The game draws through GDI into a 640×480 child window and refuses to start unless the desktop is in 256-colour or 16-bit mode. Until now the only workaround was compatibility mode with 16-bit colour (or Win98/WinNT tricks), which gives a tiny, blurry 640×480 window or a real display-mode switch.

`gagfx.dll` is loaded in place of `WINMM.dll` (the exe's import is renamed; the seven `waveOut*`/`timeGetTime` imports are forwarded to the real winmm). It then patches the exe's import table:

| Hooked | Why |
|---|---|
| `GetDeviceCaps`, `EnumDisplaySettingsA` | report 16 bpp, so the game keeps its native RGB565 renderer; GDI converts to the 32-bit desktop |
| `ChangeDisplaySettingsA` | never changes the real mode; the game's own Full screen / Window option chooses borderless fullscreen or the scaled window |
| `BitBlt`, `StretchBlt`, `PatBlt` | output aimed at the game's picture window is captured into a 640×480 back buffer |
| presenter thread (`DwmFlush`) | presents the back buffer once per compositor frame, scaled and letterboxed |
| `CreateWindowExA`, `SetWindowPos`, `ShowWindow`, `GetClientRect` | owns the window layout, so the game still believes it has a 640×480 window |
| `ScreenToClient`, mouse messages | maps the cursor back to 640×480 game coordinates |
| `RegOpenKeyExA`, `RegQueryValueExA`, `RegSetValueExA`, `RegCloseKey` | serves `HKLM\SOFTWARE\ZES't Corp.\GAG` from `gagfix.ini` (no admin rights, no installer needed) |
| `GetMessageA`, `PeekMessageA` | Alt+Enter |
| `PostMessageA`, `SendMessageA` | the game sends itself `SC_MINIMIZE` whenever it loses focus; those are dropped (minimizing from the title bar or taskbar still works) |
| main window `WM_SETCURSOR` | shows the normal arrow over the title bar and frame (the game hides the system cursor) |

The process is also made per-monitor DPI-aware, so Windows display scaling doesn't blur the picture a second time.

### Install

Copy `GAG_Modern` into the game folder, run `install.bat` (add `-Shortcut` for a desktop shortcut), then play with `GagModern.exe`. No compatibility mode.

### Known issues

- The game's save/load dialogs are laid out for 96 DPI and look small on 150–200% scaled displays, but they work.

### Build

`src\build.bat` needs Visual Studio with the C++ x86 tools. It produces `bin\gagfx.dll`.

### Notes on the executables

The common `Gag_patched.exe` differs from the original 1997 `Gag.exe` in two places only: the CD-drive check is bypassed (around `0x41EBFA`), and the registry key is opened read-only (`0x41EE28`). This patch only renames the `WINMM.dll` import, so it works with either.

This repository contains no game data or game code. You need your own copy of the game.
