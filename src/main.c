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

#define WM_TRAYICON   (WM_USER + 101)
#define IDM_SHOW      1001
#define IDM_EXIT      1002
#define IDM_HIDETRAY  1003
#define ID_HOTKEY     1
#define IDC_PROTOCOL_COMBO 11

/* ---- 通用控件（由 main.c 创建和管理） ---- */
static HWND hDriveBox;
static HWND hAutoStartCheck;
static HWND hDebugCheck;
static HWND hAutoHideCheck;
static HWND hActionBtn;
static HWND hHideBtn;
static HWND hExitBtn;
static HWND hAdvBtn;
static HWND hProtocolLabel;
static HWND hProtocolCombo;
static HWND g_hMainTipLabel = NULL;

/* ---- 全局状态 ---- */
static CommonConfig g_commonCfg;
static ProtocolHandler* g_handler = NULL;
static char g_rclonePath[MAX_PATH] = { 0 };
static NOTIFYICONDATAW g_nid = { 0 };
static HFONT g_hFont = NULL;
static HFONT g_hBoldFont = NULL;
static int g_isMounted = 0;
static int g_trayVisible = 0;
static int g_advPageActive = 0;
static int g_scrollPos = 0;
static UINT WM_WAKEUP = 0;

/* ---- 协议处理器工厂 ---- */
static ProtocolHandler* CreateProtocolHandler(const char* name, CommonConfig* cfg) {
    if (strcmp(name, "webdav") == 0) return CreateWebDavHandler(cfg);
    if (strcmp(name, "smb") == 0) return CreateSmbHandler(cfg);
    if (strcmp(name, "sftp") == 0) return CreateSftpHandler(cfg);
    if (strcmp(name, "ftp") == 0) return CreateFtpHandler(cfg);
    return NULL;
}

static int GetProtocolIndex(const char* name) {
    if (strcmp(name, "webdav") == 0) return 0;
    if (strcmp(name, "smb") == 0) return 1;
    if (strcmp(name, "sftp") == 0) return 2;
    if (strcmp(name, "ftp") == 0) return 3;
    return 0;
}

static const char* GetProtocolName(int index) {
    switch (index) {
        case 0: return "webdav";
        case 1: return "smb";
        case 2: return "sftp";
        case 3: return "ftp";
        default: return "webdav";
    }
}

static const wchar_t* GetProtocolDisplayName(int index) {
    switch (index) {
        case 0: return L"WebDAV";
        case 1: return L"SMB";
        case 2: return L"SFTP";
        case 3: return L"FTP";
        default: return L"WebDAV";
    }
}

#define PROTOCOL_COUNT 4  /* 当前支持的协议数量: WebDAV, SMB, SFTP, FTP */

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

static void RemoveTrayIcon() {
    if (g_trayVisible) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_trayVisible = 0;
    }
}

static void HideWindowAndTray(HWND hwnd) {
    RemoveTrayIcon();
    ShowWindow(hwnd, SW_HIDE);
}

/* ---- 创建带字体的控件 ---- */
static HWND CreateStyledWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int x, int y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
    HWND hwnd = CreateWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    if (hwnd && g_hFont) SendMessageW(hwnd, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return hwnd;
}

static HWND CreateStyledWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int x, int y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
    HWND hwnd = CreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    if (hwnd && g_hFont) SendMessageW(hwnd, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return hwnd;
}

static HWND CreateBoldLabelW(LPCWSTR lpWindowName, int x, int y, int nWidth, int nHeight, HWND hWndParent) {
    HWND hwnd = CreateWindowExW(0, L"STATIC", lpWindowName, WS_CHILD | WS_VISIBLE, x, y, nWidth, nHeight, hWndParent, NULL, NULL, NULL);
    if (hwnd && g_hBoldFont) SendMessageW(hwnd, WM_SETFONT, (WPARAM)g_hBoldFont, TRUE);
    return hwnd;
}

