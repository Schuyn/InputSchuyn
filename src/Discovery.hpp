/*
 * @Author: Chuyang Su cs4570@columbia.edu
 * @Date: 2026-02-11 14:50:41
 * @LastEditTime: 2026-02-11 15:24:07
 * @FilePath: /InputSchuyn/src/Discovery.hpp
 * @Description:
 * Scans currently active windows and their input method states, providing data for the main program to make decisions
 */
#pragma once
#include <windows.h>
#include <set>
#include <string>

// GetProcessName 
extern std::wstring GetProcessName(HWND hwnd);

namespace Discovery {
    BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lParam) {
        // Only check visible windows, ignore background system components
        if (!IsWindowVisible(hwnd)) return TRUE;

        // Get the process name of this window (e.g. Code.exe)
        std::wstring exe = GetProcessName(hwnd);
        
        // If the name is valid, add it to our set
        if (exe != L"unknown" && !exe.empty()) {
            auto* pSet = reinterpret_cast<std::set<std::wstring>*>(lParam);
            pSet->insert(exe);
        }
        return TRUE;
    }

    // New function to get the set of active apps
    inline std::set<std::wstring> GetActiveAppsSet() {
        std::set<std::wstring> foundApps;
        EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&foundApps));
        return foundApps;
    }
}