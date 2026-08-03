#include <windows.h>
#include "logger.h"
#include "i18n.h"
#include "deployment.h"
#include "rclone_manager.h"

#define ID_BTN_CONNECT 1
#define ID_BTN_DISCONNECT 2

HWND hUrlBox, hUserBox, hPassBox, hDriveBox;
char g_rclonePath[MAX_PATH] = { 0 };

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        CreateWindowW(L"STATIC", TR("LBL_URL"), WS_VISIBLE | WS_CHILD, 20, 20, 100, 20, hwnd, NULL, NULL, NULL);
        hUrlBox = CreateWindowW(L"EDIT", L"http://192.168.5.100:50055/music/", WS_VISIBLE | WS_CHILD | WS_BORDER, 130, 20, 250, 20, hwnd, NULL, NULL, NULL);

        CreateWindowW(L"STATIC", TR("LBL_USER"), WS_VISIBLE | WS_CHILD, 20, 50, 100, 20, hwnd, NULL, NULL, NULL);
        hUserBox = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER, 130, 50, 250, 20, hwnd, NULL, NULL, NULL);

        CreateWindowW(L"STATIC", TR("LBL_PASS"), WS_VISIBLE | WS_CHILD, 20, 80, 100, 20, hwnd, NULL, NULL, NULL);
        hPassBox = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_PASSWORD, 130, 80, 250, 20, hwnd, NULL, NULL, NULL);

        CreateWindowW(L"STATIC", TR("LBL_DRIVE"), WS_VISIBLE | WS_CHILD, 20, 110, 100, 20, hwnd, NULL, NULL, NULL);
        hDriveBox = CreateWindowW(L"EDIT", L"Z", WS_VISIBLE | WS_CHILD | WS_BORDER, 130, 110, 40, 20, hwnd, NULL, NULL, NULL);

        CreateWindowW(L"BUTTON", TR("BTN_MOUNT"), WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 130, 150, 100, 30, hwnd, (HMENU)ID_BTN_CONNECT, NULL, NULL);
        CreateWindowW(L"BUTTON", TR("BTN_UNMOUNT"), WS_VISIBLE | WS_CHILD, 240, 150, 100, 30, hwnd, (HMENU)ID_BTN_DISCONNECT, NULL, NULL);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BTN_CONNECT) {
            char url[256], user[128], pass[128], drive[8];
            GetWindowTextA(hUrlBox, url, sizeof(url));
            GetWindowTextA(hUserBox, user, sizeof(user));
            GetWindowTextA(hPassBox, pass, sizeof(pass));
            GetWindowTextA(hDriveBox, drive, sizeof(drive));

            LogMessage("INFO", "Mount action triggered.");
            if (StartRcloneMount(g_rclonePath, url, user, pass, drive)) {
				MessageBoxW(hwnd, TR("MSG_MOUNT_OK"), TR("MSG_INFO"), MB_OK | MB_ICONINFORMATION);
			} else {
				MessageBoxW(hwnd, TR("MSG_MOUNT_FAIL"), TR("MSG_ERROR"), MB_OK | MB_ICONERROR);
			}
        } else if (LOWORD(wParam) == ID_BTN_DISCONNECT) {
            LogMessage("INFO", "Unmount action triggered.");
            StopRcloneMount();
            MessageBoxW(hwnd, TR("MSG_UNMOUNT_OK"), TR("MSG_INFO"), MB_OK | MB_ICONINFORMATION);
        }
        break;

    case WM_DESTROY:
        StopRcloneMount();
        FreeI18n();
        CloseLogger();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    InitLogger();
    LogMessage("INFO", "Application boot sequence started.");

    // First initialize environment to extract embedded language files to temp path
    if (!InitializeEnvironment(g_rclonePath, sizeof(g_rclonePath))) {
        LogMessage("ERROR", "Environment initialization failed.");
        MessageBoxW(NULL, L"Environment initialization failed! Check logs.", L"Error", MB_OK | MB_ICONERROR);
        CloseLogger();
        return 1;
    }

    // Now load language settings from the extracted location
    LANGID langId = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(langId) == LANG_CHINESE) {
        InitI18n("zh");
    } else {
        InitI18n("en");
    }

    const wchar_t* CLASS_NAME = L"EmbeddedWebDavClientClass";
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, TR("UI_TITLE"), WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 240, NULL, NULL, hInstance, NULL);

    if (hwnd == NULL) {
        LogMessage("ERROR", "Failed to create main window.");
        FreeI18n();
        CloseLogger();
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);

    MSG msg = { 0 };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}