/*
 * @Author: Chuyang Su cs4570@columbia.edu
 * @Date: 2026-02-11 13:47:46
 * @LastEditTime: 2026-02-11 14:11:29
 * @FilePath: /InputSchuyn/src/main.cpp
 * @Description: 
 * 集成了窗口监控和自动切换输入法的逻辑
 */
#include <windows.h>
#include <iostream>
#include <string>
#include <map>
#include <fstream>
#include <sstream>

// Link Win32 libraries
#pragma comment(lib, "user32.lib")

// --- Configuration ---
// Language IDs
// 0x04090409: English (United States)
// 0x08040804: Chinese (Simplified, China)
#define LANG_EN (HKL)0x04090409
#define LANG_ZH (HKL)0x08040804

// Global rules map
std::map<std::wstring, HKL> appRules;

// Load rules from a simple text file
void LoadRules() {
    appRules.clear();
    std::wifstream file("rules.txt");
    // Use a simple format: ProcessName Language
    // Example: Code.exe EN
    std::wstring line;
    while (std::getline(file, line)) {
        std::wstringstream ss(line);
        std::wstring name, lang;
        if (ss >> name >> lang) {
            appRules[name] = (lang == L"EN" ? LANG_EN : LANG_ZH);
        }
    }
}

// --- Helper Functions ---

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

void SwitchInputMethod(HWND hwnd, const std::wstring& exeName) {
    if (appRules.count(exeName)) {
        HKL target = appRules[exeName];
        // PostMessage is asynchronous and safer for system-wide hooks
        PostMessage(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)target);
        std::wcout << L"[InputSchuyn] Switched " << exeName << L" to " 
                   << (target == LANG_EN ? L"English" : L"Chinese") << std::endl;
    }
}

// --- Event Callback ---

void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                           LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    if (event == EVENT_SYSTEM_FOREGROUND && hwnd != NULL) {
        std::wstring exeName = GetProcessName(hwnd);
        SwitchInputMethod(hwnd, exeName);
    }
}

// --- Main Entry Point ---

int main() {
    std::wcout << L"InputSchuyn v1.0 is running..." << std::endl;
    std::wcout << L"Monitoring window focus changes..." << std::endl;

    // Set up the event hook
    HWINEVENTHOOK hook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    if (!hook) {
        std::cerr << "Failed to install hook!" << std::endl;
        return 1;
    }

    // Standard Win32 message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWinEvent(hook);
    return 0;
}