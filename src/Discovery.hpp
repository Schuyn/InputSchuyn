/*
 * @Author: Chuyang Su cs4570@columbia.edu
 * @Date: 2026-02-11 14:50:41
 * @LastEditTime: 2026-02-11 15:07:29
 * @FilePath: /InputSchuyn/src/Discovery.hpp
 * @Description: 
 * 专门用于扫描系统中当前活跃的窗口和它们的输入法状态，提供给主程序进行决策
 */
#pragma once
#include <windows.h>
#include <set>
#include <string>
#include <iostream>

// GetProcessName 
extern std::wstring GetProcessName(HWND hwnd);

namespace Discovery {

    // 这是一个“过滤器”，只负责把发现的 .exe 名字存起来
    BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lParam) {
        // 只看“看得见”的窗口，忽略后台乱七八糟的系统组件
        if (!IsWindowVisible(hwnd)) return TRUE;

        // 获取这个窗口所属的进程名（例如 Code.exe）
        std::wstring exe = GetProcessName(hwnd);
        
        // 如果名字合法，就把它存进我们的“集合”里
        if (exe != L"unknown" && !exe.empty()) {
            auto* pSet = reinterpret_cast<std::set<std::wstring>*>(lParam);
            pSet->insert(exe);
        }
        return TRUE;
    }

    void Run() {
        std::set<std::wstring> foundApps;
        
        std::wcout << L"\n--- [Discovery Mode] Scanning Active Apps ---" << std::endl;

        // 开始全城搜索：把所有窗口都点一遍名
        EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&foundApps));

        // 搜索完了，把集合里的名字一个一个印出来
        for (const auto& app : foundApps) {
            std::wcout << L" > " << app << std::endl;
        }

        std::wcout << L"--- Discovery Complete ---\n" << std::endl;
        
        // 关键一步：强制把上面这些话立刻印到屏幕上，不要憋在内存里
        std::wcout.flush(); 
    }
}