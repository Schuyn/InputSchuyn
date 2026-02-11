# InputSchuyn

Auto-switch input method when you switch windows. No more accidentally typing 中文 in VS Code.

## How It Works

InputSchuyn listens for foreground window changes via `SetWinEventHook`. When you alt-tab into an app, it checks your rules and instantly switches your input language — English for coding, Chinese for chatting. A subtle overlay flashes briefly to confirm the switch.

## Features

- **Auto-switch** — input method follows your per-app rules on every window focus
- **GUI Config Panel** — discover running apps and assign languages with one click, no manual JSON editing needed
- **Hot-reload** — edit `rules.json` externally and changes apply on the next window switch, no restart required
- **App Discovery** — scans all visible windows so you can quickly set rules for new apps

## Quick Start

1. Place `InputSchuyn.exe` and `rules.json` in the same directory
2. Run `InputSchuyn.exe` — the config panel opens automatically
3. Select an app from the list, click **Set EN** or **Set ZH**
4. Switch windows as usual — input method follows your rules

Apps not in the rules default to Chinese (`ZH`).

## Configuration

Rules are stored in `rules.json` next to the executable:

```json
{
    "Code.exe": "EN",
    "WeChat.exe": "ZH",
    "chrome.exe": "ZH"
}
```

You can edit this file manually or use the built-in config panel. Either way, changes are picked up automatically.

## Build

Requires Windows SDK. Compile with MSVC:

```
cl /EHsc /std:c++17 src/main.cpp /Fe:bin/InputSchuyn.exe
```

Links: `user32.lib`, `gdi32.lib`, `comctl32.lib`.

## Roadmap

- [ ] UIA-based caret tracking for smarter indicator positioning
- [ ] System tray integration (hide console)
- [ ] Per-window (not just per-app) language memory

## License

MIT
