# InputSchuyn

Auto-switch input method when you switch windows. No more accidentally typing 中文 in VS Code.

## How It Works

InputSchuyn listens for foreground window changes via `SetWinEventHook`. When you alt-tab into an app, it checks `rules.json` and instantly switches your input language — English for coding, Chinese for chatting.

A subtle overlay flashes briefly to confirm the switch.

## Configuration

Edit `rules.json` next to the executable:

```json
{
    "Code.exe": "EN",
    "WeChat.exe": "ZH",
    "chrome.exe": "ZH"
}
```

Apps not listed default to Chinese (`ZH`).

## Build

Requires Windows SDK. Compile with MSVC:

```
cl /EHsc /std:c++17 src/main.cpp /Fe:bin/InputSchuyn.exe
```

Or use your preferred IDE — just link `user32.lib` and `gdi32.lib`.

## Usage

1. Place `InputSchuyn.exe` and `rules.json` in the same directory
2. Run `InputSchuyn.exe`
3. Switch windows as usual — input method follows your rules

Press `Ctrl+C` in the console to exit.

## Roadmap

- [ ] UIA-based caret tracking for smarter indicator positioning
- [ ] System tray integration (hide console)
- [ ] Hot-reload `rules.json` without restart
- [ ] Per-window (not just per-app) language memory

## License

MIT
