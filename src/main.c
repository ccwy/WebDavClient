#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <wctype.h> 
#include "logger.h"
#include "i18n.h"
#include "deployment.h"
#include "rclone_manager.h"
#include "config.h"

#define WM_TRAYICON   (WM_USER + 101)
#define IDM_SHOW      1001
#define IDM_EXIT      1002
#define IDM_HIDETRAY  1003
#define ID_HOTKEY     1

static HWND hHostBox, hPortBox, hPathBox, hSslCheck, hUserBox, hPassBox, hDriveBox, hAutoStartCheck, hDebugCheck;
static HWND hActionBtn, hHideBtn, hExitBtn;
static char g_rclonePath[MAX_PATH] = { 0 };
static AppConfig g_config;
static NOTIFYICONDATAW g_nid = { 0 };
static HFONT g_hFont = NULL;      
static HFONT g_hBoldFont = NULL;  
static int g_isMounted = 0;       
static int g_trayVisible = 0; 
static UINT WM_WAKEUP = 0; // 自定义唤醒消息标识

// 托盘图标添加与删除辅助函数
static void AddTrayIcon(HWND hwnd) {
    if (!g_trayVisible) {
        g_nid.cbSize = sizeof(NOTIFYICONDATAW);
        g_nid.hWnd = hwnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;
        g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wcscpy_s(g_nid.szTip, sizeof(g_nid.szTip) / sizeof(wchar_t), L"Rclone WebDAV Client");
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        g_trayVisible = 1;
    }
}

static void RemoveTrayIcon() {
    if (g_trayVisible) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_trayVisible = 0;
    }
}

// 创建普通控件字体 (微软雅黑 13号)
static HWND CreateStyledWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int x, int y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
    HWND hwnd = CreateWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    if (hwnd && g_hFont) {
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    }
    return hwnd;
}

static HWND CreateStyledWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int x, int y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
    HWND hwnd = CreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    if (hwnd && g_hFont) {
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    }
    return hwnd;
}

// 创建左侧固定标题标签 (微软雅黑 15号 加粗)
static HWND CreateBoldLabelW(LPCWSTR lpWindowName, int x, int y, int nWidth, int nHeight, HWND hWndParent) {
    HWND hwnd = CreateWindowExW(0, L"STATIC", lpWindowName, WS_CHILD | WS_VISIBLE, x, y, nWidth, nHeight, hWndParent, NULL, NULL, NULL);
    if (hwnd && g_hBoldFont) {
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)g_hBoldFont, TRUE);
    }
    return hwnd;
}

