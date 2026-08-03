#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "logger.h"
#include "i18n.h"
#include "deployment.h"
#include "rclone_manager.h"
#include "config.h"

static HWND hHostBox, hPortBox, hPathBox, hSslCheck, hUserBox, hPassBox, hDriveBox, hAutoStartCheck;
static char g_rclonePath[MAX_PATH] = { 0 };
static AppConfig g_config;

// 执行挂载的核心逻辑
void ExecuteMount(HWND hwnd, int isAuto) {
    GetWindowTextA(hHostBox, g_config.host, sizeof(g_config.host));
    GetWindowTextA(hPortBox, g_config.port, sizeof(g_config.port));
    GetWindowTextA(hPathBox, g_config.path, sizeof(g_config.path));
    GetWindowTextA(hUserBox, g_config.user, sizeof(g_config.user));
    GetWindowTextA(hPassBox, g_config.pass, sizeof(g_config.pass));
    GetWindowTextA(hDriveBox, g_config.drive, sizeof(g_config.drive));

    // 1. 盘符格式严格校验：必须是单个大写字母 (A-Z)
    if (strlen(g_config.drive) != 1 || g_config.drive[0] < 'A' || g_config.drive[0] > 'Z') {
        LogMessage("ERROR", "Invalid drive letter: '%s'. Must be a single uppercase letter (A-Z).", g_config.drive);
        if (!isAuto) {
            MessageBoxW(hwnd, TR("MSG_INVALID_DRIVE"), TR("MSG_ERROR"), MB_OK | MB_ICONWARNING);
        }
        return;
    }

    // 2. 盘符系统冲突检测：检查该盘符是否已被系统或其他应用占用
    DWORD logicalDrives = GetLogicalDrives();
    int driveIndex = (int)(toupper((unsigned char)g_config.drive[0]) - 'A');
    if ((logicalDrives & (1 << driveIndex)) != 0) {
        LogMessage("WARN", "Drive letter %c: is already in use on the system.", g_config.drive[0]);
        if (!isAuto) {
            MessageBoxW(hwnd, TR("MSG_DRIVE_IN_USE"), TR("MSG_ERROR"), MB_OK | MB_ICONWARNING);
        }
        return;
    }

    g_config.ssl = (SendMessageA(hSslCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_config.auto_start = (SendMessageA(hAutoStartCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

    SaveConfig(&g_config);
    SetAppAutoStart(g_config.auto_start);

    const char* scheme = g_config.ssl ? "https" : "http";
    char finalUrl[512];
    if (g_config.port[0] != '\0') {
        sprintf_s(finalUrl, sizeof(finalUrl), "%s://%s:%s%s", scheme, g_config.host, g_config.port, g_config.path);
    } else {
        sprintf_s(finalUrl, sizeof(finalUrl), "%s://%s%s", scheme, g_config.host, g_config.path);
    }

    LogMessage("INFO", "Mount action triggered with URL: %s", finalUrl);

    if (StartRcloneMount(g_rclonePath, finalUrl, g_config.user, g_config.pass, g_config.drive)) {
        if (!isAuto) {
            MessageBoxW(hwnd, TR("MSG_MOUNT_OK"), TR("MSG_INFO"), MB_OK | MB_ICONINFORMATION);
        }
    } else {
        if (!isAuto) {
            MessageBoxW(hwnd, TR("MSG_MOUNT_FAIL"), TR("MSG_ERROR"), MB_OK | MB_ICONERROR);
        }
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        LoadConfig(&g_config);

        // 主机地址
        CreateWindowExW(0, L"STATIC", TR("STR_HOST"), WS_CHILD | WS_VISIBLE, 20, 20, 80, 25, hwnd, NULL, NULL, NULL);
        hHostBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.host, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 110, 20, 280, 25, hwnd, NULL, NULL, NULL);

        // 端口 & SSL
        CreateWindowExW(0, L"STATIC", TR("STR_PORT"), WS_CHILD | WS_VISIBLE, 20, 60, 80, 25, hwnd, NULL, NULL, NULL);
        hPortBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.port, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, 110, 60, 100, 25, hwnd, NULL, NULL, NULL);
        hSslCheck = CreateWindowExW(0, L"BUTTON", TR("STR_SSL"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 225, 62, 165, 22, hwnd, NULL, NULL, NULL);
        if (g_config.ssl) SendMessageA(hSslCheck, BM_SETCHECK, BST_CHECKED, 0);

        // 路径
        CreateWindowExW(0, L"STATIC", TR("STR_PATH"), WS_CHILD | WS_VISIBLE, 20, 100, 80, 25, hwnd, NULL, NULL, NULL);
        hPathBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.path, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 110, 100, 280, 25, hwnd, NULL, NULL, NULL);

        // 用户名
        CreateWindowExW(0, L"STATIC", TR("STR_USER"), WS_CHILD | WS_VISIBLE, 20, 140, 80, 25, hwnd, NULL, NULL, NULL);
        hUserBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.user, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 110, 140, 280, 25, hwnd, NULL, NULL, NULL);

        // 密码
        CreateWindowExW(0, L"STATIC", TR("STR_PASS"), WS_CHILD | WS_VISIBLE, 20, 180, 80, 25, hwnd, NULL, NULL, NULL);
        hPassBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.pass, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 110, 180, 280, 25, hwnd, NULL, NULL, NULL);

        // 盘符 & 开机自启勾选框
        CreateWindowExW(0, L"STATIC", TR("STR_DRIVE"), WS_CHILD | WS_VISIBLE, 20, 220, 80, 25, hwnd, NULL, NULL, NULL);
        hDriveBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.drive, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_UPPERCASE, 110, 220, 50, 25, hwnd, NULL, NULL, NULL);
        
        hAutoStartCheck = CreateWindowExW(0, L"BUTTON", TR("STR_AUTO_START"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 180, 222, 210, 22, hwnd, NULL, NULL, NULL);
        if (g_config.auto_start) SendMessageA(hAutoStartCheck, BM_SETCHECK, BST_CHECKED, 0);

        // 挂载与卸载按钮
        CreateWindowExW(0, L"BUTTON", TR("STR_MOUNT_BTN"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 110, 265, 120, 35, hwnd, (HMENU)1, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", TR("STR_UNMOUNT_BTN"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 245, 265, 120, 35, hwnd, (HMENU)2, NULL, NULL);

        // 如果配置了开机自启，窗口创建后自动执行挂载
        if (g_config.auto_start) {
            ExecuteMount(hwnd, 1);
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) {
            ExecuteMount(hwnd, 0);
        } else if (LOWORD(wParam) == 2) {
            LogMessage("INFO", "Unmount action triggered.");
            StopRcloneMount();
            MessageBoxW(hwnd, TR("MSG_UNMOUNT_OK"), TR("MSG_INFO"), MB_OK | MB_ICONINFORMATION);
        }
        break;
    case WM_DESTROY:
        StopRcloneMount();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    system("chcp 65001 >nul");

    InitLogger();
    LogMessage("INFO", "Application boot sequence started.");

    LANGID langId = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(langId) == LANG_CHINESE) {
        InitI18n("zh");
        LogMessage("INFO", "System language detected: Chinese (zh)");
    } else {
        InitI18n("en");
        LogMessage("INFO", "System language detected: English (en)");
    }

    if (!InitializeEnvironment(g_rclonePath, sizeof(g_rclonePath))) {
        MessageBoxA(NULL, "Failed to initialize environment.", "Error", MB_OK | MB_ICONERROR);
        CloseLogger();
        return 1;
    }

    const wchar_t* CLASS_NAME = L"EmbeddedWebDavClientClass";
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
        CW_USEDEFAULT, CW_USEDEFAULT, 435, 360,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) {
        CloseLogger();
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
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