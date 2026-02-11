/*
 * @Author: Chuyang Su cs4570@columbia.edu
 * @Date: 2026-02-11 13:47:46
 * @LastEditTime: 2026-02-11 14:19:54
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

#define HKL_EN (HKL)0x04090409
#define HKL_ZH (HKL)0x08040804

// Global storage for rules
std::map<std::wstring, HKL> appRules;

// Simple JSON-like parser for the specific format you provided
void LoadRulesJson() {
    appRules.clear();
    std::wifstream file("rules.json");
    if (!file.is_open()) {
        std::wcerr << L"[InputSchuyn] Error: rules.json not found in bin folder!" << std::endl;
        return;
    }

    std::wstring line;
    while (std::getline(file, line)) {
        // Very basic parsing: find "Key": "Value"
        size_t firstQuote = line.find(L'\"');
        size_t secondQuote = line.find(L'\"', firstQuote + 1);
        size_t thirdQuote = line.find(L'\"', secondQuote + 1);
        size_t fourthQuote = line.find(L'\"', thirdQuote + 1);

        if (firstQuote != std::wstring::npos && fourthQuote != std::wstring::npos) {
            std::wstring exeName = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
            std::wstring lang = line.substr(thirdQuote + 1, fourthQuote - thirdQuote - 1);
            appRules[exeName] = (lang == L"EN" ? HKL_EN : HKL_ZH);
            std::wcout << L"[InputSchuyn] Loaded Rule: " << exeName << L" -> " << lang << std::endl;
        }
    }
    file.close();
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
            HKL target = appRules[exeName];
            PostMessage(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)target);
            // Optional: console feedback
            // std::wcout << L"[InputSchuyn] Switched " << exeName << std::endl;
        }
    }
}

int main() {
    std::wcout << L"--- InputSchuyn v1.1 Initializing ---" << std::endl;
    
    // 1. Load your rules.json
    LoadRulesJson();

    // 2. Set the Hook
    HWINEVENTHOOK hook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    if (!hook) {
        std::cerr << "Failed to install hook!" << std::endl;
        return 1;
    }

    std::wcout << L"--- InputSchuyn is Running ---" << std::endl;

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWinEvent(hook);
    return 0;
}