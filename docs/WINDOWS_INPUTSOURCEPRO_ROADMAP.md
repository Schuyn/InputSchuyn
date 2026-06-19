# Windows InputSourcePro-like Roadmap

## 1. Product Goal

Build a Windows-side input source assistant for InputSchuyn that behaves like an
InputSourcePro-style utility:

- Switch input method automatically by application, window, and eventually web
  context.
- Show a small input source icon near the active text caret when the input
  source changes.
- Keep the current tray/config workflow as the user-facing control surface.
- Move caret tracking from best-effort external guessing toward Windows text
  input framework integration.

The target experience is:

1. User focuses a text field.
2. InputSchuyn determines the desired input source from rules.
3. Windows switches to that input source.
4. A small icon appears beside the text caret, similar to an IME candidate
   window or macOS InputSourcePro indicator.
5. The icon disappears quickly and never steals focus.

## 2. Current State

The existing app already has useful foundation:

- Win32 tray app.
- Per-process rule map in `rules.json`.
- Config panel for discovered apps.
- Default language setting in `settings.json`.
- Startup registry toggle.
- Foreground-window monitoring through `SetWinEventHook`.
- Input language switch request through `WM_INPUTLANGCHANGEREQUEST`.

The current weak point is caret positioning:

- `GetGUIThreadInfo` works mainly for traditional Win32 edit controls.
- UI Automation `TextPattern2` works only when the target app exposes a text
  provider.
- Modern apps such as WeChat, Chromium, Electron, UWP, and custom-rendered text
  fields may not expose a usable caret rectangle to a normal external process.

Therefore the project should treat "external overlay with UIA fallback" as an
intermediate layer, not the final architecture.

## 3. Target Architecture

### 3.1 Main Controller

Keep the current InputSchuyn executable as the main controller.

Responsibilities:

- Tray icon and tray menu.
- Config panel.
- Startup toggle.
- Rule loading/saving.
- App/window focus detection.
- Input source switching.
- Overlay window rendering.
- IPC endpoint for receiving caret rectangles from helper components.

Expected binary:

- `InputSchuyn.exe`

### 3.2 Caret Provider Layer

Introduce a layered caret provider interface.

Provider priority:

1. TSF/TIP caret provider.
2. UI Automation caret provider.
3. Win32 caret provider.
4. Optional fallback position policy.

The main controller should not care where the caret came from. It only consumes:

```text
CaretRect {
    hwnd
    screenLeft
    screenTop
    screenRight
    screenBottom
    confidence
    source
    timestamp
}
```

### 3.3 TSF/TIP Component

Add a Windows Text Services Framework component as the long-term precise caret
source.

Responsibilities:

- Register as a COM text service.
- Receive text context activation callbacks.
- Read current selection/caret range from TSF context.
- Convert text extent to screen coordinates.
- Send caret rectangles to the main controller.

Expected binary:

- `InputSchuynTsf.dll`

### 3.4 IPC Channel

Use a simple local IPC protocol between `InputSchuynTsf.dll` and
`InputSchuyn.exe`.

Recommended first version:

- `WM_COPYDATA` or registered window message for local prototype.

Recommended production version:

- Named pipe with versioned message structs.

Initial message:

```text
InputSchuynCaretV1 {
    uint32_t version
    uint64_t hwnd
    int32_t left
    int32_t top
    int32_t right
    int32_t bottom
    uint32_t source
    uint32_t confidence
}
```

## 4. Delivery Phases

The schedule below assumes part-time solo development on Windows, with manual
testing on local apps. If this becomes full-time work, phases can be compressed.

### Phase 0 - Stabilize Baseline

Duration: 1-2 days

Scope:

- Restore a compiling baseline.
- Confirm `cl.exe` / MSVC Build Tools setup.
- Move current single-file prototype toward safer structure only if needed.
- Keep current behavior unchanged except for build cleanup.

Deliverables:

- Known-good build command.
- Fresh `InputSchuyn.exe`.
- Baseline manual test checklist.

Acceptance:

- App launches without console errors.
- Tray icon appears.
- Config window opens from tray.
- Existing `rules.json` is read.
- Switching between two known apps applies EN/ZH rules.
- Startup checkbox writes/removes `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\InputSchuyn`.

Exit Criteria:

- We can reproduce a clean local build from source.
- The current app is safe to refactor.

### Phase 1 - Overlay and Input Source Model

Duration: 3-5 days

Scope:

- Create a clean input source model independent from raw HKL values.
- Normalize EN/ZH/IME open-state detection.
- Extract icon loading into a reusable helper.
- Make overlay rendering icon-only, non-activating, and DPI-aware.
- Keep UIA/Win32 caret providers as best-effort.

Deliverables:

- `InputSourceInfo` model.
- `OverlayWindow` module or equivalent isolated implementation.
- Manual icon rendering test path.
- Provider confidence logging in debug builds.

Acceptance:

- Switching to English shows an EN/Microsoft-style fallback icon.
- Switching to Chinese IME shows the IME icon when Windows exposes one.
- If no real icon is available, fallback badge is shown.
- Overlay never steals keyboard focus.
- Overlay never appears in Alt-Tab.
- Overlay does not jump to screen center when caret is unknown.

Exit Criteria:

- The visual indicator is reliable when a valid caret rectangle is supplied.
- The remaining problem is clearly limited to caret acquisition.

### Phase 2 - Caret Provider Abstraction

Duration: 3-5 days

Scope:

- Add `ICaretProvider`-style abstraction.
- Implement Win32 provider using `GetGUIThreadInfo`.
- Implement UIA provider using focused element and `TextPattern2`.
- Add provider ordering and confidence levels.
- Add debug logging for provider failure reasons.

Deliverables:

- `Win32CaretProvider`.
- `UiaCaretProvider`.
- Unified `TryGetCaretRect`.
- Diagnostic log messages.

Acceptance:

- Notepad caret position is detected.
- Standard Win32 edit controls are detected.
- At least one browser text field is detected if UIA exposes it.
- When all providers fail, no misleading center-screen indicator appears.
- Logs identify which provider succeeded or failed.

Exit Criteria:

- External-process approach is fully characterized.
- We have evidence for which apps require TSF.

### Phase 3 - TSF/TIP Proof of Concept

Duration: 1-2 weeks

Scope:

- Create a minimal TSF text service DLL.
- Register/unregister the text service locally.
- Receive activation in supported text contexts.
- Read current selection.
- Call TSF context view APIs to get screen rectangle.
- Send rectangle to the main app using prototype IPC.

Deliverables:

- `InputSchuynTsf.dll`.
- Registration script or small registrar executable.
- Minimal IPC from DLL to app.
- Debug output showing caret rectangle updates.

Acceptance:

- DLL registers and unregisters cleanly.
- Windows loads the text service without crashing target apps.
- Focusing Notepad text area produces caret rectangles.
- Focusing at least one Chromium/Electron text box produces either a rectangle
  or a documented TSF failure reason.
- Main app receives TSF caret updates.
- Existing InputSchuyn app still works when the TSF component is absent.

Exit Criteria:

- We prove whether TSF gives better caret coverage than UIA/Win32 on the user's
  real target apps.

### Phase 4 - Production IPC and Overlay Integration

Duration: 1 week

Scope:

- Replace prototype IPC with a stable local protocol.
- Route TSF caret rectangles into the same overlay path.
- Add freshness checks so stale rectangles are ignored.
- Add process/window identity checks.
- Add DPI scaling support for coordinates and icon size.

Deliverables:

- Versioned IPC message format.
- Main app IPC listener.
- TSF sender.
- Unified overlay positioning logic.

Acceptance:

- TSF-provided caret rectangle drives overlay position.
- Overlay follows caret across typing and focus changes.
- Overlay does not use stale coordinates after window switch.
- Mixed DPI monitors do not produce obvious offset errors.
- Main app can start before or after the TSF component is loaded.

Exit Criteria:

- The InputSourcePro-like indicator path works end-to-end on supported apps.

### Phase 5 - Rule System Expansion

Duration: 1 week

Scope:

- Extend rules beyond process name.
- Support optional window title matching.
- Prepare browser URL/domain rule support.
- Keep backward compatibility with existing `rules.json`.

Possible rule schema:

```json
{
  "version": 2,
  "defaults": {
    "language": "ZH"
  },
  "apps": {
    "Code.exe": { "language": "EN" },
    "Weixin.exe": { "language": "ZH" },
    "chrome.exe": {
      "language": "ZH",
      "domains": {
        "github.com": "EN",
        "chat.openai.com": "ZH"
      }
    }
  }
}
```

Deliverables:

- Versioned rule loader.
- Backward-compatible migration path.
- Rule priority documentation.

Acceptance:

- Existing flat `rules.json` continues to load.
- New versioned rules load.
- App rule beats default rule.
- More specific rule beats less specific rule.
- Invalid rule file fails gracefully without deleting user settings.

Exit Criteria:

- InputSchuyn can grow toward InputSourcePro-style context rules.

### Phase 6 - Installer and Registration Flow

Duration: 1 week

Scope:

- Package `InputSchuyn.exe` and `InputSchuynTsf.dll`.
- Add install/uninstall scripts or small installer.
- Register COM/TSF component.
- Register startup option only when user enables it.
- Provide repair/unregister path.

Deliverables:

- `install.ps1` or installer project.
- `uninstall.ps1`.
- Clear admin/user permission notes.
- Versioned release folder layout.

Acceptance:

- Fresh install works on a clean Windows user profile.
- Uninstall removes TSF registration.
- Uninstall removes startup registry value if owned by InputSchuyn.
- Existing `rules.json` and `settings.json` are not destroyed by upgrade.

Exit Criteria:

- The app is usable outside the development checkout.

### Phase 7 - App Compatibility Pass

Duration: 1-2 weeks

Scope:

- Test real target applications.
- Record whether caret comes from TSF, UIA, Win32, or no provider.
- Tune fallback behavior per app category.
- Document limitations.

Initial test matrix:

| App | Expected Provider | Acceptance |
| --- | --- | --- |
| Notepad | Win32 or TSF | icon appears near caret |
| Windows Terminal | TSF/UIA/no caret | no bad center fallback |
| VS Code | UIA/TSF | icon near editor caret if provider supports it |
| Chrome address bar | UIA/TSF | icon near caret |
| Chrome web text area | UIA/TSF | icon near caret where exposed |
| WeChat | TSF target | icon near chat input if TSF exposes rect |
| Codex/Electron apps | UIA/TSF target | documented behavior |

Deliverables:

- Compatibility table.
- Known limitations list.
- App-specific fallback notes.

Acceptance:

- At least three common apps show near-caret indicators.
- Apps that cannot expose caret do not show misleading indicators.
- No tested app crashes because of TSF component.

Exit Criteria:

- The project has honest compatibility claims.

### Phase 8 - Polish and Release Candidate

Duration: 3-5 days

Scope:

- Reduce flicker.
- Tune icon size and duration.
- Add enable/disable indicator setting.
- Add enable/disable TSF provider setting.
- Add logs export for debugging.
- Update README.

Deliverables:

- Release candidate build.
- Updated README.
- Troubleshooting guide.
- Manual regression checklist.

Acceptance:

- Indicator behavior feels stable in daily use.
- User can disable overlay without disabling auto-switch.
- User can disable startup.
- User can fully exit from tray.
- README accurately describes supported and unsupported behavior.

Exit Criteria:

- Ready for a tagged local release.

## 5. Estimated Total Timeline

Conservative estimate:

- Minimum useful TSF proof: 2-3 weeks.
- InputSourcePro-like local prototype: 4-6 weeks.
- Polished daily-driver build: 6-8 weeks.

Timeline summary:

| Phase | Duration |
| --- | --- |
| Phase 0 | 1-2 days |
| Phase 1 | 3-5 days |
| Phase 2 | 3-5 days |
| Phase 3 | 1-2 weeks |
| Phase 4 | 1 week |
| Phase 5 | 1 week |
| Phase 6 | 1 week |
| Phase 7 | 1-2 weeks |
| Phase 8 | 3-5 days |

## 6. Definition of Done

The project should not be considered complete merely because an overlay window
exists. Completion requires real end-to-end behavior.

Minimum done:

- User can configure app-level input rules.
- User can enable/disable startup.
- User can switch windows and get the expected input source.
- Indicator appears near the caret in at least standard text controls.
- Indicator does not appear in misleading positions when caret is unavailable.

InputSourcePro-like done:

- TSF/TIP component is installed and active.
- Main app receives TSF caret rectangles.
- Indicator appears near caret in multiple modern apps.
- The app has documented compatibility results.
- Install/uninstall path is reliable.

Daily-driver done:

- No known crash in target apps.
- No focus stealing.
- No persistent overlay stuck on screen.
- Settings survive restart.
- Startup behavior is predictable.
- User can fully disable or uninstall every integration.

## 7. Major Risks

### TSF Complexity

TSF is COM-heavy and less forgiving than ordinary Win32 UI code. Registration,
activation, threading, and cleanup must be handled carefully.

Mitigation:

- Build TSF as a small isolated POC first.
- Keep main controller independent from TSF.
- Make TSF optional.

### App Compatibility

Some apps may not expose usable text geometry even through the expected path, or
may use unusual composition behavior.

Mitigation:

- Maintain provider source/confidence logging.
- Keep app compatibility table current.
- Avoid claiming universal support.

### Permissions and Integrity Levels

High-integrity apps may not be visible to a normal-integrity helper.

Mitigation:

- Document limitation.
- Avoid requiring admin for normal usage.
- Do not silently elevate.

### IME Icon Availability

Not every input method exposes a clean icon through IMM/TSF-accessible files.

Mitigation:

- Use fallback badges.
- Cache icons when resolved.
- Allow future custom icon overrides.

### Build and Packaging

COM/TSF registration introduces installation complexity.

Mitigation:

- Add scripts early.
- Test install/uninstall repeatedly.
- Keep a portable mode where TSF is absent.

## 8. Immediate Next Step

Start with Phase 0 and Phase 1 only.

Recommended first implementation sequence:

1. Restore a clean compiling `src/main.cpp`.
2. Confirm the startup toggle works.
3. Extract overlay drawing and input source state into small helpers.
4. Add provider diagnostics for Win32/UIA caret attempts.
5. Record which target apps fail caret detection.
6. Only then start the TSF POC.

This avoids mixing three hard problems at once: build setup, overlay rendering,
and TSF integration.