/* ---- 通用主页面控件显示/隐藏 ---- */
static void HideCommonMainControls(void) {
    ShowWindow(hProtocolLabel, SW_HIDE);
    ShowWindow(hProtocolCombo, SW_HIDE);
    ShowWindow(hDriveBox, SW_HIDE);
    ShowWindow(hAutoStartCheck, SW_HIDE);
    ShowWindow(hDebugCheck, SW_HIDE);
    ShowWindow(hAutoHideCheck, SW_HIDE);
    ShowWindow(hActionBtn, SW_HIDE);
    ShowWindow(hAdvBtn, SW_HIDE);
    ShowWindow(hHideBtn, SW_HIDE);
    ShowWindow(hExitBtn, SW_HIDE);
    ShowWindow(g_hMainTipLabel, SW_HIDE);
}

static void ShowCommonMainControls(void) {
    ShowWindow(hProtocolLabel, SW_SHOW);
    ShowWindow(hProtocolCombo, SW_SHOW);
    ShowWindow(hDriveBox, SW_SHOW);
    ShowWindow(hAutoStartCheck, SW_SHOW);
    ShowWindow(hDebugCheck, SW_SHOW);
    ShowWindow(hAutoHideCheck, SW_SHOW);
    ShowWindow(hActionBtn, SW_SHOW);
    ShowWindow(hAdvBtn, SW_SHOW);
    ShowWindow(hHideBtn, SW_SHOW);
    ShowWindow(hExitBtn, SW_SHOW);
    ShowWindow(g_hMainTipLabel, SW_SHOW);
}

/* ---- 页面切换 ---- */
static void ShowAdvPage(HWND hwnd) {
    RECT rc;
    int contentH;
    SCROLLINFO si;

    g_advPageActive = 1;
    g_scrollPos = 0;

    HideCommonMainControls();
    if (g_handler) g_handler->HideMainControls(g_handler, hwnd);

    if (g_handler) {
        g_handler->UpdateAdvPositions(g_handler, 0);
        g_handler->ShowAdvControls(g_handler, hwnd);
    }

    ShowScrollBar(hwnd, SB_VERT, TRUE);
    GetClientRect(hwnd, &rc);
    contentH = g_handler ? g_handler->GetAdvContentHeight(g_handler) : 967;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = contentH;
    si.nPage = (UINT)(rc.bottom > 0 ? rc.bottom : 1);
    si.nPos = 0;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

    SetWindowTextW(hwnd, TR("STR_ADV_SETTINGS"));
    InvalidateRect(hwnd, NULL, TRUE);
}

static void HideAdvPage(HWND hwnd) {
    SCROLLINFO si;

    g_advPageActive = 0;
    g_scrollPos = 0;

    if (g_handler) g_handler->HideAdvControls(g_handler, hwnd);

    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = 0;
    si.nPage = 0;
    si.nPos = 0;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    ShowScrollBar(hwnd, SB_VERT, FALSE);

    if (g_handler) g_handler->ShowMainControls(g_handler, hwnd);
    ShowCommonMainControls();

    SetWindowTextW(hwnd, TR("STR_TITLE"));
    InvalidateRect(hwnd, NULL, TRUE);
}

