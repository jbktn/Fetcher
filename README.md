# Fetcher

A desktop app for testing API calls, modifying them and saving them. It runs as a native application on Windows, macOS and Linux.

All three apps come from one C++ codebase built on [wxWidgets](https://www.wxwidgets.org/), which uses each platform's real toolkit: Win32 on Windows, Cocoa on macOS, GTK on Linux. There is no embedded browser and no runtime to install.

The interface is flat and minimal, with light and dark modes. Text boxes, the menu bar, lists, the saved-requests tree and file pickers are the platform's own. Tabs, buttons, dropdowns, checkboxes, dialogs and right-click menus are drawn by the app so they look the same everywhere; they work with the keyboard, but screen readers see less detail on them than on native controls.

## What it does

**Test**
- Send GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS or any custom method.
- Set query parameters, headers, a JSON, XML, text or URL-encoded form body, and Bearer, Basic or API key auth.
- See the status, time, size, formatted and coloured body, response headers, and the exact request headers that were sent.
- Search the response, or save it to a file. Binary responses are detected instead of being shown as garbage.
- Cancel a call at any time, and keep calls running in several tabs at once.

**Modify**
- Edit anything and send again. The URL and the params table stay in sync, and rows can be switched off without deleting them.
- Reopen any past call from the History tab and change it.
- Set the timeout, redirect handling and TLS verification for each request.

**Save**
- Keep requests in nested folders, with a filter, drag and drop, rename and duplicate.
- A dot on a tab marks unsaved changes, and you're asked before anything is lost.
- Import and export collections as JSON files to share or back them up.

**Light and dark**
- Switch instantly with the sun/moon button at the top right, or choose System, Light or Dark under View > Appearance. The choice is remembered, and System follows your OS.

## Keyboard shortcuts

On macOS, use Cmd instead of Ctrl.

| Shortcut | Action |
| --- | --- |
| Ctrl+Enter | Send, or cancel the running call |
| Ctrl+N | New request |
| Ctrl+S / Ctrl+Shift+S | Save / Save As |
| Ctrl+W | Close tab |
| Ctrl+D | Duplicate tab |
| Ctrl+PgDn / Ctrl+PgUp | Next / previous tab |
| Ctrl+L | Go to the URL field |
| Ctrl+Shift+F | Format the JSON body |
| Ctrl+Shift+L | Switch between light and dark |
| F2 / Del | Rename / delete the selected saved item |

## Where your data lives

| Platform | Folder |
| --- | --- |
| Windows | `%APPDATA%\Fetcher` |
| macOS | `~/Library/Application Support/Fetcher` |
| Linux | `~/.local/share/Fetcher` |

Saved requests are in `collections.json` and the last 200 sent requests are in `history.json`. Set the `FETCHER_DATA_DIR` environment variable to use a different folder.

**Credentials are stored in plain text.** Tokens and passwords you enter are written to those two files, and to any collection you export. The files are readable only by your user account, but they are not encrypted. Don't use long-lived production secrets if that's a concern, and use Clear History to remove past calls.

## Building

CMake downloads wxWidgets and nlohmann/json at configure time, pinned by checksum. The first build compiles wxWidgets and takes several minutes.

### Windows

No admin rights needed. The bootstrap script downloads a portable compiler, CMake and Ninja into `.toolchain\`.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\bootstrap-windows.ps1
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -Package
```

The app is `build\windows\fetcher.exe`, a single file with no other dependencies, and the zip is in `build\windows\package`.

### Linux

```bash
# Fedora
sudo dnf install gcc-c++ cmake ninja-build gtk3-devel libcurl-devel
# Debian or Ubuntu
sudo apt install g++ cmake ninja-build libgtk-3-dev libcurl4-openssl-dev

scripts/build-unix.sh --package
```

The app is `build/linux/fetcher`. `build/linux/package` gets a `.tar.gz`, plus a `.deb` or `.rpm` if `dpkg-dev` or `rpm-build` is installed.

### macOS

Requires the Xcode command line tools and CMake (`brew install cmake ninja`).

```bash
scripts/build-unix.sh --package
```

The app is `build/darwin/Fetcher.app`, a universal binary for Apple silicon and Intel, and the disk image is in `build/darwin/package`. It is not notarized, so on another Mac the first launch is blocked until you allow it under System Settings > Privacy & Security.

### Continuous integration

`.github/workflows/build.yml` builds, tests and packages all three platforms on every push, and attaches the packages to the run.

## How it's organised

- `src/core`: no GUI code. The data model, URL handling, JSON file format, storage and HTTP. Tested by `tests/core_tests.cpp`, which makes real requests to a local server.
- `src/app`: the wxWidgets interface. `theme.*` holds the two colour palettes, and `widgets.*` the custom-drawn controls.
- `packaging`: icons and per-platform metadata. `generate_icons.py` redraws the icons.
- `resources/fonts`: [Roboto](https://github.com/googlefonts/roboto-3-classic) for the interface and [Roboto Mono](https://github.com/googlefonts/RobotoMono) for the code editors, so text looks the same in every release. They're under the SIL Open Font License, and the licence text ships beside them. If the files are missing, the app falls back to the platform's fonts. The menu bar, tooltips and file pickers are drawn by the OS and keep its font.

HTTP uses WinHTTP on Windows and the system's libcurl on macOS and Linux. Both sit behind one function, `detail::sendOnce`. Redirects are handled above that, in `sendRequest`, so the rules are the same everywhere, including that credentials are dropped when a redirect leads to another site.
