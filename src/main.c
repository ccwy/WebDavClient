#include <windows.h>
#include <stdio.h>
#include "logger.h"
#include "i18n.h"
#include "deployment.h"
#include "rclone_manager.h"

static HWND hHostBox, hPortBox, hPathBox, hSslCheck, hUserBox, hPassBox, hDriveBox;
static char g_rclonePath[MAX_PATH] = { 0 };

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        // 主机地址
        CreateWindowExW(0, L"STATIC", TR("STR_HOST"), WS_CHILD | WS_VISIBLE, 20, 20, 80, 25, hwnd, NULL, NULL, NULL);
        hHostBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "192.168.5.100", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 110, 20, 280, 25, hwnd, NULL, NULL, NULL);

        // 端口 & SSL
        CreateWindowExW(0, L"STATIC", TR("STR_PORT"), WS_CHILD | WS_VISIBLE, 20, 60, 80, 25, hwnd, NULL, NULL, NULL);
        hPortBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "50055", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, 110, 60, 100, 25, hwnd, NULL, NULL, NULL);
        hSslCheck = CreateWindowExW(0, L"BUTTON", TR("STR_SSL"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 225, 62, 135, 22, hwnd, NULL, NULL, NULL);

        // 路径
        CreateWindowExW(0, L"STATIC", TR("STR_PATH"), WS_CHILD | WS_VISIBLE, 20, 100, 80, 25, hwnd, NULL, NULL, NULL);
        hPathBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "/music/", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 110, 100, 280, 25, hwnd, NULL, NULL, NULL);

        // 用户名
        CreateWindowExW(0, L"STATIC", TR("STR_USER"), WS_CHILD | WS_VISIBLE, 20, 140, 80, 25, hwnd, NULL, NULL, NULL);
        hUserBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "www", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 110, 140, 280, 25, hwnd, NULL, NULL, NULL);

        // 密码（明文显示）
        CreateWindowExW(0, L"STATIC", TR("STR_PASS"), WS_CHILD | WS_VISIBLE, 20, 180, 80, 25, hwnd, NULL, NULL, NULL);
        hPassBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "www", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 110, 180, 280, 25, hwnd, NULL, NULL, NULL);

        // 盘符
        CreateWindowExW(0, L"STATIC", TR("STR_DRIVE"), WS_CHILD | WS_VISIBLE, 20, 220, 80, 25, hwnd, NULL, NULL, NULL);
        hDriveBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "Z", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 110, 220, 50, 25, hwnd, NULL, NULL, NULL);

        // 按钮：挂载 (ID: 1) 与 卸载 (ID: 2)
        CreateWindowExW(0, L"BUTTON", TR("STR_MOUNT_BTN"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 110, 265, 120, 35, hwnd, (HMENU)1, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", TR("STR_UNMOUNT_BTN"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 245, 265, 120, 35, hwnd, (HMENU)2, NULL, NULL);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) {
            // 点击挂载
            char host[256], port[32], path[128], user[128], pass[128], drive[8];
            GetWindowTextA(hHostBox, host, sizeof(host));
            GetWindowTextA(hPortBox, port, sizeof(port));
            GetWindowTextA(hPathBox, path, sizeof(path));
            GetWindowTextA(hUserBox, user, sizeof(user));
            GetWindowTextA(hPassBox, pass, sizeof(pass));
            GetWindowTextA(hDriveBox, drive, sizeof(drive));

            int useSsl = (SendMessageA(hSslCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
            const char* scheme = useSsl ? "https" : "http";

            char finalUrl[512];
            if (port[0] != '\0') {
                sprintf_s(finalUrl, sizeof(finalUrl), "%s://%s:%s%s", scheme, host, port, path);
            } else {
                sprintf_s(finalUrl, sizeof(finalUrl), "%s://%s%s", scheme, host, path);
            }

            LogMessage("INFO", "Mount action triggered with URL: %s", finalUrl);

            if (StartRcloneMount(g_rclonePath, finalUrl, user, pass, drive)) {
                MessageBoxW(hwnd, TR("MSG_MOUNT_OK"), TR("MSG_INFO"), MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(hwnd, TR("MSG_MOUNT_FAIL"), TR("MSG_ERROR"), MB_OK | MB_ICONERROR);
            }
        } else if (LOWORD(wParam) == 2) {
            // 点击卸载
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

    // 1. 必须先初始化环境并释放资源（rclone.exe、winfsp.msi 以及 lang 目录下的 ini 文件）
    if (!InitializeEnvironment(g_rclonePath, sizeof(g_rclonePath))) {
        MessageBoxA(NULL, "Failed to initialize environment.", "Error", MB_OK | MB_ICONERROR);
        CloseLogger();
        return 1;
    }

    // 2. 环境释放完成后（lang 目录和 ini 文件已存在），再安全地检测并加载多语言配置
    LANGID langId = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(langId) == LANG_CHINESE) {
        InitI18n("zh");
        LogMessage("INFO", "System language detected: Chinese (zh)");
    } else {
        InitI18n("en");
        LogMessage("INFO", "System language detected: English (en)");
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