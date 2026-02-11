/*
 * @Author: Chuyang Su cs4570@columbia.edu
 * @Date: 2026-02-11 14:50:41
 * @LastEditTime: 2026-02-11 15:03:48
 * @FilePath: /InputSchuyn/src/Discovery.hpp
 * @Description: 
 * 专门用于扫描系统中当前活跃的窗口和它们的输入法状态，提供给主程序进行决策
 */
#pragma once
#include <windows.h>
#include <set>
#include <string>
#include <iostream>
#include <iomanip> // For formatting the output

// Using 'extern' to reference the function defined in main.cpp
// This avoids code duplication
extern std::wstring GetProcessName(HWND hwnd);

namespace Discovery {

    // Struct to store window info for a cleaner display
    struct WindowInfo {
        std::wstring exeName;
        std::wstring title;

        // Set needs a way to compare for deduplication
        bool operator<(const WindowInfo& other) const {
            return exeName < other.exeName;
        }
    };

    // Callback function for EnumWindows
    BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lParam) {
        if (!IsWindowVisible(hwnd)) return TRUE;

        // Get window title length
        int len = GetWindowTextLengthW(hwnd);
        if (len == 0) return TRUE;

        // Retrieve the process name using your existing helper
        std::wstring exe = GetProcessName(hwnd);
        if (exe == L"unknown" || exe.empty()) return TRUE;
        
        // Retrieve title
        wchar_t titleBuf[256];
        if (GetWindowTextW(hwnd, titleBuf, 256) > 0) {
            auto* pSet = reinterpret_cast<std::set<WindowInfo>*>(lParam);
            pSet->insert({ exe, titleBuf });
        }

        return TRUE;
    }

    // Main discovery runner
    void Run() {
        std::set<WindowInfo> foundApps;
        std::wcout << L"\n==========================================" << std::endl;
        std::wcout << L"    InputSchuyn Discovery: Active Apps    " << std::endl;
        std::wcout << L"==========================================" << std::endl;

        EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&foundApps));

        for (const auto& app : foundApps) {
            std::wcout << L" [App]: " << std::left << std::setw(20) << app.exeName 
                    << L" | [Title]: " << app.title << std::endl;
        }
        std::wcout.flush(); // Force output
    }
}