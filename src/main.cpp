/*
 * @Author: Chuyang Su cs4570@columbia.edu
 * @Date: 2026-02-11 13:47:46
 * @LastEditTime: 2026-02-11 16:41:00
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
#include "Discovery.hpp" // Include the discovery module

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

#define HKL_EN (HKL)0x04090409
#define HKL_ZH (HKL)0x08040804
#define TIMER_HIDE_UI 101
#define WM_TRAYICON (WM_USER + 1)

// GUI constants
#define ID_LISTBOX_APPS 201
#define ID_BTN_ADD_EN   202
#define ID_BTN_ADD_ZH   203
#define ID_BTN_REFRESH  204
#define ID_BTN_CLEAR_RULE 205
#define ID_BTN_TOGGLE_DEFAULT 206
#define ID_CHK_STARTUP        207

// GUI Global variables
std::map<std::wstring, HKL> appRules;    // Per-app input language rules
HKL g_defaultLang = HKL_ZH;              // Fallback language for apps without a rule
HWND g_hwndUI = NULL;                    // Indicator popup window
HWND g_hwndConfigPannel = NULL;          // Configuration panel
HWND g_hwndListBox = NULL;               // App list box
HWND g_hwndDefaultBtn = NULL;            // Default language toggle button
std::vector<std::wstring> g_discoveredList; // Mirrors listbox entries for index lookup
NOTIFYICONDATAW g_nid = { 0 };           // System tray icon data
FILETIME g_lastWriteTime = { 0 };        // Tracks rules.json modification time for hot-reload

// --- Helper: Get EXE directory ---
std::wstring GetExeDirectory() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring path(buffer);
    return path.substr(0, path.find_last_of(L"\\/"));
}

// Get the executable filename (e.g. "Code.exe") from a window handle
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

// Register or unregister this app for Windows startup via the registry
void SetStartup(bool enable) {
    HKEY hKey;
    const wchar_t* runPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyW(HKEY_CURRENT_USER, runPath, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            RegSetValueExW(hKey, L"InputSchuyn", 0, REG_SZ, (BYTE*)exePath, (lstrlenW(exePath) + 1) * sizeof(wchar_t));
        } else {
            RegDeleteValueW(hKey, L"InputSchuyn");
        }
        RegCloseKey(hKey);
    }
}

// Add a system tray icon that shows the config panel on double-click
void CreateTrayIcon(HWND hwnd) {
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    lstrcpyW(g_nid.szTip, L"InputSchuyn v1.4");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

// Serialize all app rules from memory to rules.json
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

// Load rules from rules.json with hot-reload support (skips if file unchanged)
void LoadRulesJson() {
    std::wstring configPath = GetExeDirectory() + L"\\rules.json";

    // Skip reload if file hasn't been modified since last load
    HANDLE hFile = CreateFileW(configPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        FILETIME currentWriteTime;
        if (GetFileTime(hFile, NULL, NULL, &currentWriteTime)) {
            if (g_lastWriteTime.dwLowDateTime == currentWriteTime.dwLowDateTime &&
                g_lastWriteTime.dwHighDateTime == currentWriteTime.dwHighDateTime) {
                CloseHandle(hFile);
                return; // There is no change, skip reloading
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

// Repopulate the listbox with currently active apps and their rule status
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

// Window procedure for the configuration panel
LRESULT CALLBACK ConfigWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TRAYICON:
            if (lp == WM_LBUTTONDBLCLK) ShowWindow(hwnd, SW_SHOW);
                break;
        case WM_COMMAND: {
            int wmId = LOWORD(wp);
            if (wmId == ID_BTN_ADD_EN || wmId == ID_BTN_ADD_ZH) {
                int sel = (int)SendMessage(g_hwndListBox, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) {
                    std::wstring selectedExe = g_discoveredList[sel];
                    appRules[selectedExe] = (wmId == ID_BTN_ADD_EN) ? HKL_EN : HKL_ZH;
                    SaveAllRules();     // Write the in-memory map back to JSON
                    RefreshConfigList(); // Refresh the display status
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
            }
            if (wmId == ID_CHK_STARTUP) {
                SetStartup(SendMessage((HWND)lp, BM_GETCHECK, 0, 0) == BST_CHECKED);
            }
            if (wmId == ID_BTN_REFRESH) RefreshConfigList();
            break;
        }
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE); // Clicking X only hides the window, does not exit the program
            return 0;
        case WM_DESTROY: 
            Shell_NotifyIconW(NIM_DELETE, &g_nid); 
            PostQuitMessage(0); 
            break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// Indicator popup window and caret-following logic
LRESULT CALLBACK UIWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == TIMER_HIDE_UI) ShowWindow(hwnd, SW_HIDE);
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect; GetClientRect(hwnd, &rect);
        HBRUSH hBrush = CreateSolidBrush(RGB(45, 45, 45)); FillRect(hdc, &rect, hBrush); DeleteObject(hBrush);
        SetTextColor(hdc, RGB(255, 255, 255)); SetBkMode(hdc, TRANSPARENT);
        DrawTextW(hdc, L"Input Switched", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd, &ps);
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// Show the indicator popup near the text caret, or centered on the window as fallback
void ShowIndicatorAtCaret(HWND targetHwnd) {
    if (!g_hwndUI) return;
    GUITHREADINFO gti = { sizeof(GUITHREADINFO) };
    GetGUIThreadInfo(GetWindowThreadProcessId(targetHwnd, NULL), &gti);
    POINT pt = { gti.rcCaret.left, gti.rcCaret.bottom };
    ClientToScreen(gti.hwndCaret ? gti.hwndCaret : targetHwnd, &pt);
    if (pt.x == 0 && pt.y == 0) { // No caret found, fall back to window center
        RECT r; GetWindowRect(targetHwnd, &r);
        pt.x = r.left + (r.right - r.left) / 2; pt.y = r.top + (r.bottom - r.top) / 2;
    }
    SetWindowPos(g_hwndUI, HWND_TOPMOST, pt.x, pt.y + 10, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    ShowWindow(g_hwndUI, SW_SHOWNOACTIVATE);
    SetTimer(g_hwndUI, TIMER_HIDE_UI, 1000, NULL);
}

// Foreground window change handler: switches input language based on rules
void CALLBACK WinEventProc(HWINEVENTHOOK h, DWORD e, HWND hwnd, LONG o, LONG c, DWORD t, DWORD m) {
    if (e == EVENT_SYSTEM_FOREGROUND && hwnd) {
        LoadRulesJson(); // Hot-reload rules on every window switch
        std::wstring exe = GetProcessName(hwnd);
        HKL target = appRules.count(exe) ? appRules[exe] : g_defaultLang;
        PostMessage(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)target);
        ShowIndicatorAtCaret(hwnd);
    }
}

int main() {
    ShowWindow(GetConsoleWindow(), SW_HIDE); // Hide the console window

    // Initialize UI components
    WNDCLASSW wc = { 0 }; 
    wc.lpfnWndProc = UIWndProc; 
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"InputSchuynUI"; 
    RegisterClassW(&wc);
    g_hwndUI = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        L"InputSchuynUI", L"", WS_POPUP, 0, 0, 150, 40, NULL, NULL, wc.hInstance, NULL);
    SetLayeredWindowAttributes(g_hwndUI, 0, 220, LWA_ALPHA);

    WNDCLASSW cc = { 0 }; 
    cc.lpfnWndProc = ConfigWndProc; 
    cc.hInstance = GetModuleHandle(NULL);
    cc.lpszClassName = L"ConfigPannelClass"; 
    cc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&cc);
    g_hwndConfigPannel = CreateWindowExW(0, L"ConfigPannelClass", L"InputSchuyn Config", WS_OVERLAPPEDWINDOW, 100, 100, 420, 500, NULL, NULL, cc.hInstance, NULL);
    
    g_hwndListBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY, 20, 20, 360, 320, g_hwndConfigPannel, (HMENU)ID_LISTBOX_APPS, cc.hInstance, NULL);
    CreateWindowW(L"BUTTON", L"Set EN", WS_CHILD | WS_VISIBLE, 20, 350, 80, 35, g_hwndConfigPannel, (HMENU)ID_BTN_ADD_EN, cc.hInstance, NULL);
    CreateWindowW(L"BUTTON", L"Set ZH", WS_CHILD | WS_VISIBLE, 110, 350, 80, 35, g_hwndConfigPannel, (HMENU)ID_BTN_ADD_ZH, cc.hInstance, NULL);
    CreateWindowW(L"BUTTON", L"Clear", WS_CHILD | WS_VISIBLE, 200, 350, 80, 35, g_hwndConfigPannel, (HMENU)ID_BTN_CLEAR_RULE, cc.hInstance, NULL);
    CreateWindowW(L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE, 290, 350, 80, 35, g_hwndConfigPannel, (HMENU)ID_BTN_REFRESH, cc.hInstance, NULL);
    
    g_hwndDefaultBtn = CreateWindowW(L"BUTTON", L"Default: ZH", WS_CHILD | WS_VISIBLE, 20, 400, 120, 35, g_hwndConfigPannel, (HMENU)ID_BTN_TOGGLE_DEFAULT, cc.hInstance, NULL);
    CreateWindowW(L"BUTTON", L"Startup", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 160, 400, 100, 35, g_hwndConfigPannel, (HMENU)ID_CHK_STARTUP, cc.hInstance, NULL);

    CreateTrayIcon(g_hwndConfigPannel);
    LoadRulesJson(); 
    RefreshConfigList();
    SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    MSG m; 
    while (GetMessage(&m, NULL, 0, 0)) { 
        TranslateMessage(&m); 
        DispatchMessage(&m); 
    }

    return 0;
}