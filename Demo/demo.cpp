/*
 * @Author: Chuyang Su cs4570@columbia.edu
 * @Date: 2026-02-11 13:31:21
 * @LastEditTime: 2026-02-11 13:40:32
 * @FilePath: /InputSchuyn/src/demo.cpp
 * @Description: 
 
这个 Demo 包含了两个最关键的部分：

监听窗口切换（使用 WinEventHook）。

获取当前光标位置（尝试使用 GetGUIThreadInfo）。
*/
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

// Link essential libraries
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")

// Helper to get the filename from a full path
std::wstring GetFileName(const std::wstring& path) {
    size_t lastSlash = path.find_last_of(L"\\/");
    if (std::wstring::npos == lastSlash) return path;
    return path.substr(lastSlash + 1);
}

// Get the .exe name of the current foreground window
std::wstring GetActiveProcessName() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return L"none";

    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    
    std::wstring processName = L"unknown";
    if (hProcess) {
        wchar_t buffer[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, buffer, &size)) {
            processName = GetFileName(buffer);
        }
        CloseHandle(hProcess);
    }
    return processName;
}

// Attempt to locate the text cursor (Caret)
POINT GetCaretPosition() {
    POINT pt = { 0, 0 };
    GUITHREADINFO gti;
    gti.cbSize = sizeof(GUITHREADINFO);

    // Get the thread ID of the foreground window
    HWND hwnd = GetForegroundWindow();
    DWORD tid = GetWindowThreadProcessId(hwnd, NULL);

    if (GetGUIThreadInfo(tid, &gti)) {
        if (gti.hwndCaret) {
            pt.x = gti.rcCaret.left;
            pt.y = gti.rcCaret.top;
            // Convert coordinate from window-space to screen-space
            ClientToScreen(gti.hwndCaret, &pt);
        }
    }
    return pt;
}

// Callback triggered whenever the foreground window changes
void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                           LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    
    if (event == EVENT_SYSTEM_FOREGROUND && hwnd != NULL) {
        std::wstring exeName = GetActiveProcessName();
        POINT caretPos = GetCaretPosition();

        std::wcout << L"[InputSchuyn] Focus Changed: " << exeName << std::endl;
        
        if (caretPos.x != 0 || caretPos.y != 0) {
            std::wcout << L"  - Caret at: (" << caretPos.x << L", " << caretPos.y << L")" << std::endl;
        } else {
            std::wcout << L"  - Standard caret not detected (likely a non-Win32 UI)." << std::endl;
        }
    }
}

int main() {
    // Basic setup
    std::wcout << L"--- InputSchuyn Monitor Started ---" << std::endl;
    std::wcout << L"Press Ctrl+C in this console to exit." << std::endl;

    // Register the hook for window changes
    HWINEVENTHOOK hook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    if (!hook) {
        std::cerr << "Error: Could not register WinEventHook!" << std::endl;
        return 1;
    }

    // Standard Win32 message loop is required for hooks to work
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    UnhookWinEvent(hook);
    return 0;
}