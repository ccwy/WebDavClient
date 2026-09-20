#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wctype.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include "logger.h"
#include "i18n.h"
#include "deployment.h"
#include "rclone_manager.h"
#include "config.h"
#include "protocol.h"
#include "protocol_webdav.h"
#include "protocol_smb.h"
#include "protocol_sftp.h"
#include "protocol_ftp.h"
#include "list_page.h"
#include "config_page.h"

/* ======================================================================
   WebDavClient — 批量管理模式主入口
   main.c 仅负责：字体/托盘/热键管理、页面协调（列表页↔配置页）、
   全局高级设置对话框、自动挂载、窗口生命周期
   ====================================================================== */

#define WM_TRAYICON   (WM_USER + 101)
#define IDM_SHOW      9001
#define IDM_EXIT      9002
#define IDM_HIDETRAY  9003
#define ID_HOTKEY     1

#define PAGE_LIST     0
#define PAGE_CONFIG   1

/* ---- 全局高级设置对话框控件 ID ---- */
#define GAD_ID_AUTO_START   4001
#define GAD_ID_DEBUG_LOG    4002
#define GAD_ID_AUTO_HIDE    4003
#define GAD_ID_OK           IDOK
#define GAD_ID_CANCEL       IDCANCEL

/* ---- 全局状态 ---- */
static AppConfig g_appCfg;
static ListPageData g_listPage;
static ConfigPageData g_configPage;
static char g_rclonePath[MAX_PATH] = { 0 };
static NOTIFYICONDATAW g_nid = { 0 };
static HFONT g_hFont = NULL;
static HFONT g_hBoldFont = NULL;
static HFONT g_hDescFont = NULL;
static int g_trayVisible = 0;
static int g_pageState = PAGE_LIST;
static UINT WM_WAKEUP = 0;

/* ---- 协议处理器工厂 ---- */
static ProtocolHandler* CreateHandlerForProtocol(const char* protocol,
                                                  ConnectionConfig* connCfg,
                                                  GlobalConfig* globalCfg) {
    if (strcmp(protocol, "webdav") == 0) return CreateWebDavHandler(connCfg, globalCfg);
    if (strcmp(protocol, "smb") == 0)    return CreateSmbHandler(connCfg, globalCfg);
    if (strcmp(protocol, "sftp") == 0)   return CreateSftpHandler(connCfg, globalCfg);
    if (strcmp(protocol, "ftp") == 0)    return CreateFtpHandler(connCfg, globalCfg);
    return NULL;
}

/* ---- 托盘图标辅助函数 ---- */
static void AddTrayIcon(HWND hwnd) {
    if (!g_trayVisible) {
        g_nid.cbSize = sizeof(NOTIFYICONDATAW);
        g_nid.hWnd = hwnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;
        g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wcscpy_s(g_nid.szTip, sizeof(g_nid.szTip) / sizeof(wchar_t), TR("STR_TITLE"));
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        g_trayVisible = 1;
    }
}

static void RemoveTrayIcon(void) {
    if (g_trayVisible) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_trayVisible = 0;
    }
}

static void HideWindowAndTray(HWND hwnd) {
    RemoveTrayIcon();
    ShowWindow(hwnd, SW_HIDE);
}

/* ======================================================================
   页面回调实现
   ====================================================================== */

static void OnAddConnection(void* ctx) {
    (void)ctx;
    g_pageState = PAGE_CONFIG;
    ListPage_Hide(&g_listPage);
    ConfigPage_ShowForAdd(&g_configPage);
}

static void OnEditConnection(void* ctx, const char* connId) {
    (void)ctx;
    g_pageState = PAGE_CONFIG;
    ListPage_Hide(&g_listPage);
    ConfigPage_ShowForEdit(&g_configPage, connId);
}

static void OnAdvSettings(void* ctx, const char* connId) {
    (void)ctx;
    g_pageState = PAGE_CONFIG;
    ListPage_Hide(&g_listPage);
    ConfigPage_ShowForEdit(&g_configPage, connId);
    ConfigPage_ShowAdv(&g_configPage);
}

static void OnGlobalAdvSettings(void* ctx);

static void OnExit(void* ctx) {
    HWND hwnd = (HWND)ctx;
    RemoveTrayIcon();
    DestroyWindow(hwnd);
}

static void OnHide(void* ctx) {
    HWND hwnd = (HWND)ctx;
    HideWindowAndTray(hwnd);
}

static void OnConfigSave(void* ctx) {
    (void)ctx;
    g_pageState = PAGE_LIST;
    ConfigPage_Hide(&g_configPage);
    ListPage_Show(&g_listPage);
}

static void OnConfigCancel(void* ctx) {
    (void)ctx;
    g_pageState = PAGE_LIST;
    ConfigPage_Hide(&g_configPage);
    ListPage_Show(&g_listPage);
}

