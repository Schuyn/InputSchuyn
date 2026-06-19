/*
 * @Author: Chuyang Su cs4570@columbia.edu
 * @Date: 2026-02-11 13:47:46
 * @LastEditTime: 2026-06-18 23:00:01
 * @FilePath: /InputSchuyn/src/main.cpp
 * @Description:
 * Integrates window monitoring and automatic input method switching logic
 */
#include <windows.h>
#include <iostream>
#include <string>
#include <map>
#include <fstream>
#include <vector>
#include <set>
#include <iterator>
#include <shellapi.h>
#include <imm.h>
#include <UIAutomation.h>
#include "Discovery.hpp"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

#define HKL_EN (HKL)0x04090409
#define HKL_ZH (HKL)0x08040804
#define TIMER_HIDE_UI 101
#define TIMER_POLL_INPUT 102
#define WM_TRAYICON (WM_USER + 1)

// GUI constants
#define ID_LISTBOX_APPS 201
#define ID_BTN_ADD_EN   202
#define ID_BTN_ADD_ZH   203
#define ID_BTN_REFRESH  204
#define ID_BTN_CLEAR_RULE 205
#define ID_BTN_TOGGLE_DEFAULT 206
#define ID_CHK_STARTUP        207
#define ID_CHK_START_MINIMIZED 208
#define ID_TRAY_SHOW          301
#define ID_TRAY_EXIT          302
#define ID_TRAY_STARTUP       303

const int INDICATOR_WIDTH = 36;
const int INDICATOR_HEIGHT = 36;

struct InputMethodSnapshot {
    HKL hkl = NULL;
    bool imeOpen = false;
    bool imeStatusKnown = false;
    DWORD conversion = 0;
};

struct InputIndicatorState {
    HKL hkl = NULL;
    std::wstring shortLabel = L"IM";
    bool imeOpen = false;
    bool effectiveEnglish = false;
    HICON icon = NULL;
};

std::map<std::wstring, HKL> appRules;
HKL g_defaultLang = HKL_ZH;
bool g_startMinimizedToTray = false;
bool g_isExiting = false;
HWND g_hwndUI = NULL;
HWND g_hwndConfigPannel = NULL;
HWND g_hwndListBox = NULL;
HWND g_hwndDefaultBtn = NULL;
HWND g_hwndStartupChk = NULL;
HWND g_hwndStartMinimizedChk = NULL;
std::vector<std::wstring> g_discoveredList;
NOTIFYICONDATAW g_nid = { 0 };
FILETIME g_lastWriteTime = { 0 };
InputIndicatorState g_indicatorState;
HWND g_lastObservedHwnd = NULL;
HKL g_lastObservedHkl = NULL;
bool g_lastObservedImeOpen = false;
DWORD g_lastObservedConversion = 0;
IUIAutomation* g_uia = NULL;

std::wstring GetExeDirectory() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring path(buffer);
    return path.substr(0, path.find_last_of(L"\\/"));
}

std::wstring GetProcessName(HWND hwnd) {
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    std::wstring name = L"unknown";

    if (hProcess) {
        wchar_t buffer[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, buffer, &size)) {
            std::wstring path = buffer;
            name = path.substr(path.find_last_of(L"\\") + 1);
        }
        CloseHandle(hProcess);
    }

    return name;
}