// 执行挂载的核心逻辑
void ExecuteMount(HWND hwnd, int isAuto) {
    GetWindowTextA(hHostBox, g_config.host, sizeof(g_config.host));
    GetWindowTextA(hPortBox, g_config.port, sizeof(g_config.port));
    GetWindowTextA(hPathBox, g_config.path, sizeof(g_config.path));
    GetWindowTextA(hUserBox, g_config.user, sizeof(g_config.user));
    GetWindowTextA(hPassBox, g_config.pass, sizeof(g_config.pass));
    GetWindowTextA(hDriveBox, g_config.drive, sizeof(g_config.drive));

    if (strlen(g_config.drive) != 1 || g_config.drive[0] < 'A' || g_config.drive[0] > 'Z') {
        LogMessage("ERROR", "Invalid drive letter: '%s'. Must be a single uppercase letter (A-Z).", g_config.drive);
        if (!isAuto) MessageBoxW(hwnd, TR("MSG_INVALID_DRIVE"), TR("MSG_ERROR"), MB_OK | MB_ICONWARNING);
        return;
    }

    DWORD logicalDrives = GetLogicalDrives();
    int driveIndex = (int)(toupper((unsigned char)g_config.drive[0]) - 'A');
    if (!isAuto && (logicalDrives & (1 << driveIndex)) != 0) {
        LogMessage("WARN", "Drive letter %c: is already in use on the system.", g_config.drive[0]);
        MessageBoxW(hwnd, TR("MSG_DRIVE_IN_USE"), TR("MSG_ERROR"), MB_OK | MB_ICONWARNING);
        return;
    }

    g_config.ssl = (SendMessageA(hSslCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_config.auto_start = (SendMessageA(hAutoStartCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_config.debug_log = (SendMessageA(hDebugCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

    SaveConfig(&g_config);

    const char* scheme = g_config.ssl ? "https" : "http";
    char finalUrl[512];
    if (g_config.port[0] != '\0') {
        sprintf_s(finalUrl, sizeof(finalUrl), "%s://%s:%s%s", scheme, g_config.host, g_config.port, g_config.path);
    } else {
        sprintf_s(finalUrl, sizeof(finalUrl), "%s://%s%s", scheme, g_config.host, g_config.path);
    }

    LogMessage("INFO", "Mount action triggered with URL: %s", finalUrl);

    if (StartRcloneMount(g_rclonePath, finalUrl, g_config.user, g_config.pass, g_config.drive, g_config.debug_log)) {
        g_isMounted = 1;
        SetWindowTextW(hActionBtn, TR("STR_UNMOUNT_BTN")); 
        if (!isAuto) MessageBoxW(hwnd, TR("MSG_MOUNT_OK"), TR("MSG_INFO"), MB_OK | MB_ICONINFORMATION);
    } else {
        g_isMounted = 0;
        SetWindowTextW(hActionBtn, TR("STR_MOUNT_BTN"));
        if (!isAuto) MessageBoxW(hwnd, TR("MSG_MOUNT_FAIL"), TR("MSG_ERROR"), MB_OK | MB_ICONERROR);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // 处理二次运行实例发送来的唤醒消息
    if (uMsg == WM_WAKEUP && WM_WAKEUP != 0) {
        AddTrayIcon(hwnd); 
        ShowWindow(hwnd, SW_RESTORE); // 使用 RESTORE 可以从最小化状态恢复
        SetForegroundWindow(hwnd);
        return 0;
    }

    switch (uMsg) {
    case WM_CREATE: {
        g_hFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei");
        g_hBoldFont = CreateFontW(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei");

        LoadConfig(&g_config);
        SetDebugLogEnabled(g_config.debug_log);
        RegisterHotKey(hwnd, ID_HOTKEY, MOD_CONTROL | MOD_SHIFT, 'M');

        CreateBoldLabelW(TR("STR_HOST"), 30, 25, 110, 28, hwnd);
        hHostBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.host, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 25, 390, 28, hwnd, NULL, NULL, NULL);

        CreateBoldLabelW(TR("STR_PORT"), 30, 70, 110, 28, hwnd);
        hPortBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.port, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, 145, 70, 130, 28, hwnd, NULL, NULL, NULL);
        hSslCheck = CreateStyledWindowExW(0, L"BUTTON", TR("STR_SSL"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 295, 72, 240, 25, hwnd, NULL, NULL, NULL);
        if (g_config.ssl) SendMessageA(hSslCheck, BM_SETCHECK, BST_CHECKED, 0);

        CreateBoldLabelW(TR("STR_PATH"), 30, 115, 110, 28, hwnd);
        hPathBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.path, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 115, 390, 28, hwnd, NULL, NULL, NULL);

        CreateBoldLabelW(TR("STR_USER"), 30, 160, 110, 28, hwnd);
        hUserBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.user, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 160, 390, 28, hwnd, NULL, NULL, NULL);

        CreateBoldLabelW(TR("STR_PASS"), 30, 205, 110, 28, hwnd);
        hPassBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.pass, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD, 145, 205, 390, 28, hwnd, NULL, NULL, NULL);

        CreateBoldLabelW(TR("STR_DRIVE"), 30, 250, 110, 28, hwnd);
        hDriveBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.drive, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_UPPERCASE, 145, 250, 50, 28, hwnd, NULL, NULL, NULL);
        
        hAutoStartCheck = CreateStyledWindowExW(0, L"BUTTON", TR("STR_AUTO_START"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 205, 252, 160, 25, hwnd, (HMENU)3, NULL, NULL);
        if (g_config.auto_start) SendMessageA(hAutoStartCheck, BM_SETCHECK, BST_CHECKED, 0);

        hDebugCheck = CreateStyledWindowExW(0, L"BUTTON", TR("STR_DEBUG_LOG"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 370, 252, 165, 25, hwnd, (HMENU)5, NULL, NULL);
        if (g_config.debug_log) SendMessageA(hDebugCheck, BM_SETCHECK, BST_CHECKED, 0);

        hActionBtn = CreateStyledWindowExW(0, L"BUTTON", TR("STR_MOUNT_BTN"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 30, 315, 160, 42, hwnd, (HMENU)1, NULL, NULL);
        hHideBtn   = CreateStyledWindowExW(0, L"BUTTON", TR("STR_HIDE_BTN"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 205, 315, 175, 42, hwnd, (HMENU)7, NULL, NULL);
        hExitBtn   = CreateStyledWindowExW(0, L"BUTTON", TR("STR_TRAY_EXIT"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 395, 315, 155, 42, hwnd, (HMENU)4, NULL, NULL);

        CreateStyledWindowExW(0, L"STATIC", TR("STR_HIDE_TIP"), WS_CHILD | WS_VISIBLE | SS_CENTER, 30, 375, 520, 25, hwnd, NULL, NULL, NULL);

        AddTrayIcon(hwnd);

        if (g_config.auto_start) {
            ExecuteMount(hwnd, 1);
        }
        break;
    }
    case WM_HOTKEY:
        if (wParam == ID_HOTKEY) {
            AddTrayIcon(hwnd); 
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        break;
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_SHOW, TR("STR_TRAY_SHOW"));
            AppendMenuW(hMenu, MF_STRING, IDM_HIDETRAY, TR("STR_TRAY_HIDE"));
            AppendMenuW(hMenu, MF_STRING, IDM_EXIT, TR("STR_TRAY_EXIT"));
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) {
            if (g_isMounted == 0) {
                ExecuteMount(hwnd, 0);
            } else {
                LogMessage("INFO", "Unmount action triggered.");
                StopRcloneMount();
                g_isMounted = 0;
                SetWindowTextW(hActionBtn, TR("STR_MOUNT_BTN")); 
                MessageBoxW(hwnd, TR("MSG_UNMOUNT_OK"), TR("MSG_INFO"), MB_OK | MB_ICONINFORMATION);
            }
        } else if (LOWORD(wParam) == 7 || LOWORD(wParam) == IDM_HIDETRAY) {
            RemoveTrayIcon();
            ShowWindow(hwnd, SW_HIDE);
        } else if (LOWORD(wParam) == 3) {
            int checked = (SendMessageA(hAutoStartCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_config.auto_start = checked;
            SetAppAutoStart(checked);
            SaveConfig(&g_config);
        } else if (LOWORD(wParam) == 5) {
            int checked = (SendMessageA(hDebugCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_config.debug_log = checked;
            SetDebugLogEnabled(checked);
            SaveConfig(&g_config);
            LogMessage("INFO", "Debug log toggled dynamically to: %d", checked);
        } else if (LOWORD(wParam) == 4 || LOWORD(wParam) == IDM_EXIT) {
            RemoveTrayIcon();
            DestroyWindow(hwnd);
        } else if (LOWORD(wParam) == IDM_SHOW) {
            AddTrayIcon(hwnd);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        break;
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(hwnd, ID_HOTKEY);
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hBoldFont) DeleteObject(g_hBoldFont);
        RemoveTrayIcon();
        StopRcloneMount();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 1. 获取当前程序执行路径，生成基于路径的唯一标识（转换 \ 和 : 为 _，并全部小写化）
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    wchar_t uniqueId[MAX_PATH];
    wcscpy_s(uniqueId, MAX_PATH, exePath);
    for (int i = 0; uniqueId[i] != L'\0'; i++) {
        uniqueId[i] = towlower(uniqueId[i]); // 统一转小写防止路径大小写导致的漏判
        if (uniqueId[i] == L'\\' || uniqueId[i] == L':') {
            uniqueId[i] = L'_';
        }
    }
    
    // 生成基于当前路径的唯一窗口类名和互斥体名称
    wchar_t uniqueClassName[MAX_PATH + 50];
    swprintf_s(uniqueClassName, MAX_PATH + 50, L"WebDavClientClass_%s", uniqueId);

    // 2. 注册系统级全局唤醒消息
    WM_WAKEUP = RegisterWindowMessageW(L"WebDavClientWakeupMessage");

    // 3. 互斥体单实例检测机制
    HANDLE hMutex = CreateMutexW(NULL, FALSE, uniqueClassName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // 如果当前路径下已有实例运行，查找它的主窗口
        HWND hExistingWnd = FindWindowW(uniqueClassName, NULL);
        if (hExistingWnd) {
            // 发送自定义唤醒消息唤醒旧实例
            SendMessageW(hExistingWnd, WM_WAKEUP, 0, 0);
        }
        CloseHandle(hMutex);
        return 0; // 新实例直接退出
    }

    InitLogger();
    LogMessage("INFO", "Application boot sequence started.");

    if (!InitializeEnvironment(g_rclonePath, sizeof(g_rclonePath))) {
        MessageBoxA(NULL, "Failed to initialize environment.", "Error", MB_OK | MB_ICONERROR);
        CloseLogger();
        return 1;
    }

    LANGID langId = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(langId) == LANG_CHINESE) {
        InitI18n("zh");
    } else {
        InitI18n("en");
    }

    int startInTray = 0;
    if (lpCmdLine && (strstr(lpCmdLine, "tray") != NULL || strstr(lpCmdLine, "TRAY") != NULL)) {
        startInTray = 1;
    }

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    // 使用基于路径计算出的唯一类名注册窗口
    wc.lpszClassName = uniqueClassName;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    // 计算屏幕中央的坐标
    int windowWidth = 580;
    int windowHeight = 480;
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenWidth - windowWidth) / 2;
    int posY = (screenHeight - windowHeight) / 2;

    HWND hwnd = CreateWindowExW(
        0, uniqueClassName, TR("STR_TITLE"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        posX, posY, windowWidth, windowHeight, 
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) {
        CloseLogger();
        return 0;
    }

    ShowWindow(hwnd, startInTray ? SW_HIDE : nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = { 0 };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    FreeI18n();
    CloseLogger();
    // 互斥体会随着程序主进程退出而自动被系统清理，无需手动 CloseHandle
    return 0;
}