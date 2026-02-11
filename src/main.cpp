/*
 * @Author: Chuyang Su cs4570@columbia.edu
 * @Date: 2026-02-11 13:47:46
 * @LastEditTime: 2026-02-11 15:45:43
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

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#define HKL_EN (HKL)0x04090409
#define HKL_ZH (HKL)0x08040804
#define TIMER_HIDE_UI 101
#define LANG_DEFAULT HKL_ZH // Define your default language

// GUI constants
#define ID_LISTBOX_APPS 201
#define ID_BTN_ADD_EN   202
#define ID_BTN_ADD_ZH   203
#define ID_BTN_REFRESH  204

// GUI Global variables
std::map<std::wstring, HKL> appRules;
HWND g_hwndUI = NULL;            // Indicator popup window
HWND g_hwndConfigPannel = NULL;  // Configuration panel
HWND g_hwndListBox = NULL;       // List box
std::vector<std::wstring> g_discoveredList; 
FILETIME g_lastWriteTime = { 0 };

// --- Helper: Get EXE directory ---
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

// Rules management logic
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
    
    // Hot-reload logic: Check if the file's last write time has changed since we last loaded it
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

// GUI Logic
void RefreshConfigList() {
    if (!g_hwndListBox) return;
    SendMessage(g_hwndListBox, LB_RESETCONTENT, 0, 0);
    g_discoveredList.clear();

    auto currentApps = Discovery::GetActiveAppsSet();
    for (const auto& app : currentApps) {
        std::wstring status = appRules.count(app) ? L" [Set]" : L" [New]";
        SendMessage(g_hwndListBox, LB_ADDSTRING, 0, (LPARAM)(app + status).c_str());
        g_discoveredList.push_back(app);
    }
}

LRESULT CALLBACK ConfigWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
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
            if (wmId == ID_BTN_REFRESH) RefreshConfigList();
            break;
        }
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE); // Clicking X only hides the window, does not exit the program
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// UI Logic
LRESULT CALLBACK UIWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == TIMER_HIDE_UI) ShowWindow(hwnd, SW_HIDE);
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect; GetClientRect(hwnd, &rect);
        HBRUSH hBrush = CreateSolidBrush(RGB(45, 45, 45));
        FillRect(hdc, &rect, hBrush);
        DeleteObject(hBrush);
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkMode(hdc, TRANSPARENT);
        DrawTextW(hdc, L"Input Switched", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd, &ps);
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void CreateIndicatorWindow() {
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = UIWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"InputSchuynUI";
    RegisterClassW(&wc);

    g_hwndUI = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        L"InputSchuynUI", L"", WS_POPUP,
        0, 0, 150, 40, NULL, NULL, wc.hInstance, NULL
    );
    SetLayeredWindowAttributes(g_hwndUI, 0, 220, LWA_ALPHA); // Slight transparency
}

void ShowIndicator(HWND targetHwnd) {
    if (!g_hwndUI) return;
    
    // Position: Near the center of the active window for now 
    // (UIA caret positioning can be added later)
    RECT rect;
    GetWindowRect(targetHwnd, &rect);
    int x = rect.left + (rect.right - rect.left) / 2 - 75;
    int y = rect.top + (rect.bottom - rect.top) / 2 - 20;

    SetWindowPos(g_hwndUI, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    ShowWindow(g_hwndUI, SW_SHOWNOACTIVATE);
    InvalidateRect(g_hwndUI, NULL, TRUE); // Trigger repaint
    SetTimer(g_hwndUI, TIMER_HIDE_UI, 1000, NULL); // Hide after 1s
}

// Event listener logic
void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    if (event == EVENT_SYSTEM_FOREGROUND && hwnd != NULL) {
        LoadRulesJson(); // auto hot-reload rules on window switch
        std::wstring exeName = GetProcessName(hwnd);
        if (appRules.count(exeName)) {
            HKL target = appRules[exeName];
            PostMessage(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)target);
            ShowIndicator(hwnd);
        } else {
            PostMessage(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)LANG_DEFAULT);
        }
    }
}

int main() {
    CreateIndicatorWindow();

    // Init GUI for configuration panel
    WNDCLASSW cc = { 0 };
    cc.lpfnWndProc = ConfigWndProc;
    cc.hInstance = GetModuleHandle(NULL);
    cc.lpszClassName = L"ConfigPannelClass";
    cc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&cc);

    g_hwndConfigPannel = CreateWindowExW(0, L"ConfigPannelClass", L"InputSchuyn Config Panel", 
        WS_OVERLAPPEDWINDOW, 100, 100, 400, 500, NULL, NULL, cc.hInstance, NULL);

    g_hwndListBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", NULL, 
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY, 20, 20, 340, 320, g_hwndConfigPannel, (HMENU)ID_LISTBOX_APPS, cc.hInstance, NULL);

    CreateWindowW(L"BUTTON", L"Set EN", WS_CHILD | WS_VISIBLE, 20, 360, 100, 35, g_hwndConfigPannel, (HMENU)ID_BTN_ADD_EN, cc.hInstance, NULL);
    CreateWindowW(L"BUTTON", L"Set ZH", WS_CHILD | WS_VISIBLE, 140, 360, 100, 35, g_hwndConfigPannel, (HMENU)ID_BTN_ADD_ZH, cc.hInstance, NULL);
    CreateWindowW(L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE, 260, 360, 100, 35, g_hwndConfigPannel, (HMENU)ID_BTN_REFRESH, cc.hInstance, NULL);

    LoadRulesJson();
    RefreshConfigList();
    ShowWindow(g_hwndConfigPannel, SW_SHOW);
    
    HWINEVENTHOOK hook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );
    std::wcout << L"--- InputSchuyn Running (v1.3) ---" << std::endl;
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWinEvent(hook);
    return 0;
}