bool SetStartup(bool enable) {
    HKEY hKey;
    const wchar_t* runPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    LONG openResult = RegCreateKeyExW(HKEY_CURRENT_USER, runPath, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL);
    if (openResult != ERROR_SUCCESS) return false;

    LONG result;
    if (enable) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring quotedPath = L"\"";
        quotedPath += exePath;
        quotedPath += L"\"";
        result = RegSetValueExW(hKey, L"InputSchuyn", 0, REG_SZ, (BYTE*)quotedPath.c_str(), (DWORD)((quotedPath.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(hKey, L"InputSchuyn");
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }

    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

bool IsStartupEnabled() {
    HKEY hKey;
    const wchar_t* runPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    bool enabled = false;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, runPath, 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        enabled = RegQueryValueExW(hKey, L"InputSchuyn", NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
        RegCloseKey(hKey);
    }

    return enabled;
}

void SaveSettings() {
    std::wstring configPath = GetExeDirectory() + L"\\settings.json";
    std::wofstream file(configPath);
    if (!file.is_open()) return;

    file << L"{\n";
    file << L"    \"startMinimizedToTray\": " << (g_startMinimizedToTray ? L"true" : L"false") << L",\n";
    file << L"    \"defaultLanguage\": \"" << (g_defaultLang == HKL_EN ? L"EN" : L"ZH") << L"\"\n";
    file << L"}";
}

void LoadSettings() {
    std::wstring configPath = GetExeDirectory() + L"\\settings.json";
    std::wifstream file(configPath);
    if (!file.is_open()) return;

    std::wstring line;
    while (std::getline(file, line)) {
        if (line.find(L"startMinimizedToTray") != std::wstring::npos) {
            g_startMinimizedToTray = line.find(L"true") != std::wstring::npos;
        } else if (line.find(L"defaultLanguage") != std::wstring::npos) {
            g_defaultLang = line.find(L"EN") != std::wstring::npos ? HKL_EN : HKL_ZH;
        }
    }
}

int ClampInt(int value, int minValue, int maxValue) {
    if (maxValue < minValue) return minValue;
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

bool IsAbsolutePath(const std::wstring& path) {
    return (path.size() > 2 && path[1] == L':') ||
           (path.size() > 1 && path[0] == L'\\' && path[1] == L'\\');
}

std::wstring JoinPath(const std::wstring& base, const std::wstring& leaf) {
    if (base.empty()) return leaf;
    wchar_t tail = base.back();
    if (tail == L'\\' || tail == L'/') return base + leaf;
    return base + L"\\" + leaf;
}

std::wstring ExpandEnvironmentPath(const std::wstring& path) {
    DWORD needed = ExpandEnvironmentStringsW(path.c_str(), NULL, 0);
    if (needed == 0) return path;

    std::wstring expanded(needed, L'\0');
    if (ExpandEnvironmentStringsW(path.c_str(), &expanded[0], needed) == 0) return path;
    if (!expanded.empty() && expanded.back() == L'\0') expanded.pop_back();
    return expanded;
}

bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring ResolveImeFilePath(HKL hkl) {
    wchar_t imeFile[MAX_PATH] = { 0 };
    if (ImmGetIMEFileNameW(hkl, imeFile, MAX_PATH) == 0) return L"";

    std::wstring path = ExpandEnvironmentPath(imeFile);
    if (IsAbsolutePath(path)) return path;

    wchar_t systemDir[MAX_PATH] = { 0 };
    if (GetSystemDirectoryW(systemDir, MAX_PATH) > 0) {
        std::wstring candidate = JoinPath(systemDir, path);
        if (FileExists(candidate)) return candidate;
    }

    wchar_t windowsDir[MAX_PATH] = { 0 };
    if (GetWindowsDirectoryW(windowsDir, MAX_PATH) > 0) {
        std::wstring candidate = JoinPath(windowsDir, path);
        if (FileExists(candidate)) return candidate;
    }

    return path;
}

HICON LoadIconFromFile(const std::wstring& path) {
    if (path.empty()) return NULL;

    HICON smallIcon = NULL;
    UINT extracted = ExtractIconExW(path.c_str(), 0, NULL, &smallIcon, 1);
    if (extracted != UINT_MAX && extracted > 0 && smallIcon) return smallIcon;

    SHFILEINFOW fileInfo = { 0 };
    if (SHGetFileInfoW(path.c_str(), 0, &fileInfo, sizeof(fileInfo), SHGFI_ICON | SHGFI_SMALLICON)) {
        return fileInfo.hIcon;
    }

    return NULL;
}

HICON LoadImeIcon(HKL hkl) {
    return LoadIconFromFile(ResolveImeFilePath(hkl));
}

bool IsEnglishLayout(HKL hkl) {
    return PRIMARYLANGID(LOWORD((ULONG_PTR)hkl)) == LANG_ENGLISH;
}

std::wstring GetShortInputLabel(HKL hkl, bool effectiveEnglish) {
    if (effectiveEnglish) return L"EN";

    LANGID langId = LOWORD((ULONG_PTR)hkl);
    switch (PRIMARYLANGID(langId)) {
        case LANG_CHINESE: return L"ZH";
        case LANG_JAPANESE: return L"JP";
        case LANG_KOREAN: return L"KR";
        default: return L"IM";
    }
}

HWND GetInputContextWindow(HWND targetHwnd) {
    DWORD threadId = GetWindowThreadProcessId(targetHwnd, NULL);
    GUITHREADINFO gti = { sizeof(GUITHREADINFO) };

    if (threadId != 0 && GetGUIThreadInfo(threadId, &gti)) {
        if (gti.hwndCaret && IsWindow(gti.hwndCaret)) return gti.hwndCaret;
        if (gti.hwndFocus && IsWindow(gti.hwndFocus)) return gti.hwndFocus;
    }

    return targetHwnd;
}

InputMethodSnapshot CaptureInputSnapshot(HWND targetHwnd) {
    InputMethodSnapshot snapshot;
    if (!targetHwnd || !IsWindow(targetHwnd)) return snapshot;

    DWORD threadId = GetWindowThreadProcessId(targetHwnd, NULL);
    snapshot.hkl = GetKeyboardLayout(threadId);

    HWND contextHwnd = GetInputContextWindow(targetHwnd);
    HIMC hImc = ImmGetContext(contextHwnd);
    if (hImc) {
        DWORD sentence = 0;
        snapshot.imeStatusKnown = true;
        snapshot.imeOpen = ImmGetOpenStatus(hImc) != FALSE;
        ImmGetConversionStatus(hImc, &snapshot.conversion, &sentence);
        ImmReleaseContext(contextHwnd, hImc);
    }

    return snapshot;
}

bool IsEffectivelyEnglish(const InputMethodSnapshot& snapshot) {
    if (IsEnglishLayout(snapshot.hkl)) return true;
    if (!ImmIsIME(snapshot.hkl) || !snapshot.imeStatusKnown) return false;
    if (!snapshot.imeOpen) return true;
    return (snapshot.conversion & IME_CMODE_NATIVE) == 0;
}

InputIndicatorState BuildIndicatorState(HWND targetHwnd) {
    InputMethodSnapshot snapshot = CaptureInputSnapshot(targetHwnd);

    InputIndicatorState state;
    state.hkl = snapshot.hkl;
    state.imeOpen = snapshot.imeOpen;
    state.effectiveEnglish = IsEffectivelyEnglish(snapshot);
    state.shortLabel = GetShortInputLabel(snapshot.hkl, state.effectiveEnglish);

    if (!state.effectiveEnglish) {
        state.icon = LoadImeIcon(snapshot.hkl);
    }

    return state;
}

void DestroyIndicatorIcon() {
    if (g_indicatorState.icon) {
        DestroyIcon(g_indicatorState.icon);
        g_indicatorState.icon = NULL;
    }
}

void ApplyIndicatorState(InputIndicatorState state) {
    DestroyIndicatorIcon();
    g_indicatorState = state;
}

void CreateTrayIcon(HWND hwnd) {
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    lstrcpyW(g_nid.szTip, L"InputSchuyn v1.5");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void SaveAllRules() {
    std::wstring configPath = GetExeDirectory() + L"\\rules.json";
    std::wofstream file(configPath);

    if (file.is_open()) {
        file << L"{\n";
        for (auto it = appRules.begin(); it != appRules.end(); ++it) {
            file << L"    \"" << it->first << L"\": \""
                 << (it->second == HKL_EN ? L"EN" : L"ZH") << L"\"";
            if (std::next(it) != appRules.end()) file << L",";
            file << L"\n";
        }
        file << L"}";
        file.close();
        std::wcout << L"[InputSchuyn] rules.json updated." << std::endl;
    }
}

void LoadRulesJson() {
    std::wstring configPath = GetExeDirectory() + L"\\rules.json";

    HANDLE hFile = CreateFileW(configPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        FILETIME currentWriteTime;
        if (GetFileTime(hFile, NULL, NULL, &currentWriteTime)) {
            if (g_lastWriteTime.dwLowDateTime == currentWriteTime.dwLowDateTime &&
                g_lastWriteTime.dwHighDateTime == currentWriteTime.dwHighDateTime) {
                CloseHandle(hFile);
                return;
            }
            g_lastWriteTime = currentWriteTime;
        }
        CloseHandle(hFile);
    }

    appRules.clear();
    std::wifstream file(configPath);
    if (!file.is_open()) return;

    std::wstring line;
    while (std::getline(file, line)) {
        size_t f = line.find(L'\"');
        size_t s = line.find(L'\"', f + 1);
        size_t t = line.find(L'\"', s + 1);
        size_t l = line.find(L'\"', t + 1);

        if (f != std::wstring::npos && l != std::wstring::npos) {
            std::wstring exe = line.substr(f + 1, s - f - 1);
            std::wstring lang = line.substr(t + 1, l - t - 1);
            appRules[exe] = (lang == L"EN" ? HKL_EN : HKL_ZH);
        }
    }
}

void RefreshConfigList() {
    if (!g_hwndListBox) return;

    SendMessageW(g_hwndListBox, LB_RESETCONTENT, 0, 0);
    g_discoveredList.clear();

    auto currentApps = Discovery::GetActiveAppsSet();
    for (const auto& app : currentApps) {
        std::wstring status = appRules.count(app) ? L" [Set]" : L" [New]";
        SendMessageW(g_hwndListBox, LB_ADDSTRING, 0, (LPARAM)(app + status).c_str());
        g_discoveredList.push_back(app);
    }
}

bool TryRectFromSafeArray(SAFEARRAY* rects, RECT& rect) {
    if (!rects) return false;

    LONG lower = 0;
    LONG upper = -1;
    if (FAILED(SafeArrayGetLBound(rects, 1, &lower))) return false;
    if (FAILED(SafeArrayGetUBound(rects, 1, &upper))) return false;
    if (upper - lower + 1 < 4) return false;

    double values[4] = { 0 };
    for (LONG i = 0; i < 4; ++i) {
        LONG index = lower + i;
        if (FAILED(SafeArrayGetElement(rects, &index, &values[i]))) return false;
    }

    rect.left = (LONG)values[0];
    rect.top = (LONG)values[1];
    rect.right = (LONG)(values[0] + values[2]);
    rect.bottom = (LONG)(values[1] + values[3]);

    return values[2] >= 0 && values[3] >= 0;
}

bool TryGetRangePoint(IUIAutomationTextRange* range, POINT& pt) {
    if (!range) return false;

    SAFEARRAY* rects = NULL;
    RECT rect = { 0 };

    if (SUCCEEDED(range->GetBoundingRectangles(&rects)) && TryRectFromSafeArray(rects, rect)) {
        SafeArrayDestroy(rects);
        pt.x = rect.right;
        pt.y = rect.bottom;
        return true;
    }

    if (rects) SafeArrayDestroy(rects);

    range->ExpandToEnclosingUnit(TextUnit_Character);
    rects = NULL;

    if (SUCCEEDED(range->GetBoundingRectangles(&rects)) && TryRectFromSafeArray(rects, rect)) {
        SafeArrayDestroy(rects);
        pt.x = rect.left;
        pt.y = rect.bottom;
        return true;
    }

    if (rects) SafeArrayDestroy(rects);
    return false;
}

bool TryGetUiaCaretPoint(POINT& pt) {
    if (!g_uia) return false;

    IUIAutomationElement* focused = NULL;
    if (FAILED(g_uia->GetFocusedElement(&focused)) || !focused) return false;

    IUnknown* patternUnknown = NULL;
    IUIAutomationTextPattern2* textPattern = NULL;
    IUIAutomationTextRange* caretRange = NULL;
    bool found = false;

    if (SUCCEEDED(focused->GetCurrentPattern(UIA_TextPattern2Id, &patternUnknown)) && patternUnknown) {
        if (SUCCEEDED(patternUnknown->QueryInterface(__uuidof(IUIAutomationTextPattern2), (void**)&textPattern)) && textPattern) {
            BOOL isActive = FALSE;
            if (SUCCEEDED(textPattern->GetCaretRange(&isActive, &caretRange)) && caretRange && isActive) {
                found = TryGetRangePoint(caretRange, pt);
            }
        }
    }

    if (caretRange) caretRange->Release();
    if (textPattern) textPattern->Release();
    if (patternUnknown) patternUnknown->Release();
    focused->Release();

    return found;
}

void PollCurrentInputState();

LRESULT CALLBACK ConfigWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TRAYICON:
            if (lp == WM_LBUTTONDBLCLK) {
                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
            } else if (lp == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);

                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"Show Config");
                AppendMenuW(menu, MF_STRING | (IsStartupEnabled() ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_STARTUP, L"Launch at startup");
                AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

                SetForegroundWindow(hwnd);
                TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(menu);
            }
            break;

        case WM_TIMER:
            if (wp == TIMER_POLL_INPUT) {
                PollCurrentInputState();
                return 0;
            }
            break;

        case WM_COMMAND: {
            int wmId = LOWORD(wp);

            if (wmId == ID_BTN_ADD_EN || wmId == ID_BTN_ADD_ZH) {
                int sel = (int)SendMessage(g_hwndListBox, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) {
                    std::wstring selectedExe = g_discoveredList[sel];
                    appRules[selectedExe] = (wmId == ID_BTN_ADD_EN) ? HKL_EN : HKL_ZH;
                    SaveAllRules();
                    RefreshConfigList();
                }
            }

            if (wmId == ID_BTN_CLEAR_RULE) {
                int sel = (int)SendMessageW(g_hwndListBox, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) {
                    std::wstring selectedExe = g_discoveredList[sel];

                    if (appRules.count(selectedExe)) {
                        appRules.erase(selectedExe);
                        SaveAllRules();
                        RefreshConfigList();

                        std::wcout << L"[InputSchuyn] Rule removed: " << selectedExe << std::endl;
                        std::wcout.flush();
                    }
                }
            }

            if (wmId == ID_BTN_TOGGLE_DEFAULT) {
                g_defaultLang = (g_defaultLang == HKL_ZH ? HKL_EN : HKL_ZH);
                SetWindowTextW(g_hwndDefaultBtn, g_defaultLang == HKL_ZH ? L"Default: ZH" : L"Default: EN");
                SaveSettings();
            }

            if (wmId == ID_CHK_STARTUP) {
                bool enableStartup = SendMessage((HWND)lp, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (!SetStartup(enableStartup)) {
                    SendMessageW(g_hwndStartupChk, BM_SETCHECK, IsStartupEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
                }
            }

            if (wmId == ID_CHK_START_MINIMIZED) {
                g_startMinimizedToTray = SendMessage((HWND)lp, BM_GETCHECK, 0, 0) == BST_CHECKED;
                SaveSettings();
            }

            if (wmId == ID_TRAY_SHOW) {
                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
            }

            if (wmId == ID_TRAY_STARTUP) {
                bool enableStartup = !IsStartupEnabled();
                SetStartup(enableStartup);
                SendMessageW(g_hwndStartupChk, BM_SETCHECK, IsStartupEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
            }

            if (wmId == ID_TRAY_EXIT) {
                g_isExiting = true;
                DestroyWindow(hwnd);
            }

            if (wmId == ID_BTN_REFRESH) RefreshConfigList();
            break;
        }

        case WM_CLOSE:
            if (g_isExiting) break;
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, TIMER_POLL_INPUT);
            DestroyIndicatorIcon();

            if (g_uia) {
                g_uia->Release();
                g_uia = NULL;
            }

            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            break;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}

void DrawFallbackInputIcon(HDC hdc, const RECT& iconRect) {
    COLORREF fill = g_indicatorState.effectiveEnglish ? RGB(0, 120, 215) : RGB(23, 142, 87);

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, iconRect.left, iconRect.top, iconRect.right, iconRect.bottom, 8, 8);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    HFONT font = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetTextColor(hdc, RGB(255, 255, 255));
    SetBkMode(hdc, TRANSPARENT);

    RECT textRect = iconRect;
    DrawTextW(hdc, g_indicatorState.shortLabel.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldFont);
    DeleteObject(font);
}

LRESULT CALLBACK UIWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            if (wp == TIMER_HIDE_UI) {
                KillTimer(hwnd, TIMER_HIDE_UI);
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rect;
            GetClientRect(hwnd, &rect);

            if (g_indicatorState.icon) {
                DrawIconEx(hdc, 4, 4, g_indicatorState.icon, 28, 28, 0, NULL, DI_NORMAL);
            } else {
                RECT iconRect = { 2, 2, INDICATOR_WIDTH - 2, INDICATOR_HEIGHT - 2 };
                DrawFallbackInputIcon(hdc, iconRect);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}

void ShowIndicatorAtCaret(HWND targetHwnd) {
    if (!g_hwndUI || !targetHwnd || !IsWindow(targetHwnd)) return;

    ApplyIndicatorState(BuildIndicatorState(targetHwnd));

    POINT pt = { 0, 0 };
    bool hasCaret = TryGetUiaCaretPoint(pt);

    if (!hasCaret) {
        DWORD threadId = GetWindowThreadProcessId(targetHwnd, NULL);
        GUITHREADINFO gti = { sizeof(GUITHREADINFO) };

        if (threadId != 0 && GetGUIThreadInfo(threadId, &gti) && gti.hwndCaret && IsWindow(gti.hwndCaret)) {
            pt.x = gti.rcCaret.right;
            pt.y = gti.rcCaret.bottom;
            hasCaret = ClientToScreen(gti.hwndCaret, &pt) != FALSE;
        }
    }

    if (!hasCaret) return;

    int x = pt.x + 8;
    int y = pt.y - INDICATOR_HEIGHT - 2;

    MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
        x = ClampInt(x, monitorInfo.rcWork.left, monitorInfo.rcWork.right - INDICATOR_WIDTH);
        y = ClampInt(y, monitorInfo.rcWork.top, monitorInfo.rcWork.bottom - INDICATOR_HEIGHT);
    }

    SetWindowPos(g_hwndUI, HWND_TOPMOST, x, y, INDICATOR_WIDTH, INDICATOR_HEIGHT, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_hwndUI, NULL, TRUE);
    UpdateWindow(g_hwndUI);
    SetTimer(g_hwndUI, TIMER_HIDE_UI, 900, NULL);
}

void PollCurrentInputState() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || !IsWindow(hwnd)) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) return;

    InputMethodSnapshot snapshot = CaptureInputSnapshot(hwnd);

    bool sameWindow = hwnd == g_lastObservedHwnd;
    bool hklChanged = snapshot.hkl && snapshot.hkl != g_lastObservedHkl;
    bool modeChanged = snapshot.imeStatusKnown &&
        (snapshot.imeOpen != g_lastObservedImeOpen || snapshot.conversion != g_lastObservedConversion);

    if (sameWindow && (hklChanged || modeChanged)) {
        ShowIndicatorAtCaret(hwnd);
    }

    g_lastObservedHwnd = hwnd;
    g_lastObservedHkl = snapshot.hkl;
    g_lastObservedImeOpen = snapshot.imeOpen;
    g_lastObservedConversion = snapshot.conversion;
}

void CALLBACK WinEventProc(HWINEVENTHOOK h, DWORD e, HWND hwnd, LONG o, LONG c, DWORD t, DWORD m) {
    if (e == EVENT_SYSTEM_FOREGROUND && hwnd) {
        LoadRulesJson();

        std::wstring exe = GetProcessName(hwnd);
        HKL target = appRules.count(exe) ? appRules[exe] : g_defaultLang;

        SendMessageTimeoutW(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)target, SMTO_ABORTIFHUNG, 150, NULL);
        ShowIndicatorAtCaret(hwnd);

        InputMethodSnapshot snapshot = CaptureInputSnapshot(hwnd);
        g_lastObservedHwnd = hwnd;
        g_lastObservedHkl = snapshot.hkl;
        g_lastObservedImeOpen = snapshot.imeOpen;
        g_lastObservedConversion = snapshot.conversion;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&g_uia);

    LoadSettings();

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = UIWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"InputSchuynUI";
    RegisterClassW(&wc);

    g_hwndUI = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        L"InputSchuynUI", L"", WS_POPUP, 0, 0, INDICATOR_WIDTH, INDICATOR_HEIGHT, NULL, NULL, wc.hInstance, NULL);
    SetLayeredWindowAttributes(g_hwndUI, 0, 235, LWA_ALPHA);
    SetWindowRgn(g_hwndUI, CreateRoundRectRgn(0, 0, INDICATOR_WIDTH + 1, INDICATOR_HEIGHT + 1, 8, 8), TRUE);

    WNDCLASSW cc = { 0 };
    cc.lpfnWndProc = ConfigWndProc;
    cc.hInstance = hInstance;
    cc.lpszClassName = L"ConfigPannelClass";
    cc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&cc);

    g_hwndConfigPannel = CreateWindowExW(0, L"ConfigPannelClass", L"InputSchuyn Config",
        WS_OVERLAPPEDWINDOW, 100, 100, 420, 500, NULL, NULL, cc.hInstance, NULL);

    g_hwndListBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        20, 20, 360, 320, g_hwndConfigPannel, (HMENU)ID_LISTBOX_APPS, cc.hInstance, NULL);

    CreateWindowW(L"BUTTON", L"Set EN", WS_CHILD | WS_VISIBLE, 20, 350, 80, 35, g_hwndConfigPannel, (HMENU)ID_BTN_ADD_EN, cc.hInstance, NULL);
    CreateWindowW(L"BUTTON", L"Set ZH", WS_CHILD | WS_VISIBLE, 110, 350, 80, 35, g_hwndConfigPannel, (HMENU)ID_BTN_ADD_ZH, cc.hInstance, NULL);
    CreateWindowW(L"BUTTON", L"Clear", WS_CHILD | WS_VISIBLE, 200, 350, 80, 35, g_hwndConfigPannel, (HMENU)ID_BTN_CLEAR_RULE, cc.hInstance, NULL);
    CreateWindowW(L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE, 290, 350, 80, 35, g_hwndConfigPannel, (HMENU)ID_BTN_REFRESH, cc.hInstance, NULL);

    g_hwndDefaultBtn = CreateWindowW(L"BUTTON", L"Default: ZH", WS_CHILD | WS_VISIBLE,
        20, 400, 120, 35, g_hwndConfigPannel, (HMENU)ID_BTN_TOGGLE_DEFAULT, cc.hInstance, NULL);

    g_hwndStartupChk = CreateWindowW(L"BUTTON", L"Launch at startup", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        160, 400, 180, 35, g_hwndConfigPannel, (HMENU)ID_CHK_STARTUP, cc.hInstance, NULL);

    g_hwndStartMinimizedChk = CreateWindowW(L"BUTTON", L"Start minimized to tray", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        20, 440, 220, 35, g_hwndConfigPannel, (HMENU)ID_CHK_START_MINIMIZED, cc.hInstance, NULL);

    CreateTrayIcon(g_hwndConfigPannel);
    LoadRulesJson();
    RefreshConfigList();

    SetWindowTextW(g_hwndDefaultBtn, g_defaultLang == HKL_ZH ? L"Default: ZH" : L"Default: EN");
    SendMessageW(g_hwndStartupChk, BM_SETCHECK, IsStartupEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_hwndStartMinimizedChk, BM_SETCHECK, g_startMinimizedToTray ? BST_CHECKED : BST_UNCHECKED, 0);

    if (!g_startMinimizedToTray) {
        ShowWindow(g_hwndConfigPannel, SW_SHOW);
    }

    SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    SetTimer(g_hwndConfigPannel, TIMER_POLL_INPUT, 250, NULL);

    MSG m;
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }

    CoUninitialize();
    return 0;
}