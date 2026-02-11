/*
 * @Author: Chuyang Su cs4570@columbia.edu
 * @Date: 2026-02-11 14:50:41
 * @LastEditTime: 2026-02-11 14:50:47
 * @FilePath: /InputSchuyn/src/Discovery.hpp
 * @Description: 
 * 专门用于扫描系统中当前活跃的窗口和它们的输入法状态，提供给主程序进行决策
 */
#pragma once
#include <windows.h>
#include <set>
#include <string>
#include <iostream>

// Use the existing GetProcessName helper
extern std::wstring GetProcessName(HWND hwnd);

namespace Discovery {
    void Run() {
        std::set<std::wstring> apps;
        std::wcout << L"\n--- [InputSchuyn] Discovery: Active Processes ---" << std::endl;

        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            if (IsWindowVisible(hwnd)) {
                int len = GetWindowTextLengthW(hwnd);
                if (len > 0) {
                    std::wstring name = GetProcessName(hwnd);
                    auto* pApps = reinterpret_cast<std::set<std::wstring>*>(lParam);
                    pApps->insert(name);
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&apps));

        for (const auto& app : apps) {
            std::wcout << L"  > " << app << std::endl;
        }
        std::wcout << L"--- End of Discovery ---\n" << std::endl;
    }
}