/* ======================================================================
   全局高级设置对话框（模态弹出窗口）
   ====================================================================== */

static LRESULT CALLBACK GlobalAdvWndProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == GAD_ID_OK) {
            /* 读取复选框状态并保存 */
            g_appCfg.global.auto_start = (SendMessageW(GetDlgItem(hDlg, GAD_ID_AUTO_START),
                                             BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
            g_appCfg.global.debug_log = (SendMessageW(GetDlgItem(hDlg, GAD_ID_DEBUG_LOG),
                                            BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
            g_appCfg.global.auto_hide = (SendMessageW(GetDlgItem(hDlg, GAD_ID_AUTO_HIDE),
                                            BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
            SetAppAutoStart(g_appCfg.global.auto_start);
            SetDebugLogEnabled(g_appCfg.global.debug_log);
            SaveGlobalConfig(&g_appCfg.global);
            DestroyWindow(hDlg);
        } else if (LOWORD(wParam) == GAD_ID_CANCEL) {
            DestroyWindow(hDlg);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hDlg);
        return 0;
    }
    return DefWindowProcW(hDlg, uMsg, wParam, lParam);
}

/* 全局高级设置窗口类名 */
static const wchar_t* GADV_CLASS = L"GlobalAdvDlgClass";
static int g_advClassRegistered = 0;

static void OnGlobalAdvSettings(void* ctx) {
    HWND hwndOwner = (HWND)ctx;
    /* 使用编程方式创建模态弹出窗口 */
    int dlgW = 340, dlgH = 200;
    int screenW, screenH, posX, posY;
    RECT rcOwner;
    HWND hDlg, hAutoStart, hDebugLog, hAutoHide, hOkBtn, hCancelBtn;
    MSG msg;

    /* 注册窗口类（仅一次） */
    if (!g_advClassRegistered) {
        WNDCLASSW wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = GlobalAdvWndProc;
        wc.hInstance = NULL;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = GADV_CLASS;
        RegisterClassW(&wc);
        g_advClassRegistered = 1;
    }

    /* 计算居中位置 */
    if (GetWindowRect(hwndOwner, &rcOwner)) {
        posX = (rcOwner.left + rcOwner.right - dlgW) / 2;
        posY = (rcOwner.top + rcOwner.bottom - dlgH) / 2;
    } else {
        screenW = GetSystemMetrics(SM_CXSCREEN);
        screenH = GetSystemMetrics(SM_CYSCREEN);
        posX = (screenW - dlgW) / 2;
        posY = (screenH - dlgH) / 2;
    }

    /* 创建弹出窗口 */
    hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        GADV_CLASS, TR("STR_ADV_SETTINGS"),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        posX, posY, dlgW, dlgH, hwndOwner, NULL, NULL, NULL);

    /* 创建复选框 */
    hAutoStart = CreateWindowExW(0, L"BUTTON", TR("STR_AUTO_START"),
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        20, 20, 280, 25, hDlg, (HMENU)GAD_ID_AUTO_START, NULL, NULL);
    SendMessageW(hAutoStart, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    if (g_appCfg.global.auto_start) SendMessageA(hAutoStart, BM_SETCHECK, BST_CHECKED, 0);

    hDebugLog = CreateWindowExW(0, L"BUTTON", TR("STR_DEBUG_LOG"),
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        20, 55, 280, 25, hDlg, (HMENU)GAD_ID_DEBUG_LOG, NULL, NULL);
    SendMessageW(hDebugLog, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    if (g_appCfg.global.debug_log) SendMessageA(hDebugLog, BM_SETCHECK, BST_CHECKED, 0);

    hAutoHide = CreateWindowExW(0, L"BUTTON", TR("STR_AUTO_HIDE"),
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_MULTILINE,
        20, 90, 280, 40, hDlg, (HMENU)GAD_ID_AUTO_HIDE, NULL, NULL);
    SendMessageW(hAutoHide, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    if (g_appCfg.global.auto_hide) SendMessageA(hAutoHide, BM_SETCHECK, BST_CHECKED, 0);

    /* 创建 OK/Cancel 按钮 */
    hOkBtn = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_OK"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        80, 145, 80, 30, hDlg, (HMENU)GAD_ID_OK, NULL, NULL);
    SendMessageW(hOkBtn, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    hCancelBtn = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_CANCEL"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        180, 145, 80, 30, hDlg, (HMENU)GAD_ID_CANCEL, NULL, NULL);
    SendMessageW(hCancelBtn, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    /* 禁用父窗口（模态行为） */
    EnableWindow(hwndOwner, FALSE);
    ShowWindow(hDlg, SW_SHOW);

    /* 模态消息循环 */
    while (IsWindow(hDlg) && GetMessage(&msg, NULL, 0, 0)) {
        if (!IsWindowEnabled(hwndOwner) && msg.message == WM_KEYDOWN) {
            /* 允许 Tab 键在对话框内导航 */
        }
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    /* 重新启用父窗口 */
    EnableWindow(hwndOwner, TRUE);
    SetForegroundWindow(hwndOwner);
}

/* ======================================================================
   窗口过程
   ====================================================================== */

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_WAKEUP && WM_WAKEUP != 0) {
        AddTrayIcon(hwnd);
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        return 0;
    }

    switch (uMsg) {
    case WM_CREATE: {
        g_hFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei");
        g_hBoldFont = CreateFontW(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei");
        g_hDescFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei");

        LoadAppConfig(&g_appCfg);
        SetDebugLogEnabled(g_appCfg.global.debug_log);
        RegisterHotKey(hwnd, ID_HOTKEY, MOD_CONTROL | MOD_SHIFT, 'M');
        RcloneInit();

        /* 创建列表页 */
        {
            ListPageCallbacks lpCb;
            lpCb.OnAddConnection = OnAddConnection;
            lpCb.OnEditConnection = OnEditConnection;
            lpCb.OnAdvSettings = OnAdvSettings;
            lpCb.OnGlobalAdvSettings = OnGlobalAdvSettings;
            lpCb.OnExit = OnExit;
            lpCb.OnHide = OnHide;
            lpCb.ctx = hwnd;
            ListPage_Create(&g_listPage, hwnd, &g_appCfg, g_rclonePath,
                            g_hFont, g_hBoldFont, lpCb);
        }

        /* 创建配置页 */
        {
            ConfigPageCallbacks cpCb;
            cpCb.OnSave = OnConfigSave;
            cpCb.OnCancel = OnConfigCancel;
            cpCb.ctx = hwnd;
            ConfigPage_Create(&g_configPage, hwnd, &g_appCfg, g_rclonePath,
                              g_hFont, g_hBoldFont, g_hDescFont, cpCb);
        }

        /* 显示列表页 */
        g_pageState = PAGE_LIST;
        ListPage_Show(&g_listPage);

        AddTrayIcon(hwnd);

        /* 自动挂载：如果全局 auto_start 开启，则挂载所有连接 */
        if (g_appCfg.global.auto_start) {
            int i;
            int anyMounted = 0;
            for (i = 0; i < g_appCfg.count; i++) {
                ConnectionConfig* conn = &g_appCfg.connections[i];
                if (!IsMounted(conn->id)) {
                    ProtocolHandler* h = CreateHandlerForProtocol(
                        conn->protocol, conn, &g_appCfg.global);
                    if (h) {
                        h->LoadConfig(h);
                        if (h->ExecuteMountFromConfig(h, g_rclonePath)) {
                            anyMounted = 1;
                        }
                        h->Destroy(h);
                        free(h);
                    }
                }
            }
            ListPage_Refresh(&g_listPage);
            if (anyMounted && g_appCfg.global.auto_hide) {
                HideWindowAndTray(hwnd);
            }
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
            {
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, IDM_SHOW, TR("STR_TRAY_SHOW"));
                AppendMenuW(hMenu, MF_STRING, IDM_HIDETRAY, TR("STR_TRAY_HIDE"));
                AppendMenuW(hMenu, MF_STRING, IDM_EXIT, TR("STR_TRAY_EXIT"));
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                               pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
        }
        break;
    case WM_COMMAND: {
        int handled = 0;
        if (g_pageState == PAGE_LIST) {
            handled = ListPage_HandleCommand(&g_listPage, wParam, lParam);
        } else if (g_pageState == PAGE_CONFIG) {
            handled = ConfigPage_HandleCommand(&g_configPage, wParam, lParam);
        }
        if (!handled) {
            if (LOWORD(wParam) == IDM_SHOW) {
                AddTrayIcon(hwnd);
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            } else if (LOWORD(wParam) == IDM_EXIT) {
                RemoveTrayIcon();
                DestroyWindow(hwnd);
            } else if (LOWORD(wParam) == IDM_HIDETRAY) {
                HideWindowAndTray(hwnd);
            }
        }
        break;
    }
    case WM_VSCROLL: {
        int delta = 0;
        SCROLLINFO si;
        memset(&si, 0, sizeof(si));
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        switch (LOWORD(wParam)) {
            case SB_LINEUP:   delta = -30; break;
            case SB_LINEDOWN: delta = 30; break;
            case SB_PAGEUP:   delta = -(int)si.nPage; break;
            case SB_PAGEDOWN: delta = (int)si.nPage; break;
            case SB_THUMBTRACK: delta = (int)(si.nTrackPos - si.nPos); break;
        }
        if (delta != 0) {
            if (g_pageState == PAGE_LIST) {
                ListPage_HandleScroll(&g_listPage, delta);
            } else if (g_pageState == PAGE_CONFIG) {
                ConfigPage_HandleScroll(&g_configPage, delta);
            }
        }
        break;
    }
    case WM_MOUSEWHEEL: {
        int delta = (short)HIWORD(wParam);
        if (g_pageState == PAGE_LIST) {
            ListPage_HandleMouseWheel(&g_listPage, delta);
        } else if (g_pageState == PAGE_CONFIG) {
            ConfigPage_HandleMouseWheel(&g_configPage, delta);
        }
        break;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        if (g_pageState == PAGE_LIST) {
            ListPage_HandleEraseBkgnd(&g_listPage, hdc);
        } else if (g_pageState == PAGE_CONFIG) {
            ConfigPage_HandleEraseBkgnd(&g_configPage, hdc);
        } else {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
        }
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        LRESULT result = -1;
        if (g_pageState == PAGE_LIST) {
            result = ListPage_HandleCtlColor(&g_listPage, (HWND)lParam, (HDC)wParam);
        } else if (g_pageState == PAGE_CONFIG) {
            result = ConfigPage_HandleCtlColor(&g_configPage, (HWND)lParam, (HDC)wParam);
        }
        if (result != -1) return result;
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    case WM_CLOSE:
        if (g_pageState == PAGE_CONFIG) {
            g_pageState = PAGE_LIST;
            ConfigPage_Hide(&g_configPage);
            ListPage_Show(&g_listPage);
            return 0;
        }
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(hwnd, ID_HOTKEY);
        ListPage_Destroy(&g_listPage);
        ConfigPage_Destroy(&g_configPage);
        if (g_hFont)    { DeleteObject(g_hFont);    g_hFont = NULL; }
        if (g_hBoldFont){ DeleteObject(g_hBoldFont); g_hBoldFont = NULL; }
        if (g_hDescFont){ DeleteObject(g_hDescFont); g_hDescFont = NULL; }
        RemoveTrayIcon();
        StopAllMounts();
        RcloneCleanup();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

/* ======================================================================
   WinMain
   ====================================================================== */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    wchar_t exePath[MAX_PATH];
    wchar_t uniqueId[MAX_PATH];
    int i;
    wchar_t uniqueClassName[MAX_PATH + 50];
    HANDLE hMutex;
    int startInTray = 0;
    WNDCLASSW wc;
    int windowWidth = 580;
    int windowHeight = 515;
    int screenWidth, screenHeight, posX, posY;
    HWND hwnd;
    MSG msg;

    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wcscpy_s(uniqueId, MAX_PATH, exePath);
    for (i = 0; uniqueId[i] != L'\0'; i++) {
        uniqueId[i] = towlower(uniqueId[i]);
        if (uniqueId[i] == L'\\' || uniqueId[i] == L':') uniqueId[i] = L'_';
    }
    swprintf_s(uniqueClassName, MAX_PATH + 50, L"WebDavClientClass_%s", uniqueId);

    WM_WAKEUP = RegisterWindowMessageW(L"WebDavClientWakeupMessage");

    hMutex = CreateMutexW(NULL, FALSE, uniqueClassName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExistingWnd = FindWindowW(uniqueClassName, NULL);
        if (hExistingWnd) SendMessageW(hExistingWnd, WM_WAKEUP, 0, 0);
        CloseHandle(hMutex);
        return 0;
    }

    InitLogger();
    LogMessage("INFO", "Application boot sequence started.");

    if (!InitializeEnvironment(g_rclonePath, sizeof(g_rclonePath))) {
        CloseLogger();
        return 1;
    }

    if (lpCmdLine && (strstr(lpCmdLine, "tray") != NULL || strstr(lpCmdLine, "TRAY") != NULL)) {
        startInTray = 1;
    }

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = uniqueClassName;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    screenWidth = GetSystemMetrics(SM_CXSCREEN);
    screenHeight = GetSystemMetrics(SM_CYSCREEN);
    posX = (screenWidth - windowWidth) / 2;
    posY = (screenHeight - windowHeight) / 2;

    hwnd = CreateWindowExW(
        0, uniqueClassName, TR("STR_TITLE"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        posX, posY, windowWidth, windowHeight,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) {
        CloseLogger();
        return 0;
    }

    ShowWindow(hwnd, startInTray ? SW_HIDE : nCmdShow);
    UpdateWindow(hwnd);

    memset(&msg, 0, sizeof(msg));
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_MOUSEWHEEL) {
            HWND hRoot = GetAncestor(msg.hwnd, GA_ROOT);
            if (hRoot) SendMessage(hRoot, WM_MOUSEWHEEL, msg.wParam, msg.lParam);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    FreeI18n();
    CloseLogger();
    return 0;
}