#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "logger.h"
#include "i18n.h"
#include "deployment.h"
#include "rclone_manager.h"
#include "config.h"

#pragma comment(lib, "comctl32.lib")

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
static int g_trayVisible = 0; // 记录托盘图标是否在任务栏显示

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

    // 1. 盘符格式校验：必须是单个大写字母 (A-Z)
    if (strlen(g_config.drive) != 1 || g_config.drive[0] < 'A' || g_config.drive[0] > 'Z') {
        LogMessage("ERROR", "Invalid drive letter: '%s'. Must be a single uppercase letter (A-Z).", g_config.drive);
        if (!isAuto) {
            MessageBoxW(hwnd, TR("MSG_INVALID_DRIVE"), TR("MSG_ERROR"), MB_OK | MB_ICONWARNING);
        }
        return;
    }

    // 2. 盘符系统冲突检测
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
        if (!isAuto) {
            MessageBoxW(hwnd, TR("MSG_MOUNT_OK"), TR("MSG_INFO"), MB_OK | MB_ICONINFORMATION);
        }
    } else {
        g_isMounted = 0;
        SetWindowTextW(hActionBtn, TR("STR_MOUNT_BTN"));
        if (!isAuto) {
            MessageBoxW(hwnd, TR("MSG_MOUNT_FAIL"), TR("MSG_ERROR"), MB_OK | MB_ICONERROR);
        }
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        // 初始化 Common Controls
        INITCOMMONCONTROLSEX iccex = { sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES };
        InitCommonControlsEx(&iccex);

        // 1. 创建普通控件字体：微软雅黑 13号 (-17)
        g_hFont = CreateFontW(
            -17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei"
        );

        // 2. 创建固定标签字体：微软雅黑 15号 加粗 (-20, FW_BOLD)
        g_hBoldFont = CreateFontW(
            -20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei"
        );

        LoadConfig(&g_config);

        // 初始化日志状态
        SetDebugLogEnabled(g_config.debug_log);

        // 注册全局热键 Ctrl + Shift + M
        RegisterHotKey(hwnd, ID_HOTKEY, MOD_CONTROL | MOD_SHIFT, 'M');

        // 主机地址
        CreateBoldLabelW(TR("STR_HOST"), 30, 25, 110, 28, hwnd);
        hHostBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.host, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 25, 390, 28, hwnd, NULL, NULL, NULL);

        // 端口 & SSL
        CreateBoldLabelW(TR("STR_PORT"), 30, 70, 110, 28, hwnd);
        hPortBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.port, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, 145, 70, 130, 28, hwnd, NULL, NULL, NULL);
        hSslCheck = CreateStyledWindowExW(0, L"BUTTON", TR("STR_SSL"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 295, 72, 240, 25, hwnd, NULL, NULL, NULL);
        if (g_config.ssl) SendMessageA(hSslCheck, BM_SETCHECK, BST_CHECKED, 0);

        // 路径
        CreateBoldLabelW(TR("STR_PATH"), 30, 115, 110, 28, hwnd);
        hPathBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.path, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 115, 390, 28, hwnd, NULL, NULL, NULL);

        // 用户名
        CreateBoldLabelW(TR("STR_USER"), 30, 160, 110, 28, hwnd);
        hUserBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.user, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 160, 390, 28, hwnd, NULL, NULL, NULL);

        // 密码
        CreateBoldLabelW(TR("STR_PASS"), 30, 205, 110, 28, hwnd);
        hPassBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.pass, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD, 145, 205, 390, 28, hwnd, NULL, NULL, NULL);

        // 盘符 & 开机自启勾选框 & 调试日志勾选框
        CreateBoldLabelW(TR("STR_DRIVE"), 30, 250, 110, 28, hwnd);
        hDriveBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.drive, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_UPPERCASE, 145, 250, 50, 28, hwnd, NULL, NULL, NULL);
        
        // 开机自启复选框 (ID=3)
        hAutoStartCheck = CreateStyledWindowExW(0, L"BUTTON", TR("STR_AUTO_START"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 205, 252, 160, 25, hwnd, (HMENU)3, NULL, NULL);
        if (g_config.auto_start) SendMessageA(hAutoStartCheck, BM_SETCHECK, BST_CHECKED, 0);

        // 调试日志复选框 (ID=5)
        hDebugCheck = CreateStyledWindowExW(0, L"BUTTON", TR("STR_DEBUG_LOG"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 370, 252, 165, 25, hwnd, (HMENU)5, NULL, NULL);
        if (g_config.debug_log) SendMessageA(hDebugCheck, BM_SETCHECK, BST_CHECKED, 0);

        // 底部三个按钮横向排布：挂载(ID=1)、隐藏(ID=7)、退出(ID=4)
        hActionBtn = CreateStyledWindowExW(0, L"BUTTON", TR("STR_MOUNT_BTN"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 30, 315, 160, 42, hwnd, (HMENU)1, NULL, NULL);
        hHideBtn   = CreateStyledWindowExW(0, L"BUTTON", TR("STR_HIDE_BTN"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 205, 315, 175, 42, hwnd, (HMENU)7, NULL, NULL);
        hExitBtn   = CreateStyledWindowExW(0, L"BUTTON", TR("STR_TRAY_EXIT"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 395, 315, 155, 42, hwnd, (HMENU)4, NULL, NULL);

        // 为“隐藏”按钮绑定悬浮 Tooltip
        HWND hToolTip = CreateWindowExW(
            WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, NULL, ((LPCREATESTRUCT)lParam)->hInstance, NULL
        );

        if (hToolTip) {
            // 强制将 Tooltip 置于最顶层，防止被控件遮挡
            SetWindowPos(hToolTip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

            TOOLINFOW toolInfo = { 0 };
            toolInfo.cbSize = sizeof(TOOLINFOW);
            toolInfo.hwnd = hwnd;
            toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS; // 自动子类化接管鼠标悬浮事件
            toolInfo.uId = (UINT_PTR)hHideBtn;
            toolInfo.lpszText = (LPWSTR)TR("STR_HIDE_TIP");

            // 限制最大宽度以保证渲染正常
            SendMessageW(hToolTip, TTM_SETMAXTIPWIDTH, 0, 300);
            SendMessageW(hToolTip, TTM_ADDTOOLW, 0, (LPARAM)&toolInfo);
            SendMessageW(hToolTip, TTM_ACTIVATE, TRUE, 0);
        }

        // 注册系统托盘图标
        AddTrayIcon(hwnd);

        // 如果开启了自启，在后台自动挂载
        if (g_config.auto_start) {
            ExecuteMount(hwnd, 1);
        }
        break;
    }
    case WM_HOTKEY:
        if (wParam == ID_HOTKEY) {
            AddTrayIcon(hwnd); // 热键唤醒时同步恢复托盘图标
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        }
        break;
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP) {
            ShowWindow(hwnd, SW_SHOW);
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
            // 点击主页面隐藏按钮或托盘“隐藏托盘”选项：同时隐藏窗口和托盘图标
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
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        }
        break;
    case WM_CLOSE:
        // 点击窗口 X 关闭按钮：仅隐藏窗口，保留托盘图标（最小化到托盘）
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(hwnd, ID_HOTKEY); // 销毁全局热键
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

    const wchar_t* CLASS_NAME = L"WebDavClientClass";
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, TR("STR_TITLE"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 580, 480,
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
    return 0;
}