/* ---- 窗口过程 ---- */
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_WAKEUP && WM_WAKEUP != 0) {
        AddTrayIcon(hwnd);
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        return 0;
    }

    switch (uMsg) {
    case WM_CREATE: {
        HFONT hDescFont;
        g_hFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei");
        g_hBoldFont = CreateFontW(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei");

        LoadCommonConfig(&g_commonCfg);
        SetDebugLogEnabled(g_commonCfg.debug_log);
        RegisterHotKey(hwnd, ID_HOTKEY, MOD_CONTROL | MOD_SHIFT, 'M');

        /* 创建协议处理器并加载配置 */
        g_handler = CreateProtocolHandler(g_commonCfg.protocol, &g_commonCfg);
        if (g_handler) {
            g_handler->LoadConfig(g_handler);
            g_handler->CreateMainControls(g_handler, hwnd, g_hFont, g_hBoldFont);
        }

        /* 创建通用控件（Y 坐标下移 45 以容纳协议选择行） */
        hProtocolLabel = CreateBoldLabelW(TR("STR_PROTOCOL"), 30, 28, 110, 28, hwnd);
        hProtocolCombo = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 145, 25, 390, 200, hwnd, (HMENU)IDC_PROTOCOL_COMBO, NULL, NULL);
        SendMessageW(hProtocolCombo, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        {
            int pi;
            for (pi = 0; pi < PROTOCOL_COUNT; pi++) {
                SendMessageW(hProtocolCombo, CB_ADDSTRING, 0, (LPARAM)GetProtocolDisplayName(pi));
            }
            SendMessageW(hProtocolCombo, CB_SETCURSEL, GetProtocolIndex(g_commonCfg.protocol), 0);
        }

        hDriveBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_commonCfg.drive, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_UPPERCASE, 145, 295, 50, 28, hwnd, NULL, NULL, NULL);
        hAutoStartCheck = CreateStyledWindowExW(0, L"BUTTON", TR("STR_AUTO_START"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 205, 297, 160, 25, hwnd, (HMENU)3, NULL, NULL);
        if (g_commonCfg.auto_start) SendMessageA(hAutoStartCheck, BM_SETCHECK, BST_CHECKED, 0);
        hDebugCheck = CreateStyledWindowExW(0, L"BUTTON", TR("STR_DEBUG_LOG"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 370, 297, 165, 25, hwnd, (HMENU)5, NULL, NULL);
        if (g_commonCfg.debug_log) SendMessageA(hDebugCheck, BM_SETCHECK, BST_CHECKED, 0);
        hAutoHideCheck = CreateStyledWindowExW(0, L"BUTTON", TR("STR_AUTO_HIDE"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 30, 340, 505, 28, hwnd, (HMENU)8, NULL, NULL);
        if (g_commonCfg.auto_hide) SendMessageA(hAutoHideCheck, BM_SETCHECK, BST_CHECKED, 0);

        hActionBtn = CreateStyledWindowExW(0, L"BUTTON", TR("STR_MOUNT_BTN"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 30, 385, 121, 42, hwnd, (HMENU)1, NULL, NULL);
        hAdvBtn    = CreateStyledWindowExW(0, L"BUTTON", TR("STR_ADV_SETTINGS"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 158, 385, 121, 42, hwnd, (HMENU)10, NULL, NULL);
        hHideBtn   = CreateStyledWindowExW(0, L"BUTTON", TR("STR_HIDE_BTN"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 286, 385, 121, 42, hwnd, (HMENU)7, NULL, NULL);
        hExitBtn   = CreateStyledWindowExW(0, L"BUTTON", TR("STR_TRAY_EXIT"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 414, 385, 121, 42, hwnd, (HMENU)4, NULL, NULL);
        g_hMainTipLabel = CreateStyledWindowExW(0, L"STATIC", TR("STR_HIDE_TIP"), WS_CHILD | WS_VISIBLE | SS_CENTER, 30, 440, 505, 25, hwnd, NULL, NULL, NULL);

        /* 创建高级设置控件（初始隐藏） */
        hDescFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei");
        if (g_handler) {
            g_handler->CreateAdvControls(g_handler, hwnd, g_hFont, g_hBoldFont, hDescFont);
        }

        AddTrayIcon(hwnd);

        if (g_commonCfg.auto_start && g_handler) {
            if (g_handler->ExecuteMount(g_handler, hwnd, g_rclonePath, 1)) {
                g_isMounted = 1;
                SetWindowTextW(hActionBtn, TR("STR_UNMOUNT_BTN"));
                if (g_commonCfg.auto_hide) HideWindowAndTray(hwnd);
            } else {
                LogMessage("ERROR", "Auto-start mount failed. Check configuration and rclone_error.log for details.");
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
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_SHOW, TR("STR_TRAY_SHOW"));
            AppendMenuW(hMenu, MF_STRING, IDM_HIDETRAY, TR("STR_TRAY_HIDE"));
            AppendMenuW(hMenu, MF_STRING, IDM_EXIT, TR("STR_TRAY_EXIT"));
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        break;
    case WM_COMMAND: {
        /* 先让协议处理器处理命令（返回2=请求切换回主页面, 1=已处理, 0=未处理） */
        if (g_handler) {
            int cmdResult = g_handler->HandleCommand(g_handler, hwnd, wParam, lParam);
            if (cmdResult == 2) {
                HideAdvPage(hwnd);
                break;
            }
            if (cmdResult == 1) break;
        }

        if (LOWORD(wParam) == 1) {
            /* 挂载/卸载按钮 */
            if (g_isMounted == 0) {
                GetWindowTextA(hDriveBox, g_commonCfg.drive, sizeof(g_commonCfg.drive));
                g_commonCfg.auto_start = (SendMessageA(hAutoStartCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
                g_commonCfg.debug_log  = (SendMessageA(hDebugCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
                g_commonCfg.auto_hide  = (SendMessageA(hAutoHideCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
                SaveCommonConfig(&g_commonCfg);
                if (g_handler && g_handler->ExecuteMount(g_handler, hwnd, g_rclonePath, 0)) {
                    g_isMounted = 1;
                    SetWindowTextW(hActionBtn, TR("STR_UNMOUNT_BTN"));
                    MessageBoxW(hwnd, TR("MSG_MOUNT_OK"), TR("MSG_INFO"), MB_OK | MB_ICONINFORMATION);
                    if (g_commonCfg.auto_hide) HideWindowAndTray(hwnd);
                } else {
                    LogMessage("ERROR", "Manual mount failed. Check configuration and rclone_error.log for details.");
                }
            } else {
                LogMessage("INFO", "Unmount action triggered.");
                StopRcloneMount();
                g_isMounted = 0;
                SetWindowTextW(hActionBtn, TR("STR_MOUNT_BTN"));
                MessageBoxW(hwnd, TR("MSG_UNMOUNT_OK"), TR("MSG_INFO"), MB_OK | MB_ICONINFORMATION);
            }
        } else if (LOWORD(wParam) == 7 || LOWORD(wParam) == IDM_HIDETRAY) {
            HideWindowAndTray(hwnd);
        } else if (LOWORD(wParam) == 3) {
            int checked = (SendMessageA(hAutoStartCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_commonCfg.auto_start = checked;
            SetAppAutoStart(checked);
            SaveCommonConfig(&g_commonCfg);
        } else if (LOWORD(wParam) == 5) {
            int checked = (SendMessageA(hDebugCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_commonCfg.debug_log = checked;
            SetDebugLogEnabled(checked);
            SaveCommonConfig(&g_commonCfg);
            LogMessage("INFO", "Debug log toggled dynamically to: %d", checked);
        } else if (LOWORD(wParam) == 8) {
            int checked = (SendMessageA(hAutoHideCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_commonCfg.auto_hide = checked;
            SaveCommonConfig(&g_commonCfg);
        } else if (LOWORD(wParam) == IDC_PROTOCOL_COMBO && HIWORD(wParam) == CBN_SELCHANGE) {
            /* 协议切换 */
            int newSel = (int)SendMessageW(hProtocolCombo, CB_GETCURSEL, 0, 0);
            if (newSel != CB_ERR && newSel != GetProtocolIndex(g_commonCfg.protocol)) {
                const char* newName = GetProtocolName(newSel);
                /* 如果在高级页面，先切回主页面 */
                if (g_advPageActive) HideAdvPage(hwnd);
                /* 保存当前协议的高级设置，避免切换时丢失 */
                if (g_handler) g_handler->SaveAdvSettingsFromUI(g_handler);
                /* 销毁旧处理器 */
                if (g_handler) {
                    g_handler->DestroyControls(g_handler, hwnd);
                    g_handler->Destroy(g_handler);
                    free(g_handler);
                    g_handler = NULL;
                }
                /* 更新协议并保存 */
                strcpy_s(g_commonCfg.protocol, sizeof(g_commonCfg.protocol), newName);
                SaveCommonConfig(&g_commonCfg);
                /* 创建新处理器 */
                g_handler = CreateProtocolHandler(newName, &g_commonCfg);
                if (g_handler) {
                    g_handler->LoadConfig(g_handler);
                    g_handler->CreateMainControls(g_handler, hwnd, g_hFont, g_hBoldFont);
                    g_handler->ShowMainControls(g_handler, hwnd);
                    /* 创建高级设置控件 */
                    {
                        HFONT hDescFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei");
                        g_handler->CreateAdvControls(g_handler, hwnd, g_hFont, g_hBoldFont, hDescFont);
                    }
                }
                InvalidateRect(hwnd, NULL, TRUE);
            }
        } else if (LOWORD(wParam) == 10) {
            ShowAdvPage(hwnd);
        } else if (LOWORD(wParam) == 4 || LOWORD(wParam) == IDM_EXIT) {
            RemoveTrayIcon();
            DestroyWindow(hwnd);
        } else if (LOWORD(wParam) == IDM_SHOW) {
            AddTrayIcon(hwnd);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        break;
    }
    case WM_VSCROLL: {
        if (g_advPageActive && g_handler) {
            SCROLLINFO si;
            int contentH = g_handler->GetAdvContentHeight(g_handler);
            memset(&si, 0, sizeof(si));
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            switch (LOWORD(wParam)) {
                case SB_LINEUP: si.nPos -= 30; break;
                case SB_LINEDOWN: si.nPos += 30; break;
                case SB_PAGEUP: si.nPos -= (int)si.nPage; break;
                case SB_PAGEDOWN: si.nPos += (int)si.nPage; break;
                case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
            }
            if (si.nPos < 0) si.nPos = 0;
            if (si.nPos > contentH - (int)si.nPage) si.nPos = contentH - (int)si.nPage;
            si.fMask = SIF_POS;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            GetScrollInfo(hwnd, SB_VERT, &si);
            if (si.nPos != g_scrollPos) {
                g_scrollPos = si.nPos;
                SendMessage(hwnd, WM_SETREDRAW, FALSE, 0);
                g_handler->UpdateAdvPositions(g_handler, g_scrollPos);
                SendMessage(hwnd, WM_SETREDRAW, TRUE, 0);
                RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
            }
        }
        break;
    }
    case WM_MOUSEWHEEL: {
        if (g_advPageActive && g_handler) {
            SCROLLINFO si;
            int contentH = g_handler->GetAdvContentHeight(g_handler);
            int delta = (short)HIWORD(wParam);
            memset(&si, 0, sizeof(si));
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            si.nPos -= delta / WHEEL_DELTA * 30;
            if (si.nPos < 0) si.nPos = 0;
            if (si.nPos > contentH - (int)si.nPage) si.nPos = contentH - (int)si.nPage;
            si.fMask = SIF_POS;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            GetScrollInfo(hwnd, SB_VERT, &si);
            if (si.nPos != g_scrollPos) {
                g_scrollPos = si.nPos;
                SendMessage(hwnd, WM_SETREDRAW, FALSE, 0);
                g_handler->UpdateAdvPositions(g_handler, g_scrollPos);
                SendMessage(hwnd, WM_SETREDRAW, TRUE, 0);
                RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
            }
        }
        break;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        if (g_advPageActive && g_handler) {
            LRESULT result = g_handler->HandleCtlColor(g_handler, (HWND)lParam, (HDC)wParam);
            if (result != -1) return result;
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    case WM_CLOSE:
        if (g_advPageActive) {
            HideAdvPage(hwnd);
            return 0;
        }
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(hwnd, ID_HOTKEY);
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hBoldFont) DeleteObject(g_hBoldFont);
        if (g_handler) {
            g_handler->DestroyControls(g_handler, hwnd);
            g_handler->Destroy(g_handler);
            free(g_handler);
            g_handler = NULL;
        }
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
        if (msg.message == WM_MOUSEWHEEL && g_advPageActive) {
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