/*
 * @Author: Chuyang Su cs4570@columbia.edu
 * @Date: 2026-02-11 13:47:46
 * @LastEditTime: 2026-02-11 14:28:46
 * @FilePath: /InputSchuyn/src/main.cpp
 * @Description: 
 * 集成了窗口监控和自动切换输入法的逻辑
 */
#include <windows.h>
#include <iostream>
#include <string>
#include <map>
#include <fstream>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#define HKL_EN (HKL)0x04090409
#define HKL_ZH (HKL)0x08040804
#define TIMER_HIDE_UI 101

// Define your default language
#define LANG_DEFAULT HKL_ZH

// Global storage for rules
std::map<std::wstring, HKL> appRules;
HWND g_hwndUI = NULL;

// --- Helper: Get EXE directory ---
std::wstring GetExeDirectory() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring path(buffer);
    return path.substr(0, path.find_last_of(L"\\/"));
}

// --- UI: Simple Layered Window ---
LRESULT CALLBACK UIWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == TIMER_HIDE_UI) {
        ShowWindow(hwnd, SW_HIDE);
        KillTimer(hwnd, TIMER_HIDE_UI);
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        // Draw a simple background
        RECT rect;
        GetClientRect(hwnd, &rect);
        HBRUSH hBrush = CreateSolidBrush(RGB(45, 45, 45));
        FillRect(hdc, &rect, hBrush);
        DeleteObject(hBrush);
        
        // Draw Text (Language info)
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
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    g_hwndUI = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        L"InputSchuynUI", L"", WS_POPUP,
        0, 0, 150, 40, NULL, NULL, wc.hInstance, NULL
    );
    SetLayeredWindowAttributes(g_hwndUI, 0, 220, LWA_ALPHA); // Slight transparency
}

void ShowIndicator(HWND targetHwnd, bool isEn) {
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

// Simple JSON-like parser for the specific format you provided
void LoadRulesJson() {
    appRules.clear();
    std::wstring configPath = GetExeDirectory() + L"\\rules.json";
    std::wifstream file(configPath);
    
    if (!file.is_open()) {
        std::wcerr << L"[InputSchuyn] Error: rules.json NOT found at: " << configPath << std::endl;
        return;
    }

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
            std::wcout << L"[InputSchuyn] Rule: " << exe << L" -> " << lang << std::endl;
        }
    }
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

void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                           LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    if (event == EVENT_SYSTEM_FOREGROUND && hwnd != NULL) {
        std::wstring exeName = GetProcessName(hwnd);
        
        if (appRules.count(exeName)) {
            // Rule exists: Apply specific language
            HKL target = appRules[exeName];
            PostMessage(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)target);
            ShowIndicator(hwnd, target == HKL_EN);
        } else {
            // No rule found: Reset to DEFAULT instead of inheriting
            PostMessage(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)LANG_DEFAULT);
            // Optional: You can decide whether to show UI for default reset
        }
    }
}

int main() {
    CreateIndicatorWindow();
    LoadRulesJson();

    HWINEVENTHOOK hook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWinEvent(hook);
    return 0;
}