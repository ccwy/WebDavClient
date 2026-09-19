#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <wctype.h> 
#include <commctrl.h>
#include <shlobj.h>
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

static HWND hHostBox;
static HWND hPortBox;
static HWND hPathBox;
static HWND hSslCheck;
static HWND hUserBox;
static HWND hPassBox;
static HWND hDriveBox;
static HWND hAutoStartCheck;
static HWND hDebugCheck;
static HWND hAutoHideCheck;
static HWND hActionBtn;
static HWND hHideBtn;
static HWND hExitBtn;
static HWND hAdvBtn;
// 主页面标签（用于页面切换时显示/隐藏）
static HWND g_hMainLabels[6] = { NULL };
static HWND g_hMainTipLabel = NULL;
// 高级设置页面状态与滚动
static int g_advPageActive = 0;
static int g_scrollPos = 0;
// 高级设置页面控件
static HWND g_hAdvLabels[10] = { NULL };
static HWND g_hAdvEdits[9] = { NULL };
static HWND g_hAdvComboVfs = NULL;
static HWND g_hAdvBtnBrowse = NULL;
static HWND g_hAdvBtnBack = NULL;
static HWND g_hAdvBtnSave = NULL;
static HWND g_hAdvBtnReset = NULL;
static HWND g_hAdvDescLabels[10] = { NULL };
static HFONT g_hAdvDescFont = NULL;
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

// 隐藏主页面和托盘图标
static void HideWindowAndTray(HWND hwnd) {
    RemoveTrayIcon();
    ShowWindow(hwnd, SW_HIDE);
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

// 高级设置对话框控件ID
#define IDC_ADV_EDIT_DCT    201
#define IDC_ADV_EDIT_BS     202
#define IDC_ADV_EDIT_TR     203
#define IDC_ADV_EDIT_CD     204
#define IDC_ADV_EDIT_CMA    205
#define IDC_ADV_EDIT_RCS    206
#define IDC_ADV_EDIT_RCSL   207
#define IDC_ADV_BTN_BROWSE  208
#define IDC_ADV_BTN_OK      209
#define IDC_ADV_BTN_CANCEL  210
#define IDC_ADV_BTN_RESET   211
#define IDC_ADV_EDIT_VOLNAME 212
#define IDC_ADV_COMBO_VFS   213
#define IDC_ADV_EDIT_VCMS   214
#define IDC_ADV_BTN_BACK    215
#define IDC_ADV_BTN_SAVE    216

// 移动控件Y坐标偏移量（用于滚动）
static void MoveCtrlDelta(HWND hCtrl, int dy) {
    RECT rc;
    if (!hCtrl || !IsWindow(hCtrl)) return;
    GetWindowRect(hCtrl, &rc);
    MapWindowPoints(NULL, GetParent(hCtrl), (LPPOINT)&rc, 2);
    MoveWindow(hCtrl, rc.left, rc.top + dy, rc.right - rc.left, rc.bottom - rc.top, FALSE);
}

// 将所有高级设置控件重置到设计位置（scrollPos=0）
static void ResetAdvPositions(void) {
    int y;
    /* Row 0: VFS ComboBox */
    y = 15;
    MoveWindow(g_hAdvLabels[0], 20, y + 3, 165, 25, FALSE);
    MoveWindow(g_hAdvComboVfs, 195, y, 330, 200, FALSE);
    MoveWindow(g_hAdvDescLabels[0], 195, y + 28, 330, 40, FALSE);
    /* Row 1: dir-cache-time */
    y = 85;
    MoveWindow(g_hAdvLabels[1], 20, y + 3, 165, 25, FALSE);
    MoveWindow(g_hAdvEdits[0], 195, y, 330, 25, FALSE);
    MoveWindow(g_hAdvDescLabels[1], 195, y + 28, 330, 40, FALSE);
    /* Row 2: buffer-size */
    y = 155;
    MoveWindow(g_hAdvLabels[2], 20, y + 3, 165, 25, FALSE);
    MoveWindow(g_hAdvEdits[1], 195, y, 330, 25, FALSE);
    MoveWindow(g_hAdvDescLabels[2], 195, y + 28, 330, 40, FALSE);
    /* Row 3: transfers */
    y = 225;
    MoveWindow(g_hAdvLabels[3], 20, y + 3, 165, 25, FALSE);
    MoveWindow(g_hAdvEdits[2], 195, y, 330, 25, FALSE);
    MoveWindow(g_hAdvDescLabels[3], 195, y + 28, 330, 40, FALSE);
    /* Row 4: cache-dir (narrower edit + browse button) */
    y = 295;
    MoveWindow(g_hAdvLabels[4], 20, y + 3, 165, 25, FALSE);
    MoveWindow(g_hAdvEdits[3], 195, y, 260, 25, FALSE);
    MoveWindow(g_hAdvBtnBrowse, 465, y, 60, 25, FALSE);
    MoveWindow(g_hAdvDescLabels[4], 195, y + 28, 330, 40, FALSE);
    /* Row 5: vfs-cache-max-age */
    y = 365;
    MoveWindow(g_hAdvLabels[5], 20, y + 3, 165, 25, FALSE);
    MoveWindow(g_hAdvEdits[4], 195, y, 330, 25, FALSE);
    MoveWindow(g_hAdvDescLabels[5], 195, y + 28, 330, 40, FALSE);
    /* Row 6: vfs-read-chunk-size */
    y = 435;
    MoveWindow(g_hAdvLabels[6], 20, y + 3, 165, 25, FALSE);
    MoveWindow(g_hAdvEdits[5], 195, y, 330, 25, FALSE);
    MoveWindow(g_hAdvDescLabels[6], 195, y + 28, 330, 40, FALSE);
    /* Row 7: vfs-read-chunk-size-limit */
    y = 505;
    MoveWindow(g_hAdvLabels[7], 20, y + 3, 165, 25, FALSE);
    MoveWindow(g_hAdvEdits[6], 195, y, 330, 25, FALSE);
    MoveWindow(g_hAdvDescLabels[7], 195, y + 28, 330, 40, FALSE);
    /* Row 8: volname */
    y = 575;
    MoveWindow(g_hAdvLabels[8], 20, y + 3, 165, 25, FALSE);
    MoveWindow(g_hAdvEdits[7], 195, y, 330, 25, FALSE);
    MoveWindow(g_hAdvDescLabels[8], 195, y + 28, 330, 40, FALSE);
    /* Row 9: vfs-cache-max-size */
    y = 645;
    MoveWindow(g_hAdvLabels[9], 20, y + 3, 165, 25, FALSE);
    MoveWindow(g_hAdvEdits[8], 195, y, 330, 25, FALSE);
    MoveWindow(g_hAdvDescLabels[9], 195, y + 28, 330, 40, FALSE);
    /* Bottom buttons */
    y = 730;
    MoveWindow(g_hAdvBtnBack, 30, y, 155, 32, FALSE);
    MoveWindow(g_hAdvBtnSave, 205, y, 155, 32, FALSE);
    MoveWindow(g_hAdvBtnReset, 380, y, 155, 32, FALSE);
}

// 隐藏主页面控件
static void HideMainControls(void) {
    int i;
    for (i = 0; i < 6; i++) ShowWindow(g_hMainLabels[i], SW_HIDE);
    ShowWindow(hHostBox, SW_HIDE);
    ShowWindow(hPortBox, SW_HIDE);
    ShowWindow(hPathBox, SW_HIDE);
    ShowWindow(hUserBox, SW_HIDE);
    ShowWindow(hPassBox, SW_HIDE);
    ShowWindow(hDriveBox, SW_HIDE);
    ShowWindow(hSslCheck, SW_HIDE);
    ShowWindow(hAutoStartCheck, SW_HIDE);
    ShowWindow(hDebugCheck, SW_HIDE);
    ShowWindow(hAutoHideCheck, SW_HIDE);
    ShowWindow(hActionBtn, SW_HIDE);
    ShowWindow(hAdvBtn, SW_HIDE);
    ShowWindow(hHideBtn, SW_HIDE);
    ShowWindow(hExitBtn, SW_HIDE);
    ShowWindow(g_hMainTipLabel, SW_HIDE);
}

// 显示主页面控件
static void ShowMainControls(void) {
    int i;
    for (i = 0; i < 6; i++) ShowWindow(g_hMainLabels[i], SW_SHOW);
    ShowWindow(hHostBox, SW_SHOW);
    ShowWindow(hPortBox, SW_SHOW);
    ShowWindow(hPathBox, SW_SHOW);
    ShowWindow(hUserBox, SW_SHOW);
    ShowWindow(hPassBox, SW_SHOW);
    ShowWindow(hDriveBox, SW_SHOW);
    ShowWindow(hSslCheck, SW_SHOW);
    ShowWindow(hAutoStartCheck, SW_SHOW);
    ShowWindow(hDebugCheck, SW_SHOW);
    ShowWindow(hAutoHideCheck, SW_SHOW);
    ShowWindow(hActionBtn, SW_SHOW);
    ShowWindow(hAdvBtn, SW_SHOW);
    ShowWindow(hHideBtn, SW_SHOW);
    ShowWindow(hExitBtn, SW_SHOW);
    ShowWindow(g_hMainTipLabel, SW_SHOW);
}

// 切换到高级设置页面
static void ShowAdvPage(HWND hwnd) {
    int i;
    RECT rc;
    int contentH;
    SCROLLINFO si;

    g_advPageActive = 1;
    g_scrollPos = 0;

    HideMainControls();
    ResetAdvPositions();

    /* 显示高级设置控件 */
    for (i = 0; i < 10; i++) {
        ShowWindow(g_hAdvLabels[i], SW_SHOW);
        ShowWindow(g_hAdvDescLabels[i], SW_SHOW);
    }
    for (i = 0; i < 9; i++) ShowWindow(g_hAdvEdits[i], SW_SHOW);
    ShowWindow(g_hAdvComboVfs, SW_SHOW);
    ShowWindow(g_hAdvBtnBrowse, SW_SHOW);
    ShowWindow(g_hAdvBtnBack, SW_SHOW);
    ShowWindow(g_hAdvBtnSave, SW_SHOW);
    ShowWindow(g_hAdvBtnReset, SW_SHOW);

    /* 设置滚动条 */
    GetClientRect(hwnd, &rc);
    contentH = 777; /* 15 + 70*10 + 15 + 32 + 15 */
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

// 切换回主页面
static void HideAdvPage(HWND hwnd) {
    int i;
    SCROLLINFO si;

    g_advPageActive = 0;
    g_scrollPos = 0;

    /* 隐藏高级设置控件 */
    for (i = 0; i < 10; i++) {
        ShowWindow(g_hAdvLabels[i], SW_HIDE);
        ShowWindow(g_hAdvDescLabels[i], SW_HIDE);
    }
    for (i = 0; i < 9; i++) ShowWindow(g_hAdvEdits[i], SW_HIDE);
    ShowWindow(g_hAdvComboVfs, SW_HIDE);
    ShowWindow(g_hAdvBtnBrowse, SW_HIDE);
    ShowWindow(g_hAdvBtnBack, SW_HIDE);
    ShowWindow(g_hAdvBtnSave, SW_HIDE);
    ShowWindow(g_hAdvBtnReset, SW_HIDE);

    /* 禁用滚动条 */
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = 0;
    si.nPage = 0;
    si.nPos = 0;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    ShowScrollBar(hwnd, SB_VERT, FALSE);

    ShowMainControls();
    SetWindowTextW(hwnd, TR("STR_TITLE"));
    InvalidateRect(hwnd, NULL, TRUE);
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

    if (StartRcloneMount(g_rclonePath, finalUrl, &g_config)) {
        g_isMounted = 1;
        SetWindowTextW(hActionBtn, TR("STR_UNMOUNT_BTN")); 
        if (!isAuto) MessageBoxW(hwnd, TR("MSG_MOUNT_OK"), TR("MSG_INFO"), MB_OK | MB_ICONINFORMATION);
        // 挂载成功后，若启用了自动隐藏，则隐藏主页面和托盘
        if (g_config.auto_hide) {
            HideWindowAndTray(hwnd);
        }
    } else {
        g_isMounted = 0;
        SetWindowTextW(hActionBtn, TR("STR_MOUNT_BTN"));
        if (!isAuto) MessageBoxW(hwnd, TR("MSG_MOUNT_FAIL"), TR("MSG_ERROR"), MB_OK | MB_ICONERROR);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // 处理二次运行实例发送来的唤醒消息?
    if (uMsg == WM_WAKEUP && WM_WAKEUP != 0) {
        AddTrayIcon(hwnd); 
        ShowWindow(hwnd, SW_RESTORE); // 使用 RESTORE 可以从最小化状态恢复?
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

        g_hMainLabels[0] = CreateBoldLabelW(TR("STR_HOST"), 30, 25, 110, 28, hwnd);
        hHostBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.host, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 25, 390, 28, hwnd, NULL, NULL, NULL);

        g_hMainLabels[1] = CreateBoldLabelW(TR("STR_PORT"), 30, 70, 110, 28, hwnd);
        hPortBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.port, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, 145, 70, 130, 28, hwnd, NULL, NULL, NULL);
        hSslCheck = CreateStyledWindowExW(0, L"BUTTON", TR("STR_SSL"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 295, 72, 240, 25, hwnd, NULL, NULL, NULL);
        if (g_config.ssl) SendMessageA(hSslCheck, BM_SETCHECK, BST_CHECKED, 0);

        g_hMainLabels[2] = CreateBoldLabelW(TR("STR_PATH"), 30, 115, 110, 28, hwnd);
        hPathBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.path, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 115, 390, 28, hwnd, NULL, NULL, NULL);

        g_hMainLabels[3] = CreateBoldLabelW(TR("STR_USER"), 30, 160, 110, 28, hwnd);
        hUserBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.user, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 160, 390, 28, hwnd, NULL, NULL, NULL);

        g_hMainLabels[4] = CreateBoldLabelW(TR("STR_PASS"), 30, 205, 110, 28, hwnd);
        hPassBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.pass, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD, 145, 205, 390, 28, hwnd, NULL, NULL, NULL);

        g_hMainLabels[5] = CreateBoldLabelW(TR("STR_DRIVE"), 30, 250, 110, 28, hwnd);
        hDriveBox = CreateStyledWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.drive, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_UPPERCASE, 145, 250, 50, 28, hwnd, NULL, NULL, NULL);
        
        hAutoStartCheck = CreateStyledWindowExW(0, L"BUTTON", TR("STR_AUTO_START"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 205, 252, 160, 25, hwnd, (HMENU)3, NULL, NULL);
        if (g_config.auto_start) SendMessageA(hAutoStartCheck, BM_SETCHECK, BST_CHECKED, 0);

        hDebugCheck = CreateStyledWindowExW(0, L"BUTTON", TR("STR_DEBUG_LOG"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 370, 252, 165, 25, hwnd, (HMENU)5, NULL, NULL);
        if (g_config.debug_log) SendMessageA(hDebugCheck, BM_SETCHECK, BST_CHECKED, 0);

        hAutoHideCheck = CreateStyledWindowExW(0, L"BUTTON", TR("STR_AUTO_HIDE"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 30, 295, 505, 28, hwnd, (HMENU)8, NULL, NULL);
        if (g_config.auto_hide) SendMessageA(hAutoHideCheck, BM_SETCHECK, BST_CHECKED, 0);

        hActionBtn = CreateStyledWindowExW(0, L"BUTTON", TR("STR_MOUNT_BTN"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 30, 340, 121, 42, hwnd, (HMENU)1, NULL, NULL);
        hAdvBtn    = CreateStyledWindowExW(0, L"BUTTON", TR("STR_ADV_SETTINGS"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 158, 340, 121, 42, hwnd, (HMENU)10, NULL, NULL);
        hHideBtn   = CreateStyledWindowExW(0, L"BUTTON", TR("STR_HIDE_BTN"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 286, 340, 121, 42, hwnd, (HMENU)7, NULL, NULL);
        hExitBtn   = CreateStyledWindowExW(0, L"BUTTON", TR("STR_TRAY_EXIT"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 414, 340, 121, 42, hwnd, (HMENU)4, NULL, NULL);

        g_hMainTipLabel = CreateStyledWindowExW(0, L"STATIC", TR("STR_HIDE_TIP"), WS_CHILD | WS_VISIBLE | SS_CENTER, 30, 395, 505, 25, hwnd, NULL, NULL, NULL);

        /* --- 创建高级设置页面控件（初始隐藏） --- */
        {
            int y;
            char transfersStr[16];
            const wchar_t* vfsDesc = NULL;
            g_hAdvDescFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei");

            /* Row 0: vfs-cache-mode ComboBox */
            y = 15;
            g_hAdvLabels[0] = CreateWindowExW(0, L"STATIC", TR("STR_VFS_CACHE_MODE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvLabels[0], WM_SETFONT, (WPARAM)g_hBoldFont, TRUE);
            g_hAdvComboVfs = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 195, y, 330, 200, hwnd, (HMENU)IDC_ADV_COMBO_VFS, NULL, NULL);
            SendMessageW(g_hAdvComboVfs, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessageW(g_hAdvComboVfs, CB_ADDSTRING, 0, (LPARAM)TR("STR_VFS_CACHE_OFF"));
            SendMessageW(g_hAdvComboVfs, CB_ADDSTRING, 0, (LPARAM)TR("STR_VFS_CACHE_MINIMAL"));
            SendMessageW(g_hAdvComboVfs, CB_ADDSTRING, 0, (LPARAM)TR("STR_VFS_CACHE_WRITES"));
            SendMessageW(g_hAdvComboVfs, CB_ADDSTRING, 0, (LPARAM)TR("STR_VFS_CACHE_FULL"));
            SendMessageW(g_hAdvComboVfs, CB_SETCURSEL, (WPARAM)g_config.vfs_cache_mode, 0);
            switch (g_config.vfs_cache_mode) {
                case 0: vfsDesc = TR("STR_VFS_TIP_OFF"); break;
                case 1: vfsDesc = TR("STR_VFS_TIP_MINIMAL"); break;
                case 2: vfsDesc = TR("STR_VFS_TIP_WRITES"); break;
                case 3: vfsDesc = TR("STR_VFS_TIP_FULL"); break;
                default: vfsDesc = TR("STR_VFS_TIP_WRITES"); break;
            }
            g_hAdvDescLabels[0] = CreateWindowExW(0, L"STATIC", vfsDesc, WS_CHILD, 195, y + 28, 330, 40, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvDescLabels[0], WM_SETFONT, (WPARAM)g_hAdvDescFont, TRUE);

            /* Row 1: dir-cache-time */
            y = 85;
            g_hAdvLabels[1] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_DIR_CACHE_TIME"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvLabels[1], WM_SETFONT, (WPARAM)g_hBoldFont, TRUE);
            g_hAdvEdits[0] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.dir_cache_time, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 25, hwnd, (HMENU)IDC_ADV_EDIT_DCT, NULL, NULL);
            SendMessageW(g_hAdvEdits[0], WM_SETFONT, (WPARAM)g_hFont, TRUE);
            g_hAdvDescLabels[1] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_DIR_CACHE_TIME"), WS_CHILD, 195, y + 28, 330, 40, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvDescLabels[1], WM_SETFONT, (WPARAM)g_hAdvDescFont, TRUE);

            /* Row 2: buffer-size */
            y = 155;
            g_hAdvLabels[2] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_BUFFER_SIZE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvLabels[2], WM_SETFONT, (WPARAM)g_hBoldFont, TRUE);
            g_hAdvEdits[1] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.buffer_size, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 25, hwnd, (HMENU)IDC_ADV_EDIT_BS, NULL, NULL);
            SendMessageW(g_hAdvEdits[1], WM_SETFONT, (WPARAM)g_hFont, TRUE);
            g_hAdvDescLabels[2] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_BUFFER_SIZE"), WS_CHILD, 195, y + 28, 330, 40, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvDescLabels[2], WM_SETFONT, (WPARAM)g_hAdvDescFont, TRUE);

            /* Row 3: transfers */
            y = 225;
            g_hAdvLabels[3] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_TRANSFERS"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvLabels[3], WM_SETFONT, (WPARAM)g_hBoldFont, TRUE);
            sprintf_s(transfersStr, sizeof(transfersStr), "%d", g_config.transfers);
            g_hAdvEdits[2] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", transfersStr, WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 195, y, 330, 25, hwnd, (HMENU)IDC_ADV_EDIT_TR, NULL, NULL);
            SendMessageW(g_hAdvEdits[2], WM_SETFONT, (WPARAM)g_hFont, TRUE);
            g_hAdvDescLabels[3] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_TRANSFERS"), WS_CHILD, 195, y + 28, 330, 40, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvDescLabels[3], WM_SETFONT, (WPARAM)g_hAdvDescFont, TRUE);

            /* Row 4: cache-dir (narrower edit + browse button) */
            y = 295;
            g_hAdvLabels[4] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_CACHE_DIR"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvLabels[4], WM_SETFONT, (WPARAM)g_hBoldFont, TRUE);
            g_hAdvEdits[3] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.cache_dir, WS_CHILD | ES_AUTOHSCROLL, 195, y, 260, 25, hwnd, (HMENU)IDC_ADV_EDIT_CD, NULL, NULL);
            SendMessageW(g_hAdvEdits[3], WM_SETFONT, (WPARAM)g_hFont, TRUE);
            g_hAdvBtnBrowse = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | BS_PUSHBUTTON, 465, y, 60, 25, hwnd, (HMENU)IDC_ADV_BTN_BROWSE, NULL, NULL);
            SendMessageW(g_hAdvBtnBrowse, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            g_hAdvDescLabels[4] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_CACHE_DIR"), WS_CHILD, 195, y + 28, 330, 40, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvDescLabels[4], WM_SETFONT, (WPARAM)g_hAdvDescFont, TRUE);

            /* Row 5: vfs-cache-max-age */
            y = 365;
            g_hAdvLabels[5] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_CACHE_MAX_AGE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvLabels[5], WM_SETFONT, (WPARAM)g_hBoldFont, TRUE);
            g_hAdvEdits[4] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.vfs_cache_max_age, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 25, hwnd, (HMENU)IDC_ADV_EDIT_CMA, NULL, NULL);
            SendMessageW(g_hAdvEdits[4], WM_SETFONT, (WPARAM)g_hFont, TRUE);
            g_hAdvDescLabels[5] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VFS_CACHE_MAX_AGE"), WS_CHILD, 195, y + 28, 330, 40, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvDescLabels[5], WM_SETFONT, (WPARAM)g_hAdvDescFont, TRUE);

            /* Row 6: vfs-read-chunk-size */
            y = 435;
            g_hAdvLabels[6] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_READ_CHUNK"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvLabels[6], WM_SETFONT, (WPARAM)g_hBoldFont, TRUE);
            g_hAdvEdits[5] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.vfs_read_chunk_size, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 25, hwnd, (HMENU)IDC_ADV_EDIT_RCS, NULL, NULL);
            SendMessageW(g_hAdvEdits[5], WM_SETFONT, (WPARAM)g_hFont, TRUE);
            g_hAdvDescLabels[6] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VFS_READ_CHUNK"), WS_CHILD, 195, y + 28, 330, 40, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvDescLabels[6], WM_SETFONT, (WPARAM)g_hAdvDescFont, TRUE);

            /* Row 7: vfs-read-chunk-size-limit */
            y = 505;
            g_hAdvLabels[7] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_READ_CHUNK_LIMIT"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvLabels[7], WM_SETFONT, (WPARAM)g_hBoldFont, TRUE);
            g_hAdvEdits[6] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.vfs_read_chunk_size_limit, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 25, hwnd, (HMENU)IDC_ADV_EDIT_RCSL, NULL, NULL);
            SendMessageW(g_hAdvEdits[6], WM_SETFONT, (WPARAM)g_hFont, TRUE);
            g_hAdvDescLabels[7] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VFS_READ_CHUNK_LIMIT"), WS_CHILD, 195, y + 28, 330, 40, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvDescLabels[7], WM_SETFONT, (WPARAM)g_hAdvDescFont, TRUE);

            /* Row 8: volname */
            y = 575;
            g_hAdvLabels[8] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VOLNAME"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvLabels[8], WM_SETFONT, (WPARAM)g_hBoldFont, TRUE);
            g_hAdvEdits[7] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.volname, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 25, hwnd, (HMENU)IDC_ADV_EDIT_VOLNAME, NULL, NULL);
            SendMessageW(g_hAdvEdits[7], WM_SETFONT, (WPARAM)g_hFont, TRUE);
            g_hAdvDescLabels[8] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VOLNAME"), WS_CHILD, 195, y + 28, 330, 40, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvDescLabels[8], WM_SETFONT, (WPARAM)g_hAdvDescFont, TRUE);

            /* Row 9: vfs-cache-max-size */
            y = 645;
            g_hAdvLabels[9] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_CACHE_MAX_SIZE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvLabels[9], WM_SETFONT, (WPARAM)g_hBoldFont, TRUE);
            g_hAdvEdits[8] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.vfs_cache_max_size, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 25, hwnd, (HMENU)IDC_ADV_EDIT_VCMS, NULL, NULL);
            SendMessageW(g_hAdvEdits[8], WM_SETFONT, (WPARAM)g_hFont, TRUE);
            g_hAdvDescLabels[9] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VFS_CACHE_MAX_SIZE"), WS_CHILD, 195, y + 28, 330, 40, hwnd, NULL, NULL, NULL);
            SendMessageW(g_hAdvDescLabels[9], WM_SETFONT, (WPARAM)g_hAdvDescFont, TRUE);

            /* Bottom buttons: Back, Save, Reset */
            y = 730;
            g_hAdvBtnBack = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_BACK"), WS_CHILD | BS_PUSHBUTTON, 30, y, 155, 32, hwnd, (HMENU)IDC_ADV_BTN_BACK, NULL, NULL);
            SendMessageW(g_hAdvBtnBack, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            g_hAdvBtnSave = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_OK"), WS_CHILD | BS_PUSHBUTTON, 205, y, 155, 32, hwnd, (HMENU)IDC_ADV_BTN_SAVE, NULL, NULL);
            SendMessageW(g_hAdvBtnSave, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            g_hAdvBtnReset = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_RESET"), WS_CHILD | BS_PUSHBUTTON, 380, y, 155, 32, hwnd, (HMENU)IDC_ADV_BTN_RESET, NULL, NULL);
            SendMessageW(g_hAdvBtnReset, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        }

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
            HideWindowAndTray(hwnd);
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
        } else if (LOWORD(wParam) == 8) {
            int checked = (SendMessageA(hAutoHideCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_config.auto_hide = checked;
            SaveConfig(&g_config);
        } else if (LOWORD(wParam) == 10) {
            ShowAdvPage(hwnd);
        } else if (LOWORD(wParam) == IDC_ADV_BTN_BACK) {
            HideAdvPage(hwnd);
        } else if (LOWORD(wParam) == IDC_ADV_BTN_SAVE) {
            char transfersBuf[16];
            int vfsSel;
            GetWindowTextA(g_hAdvEdits[0], g_config.dir_cache_time, sizeof(g_config.dir_cache_time));
            GetWindowTextA(g_hAdvEdits[1], g_config.buffer_size, sizeof(g_config.buffer_size));
            memset(transfersBuf, 0, sizeof(transfersBuf));
            GetWindowTextA(g_hAdvEdits[2], transfersBuf, sizeof(transfersBuf));
            g_config.transfers = atoi(transfersBuf);
            if (g_config.transfers <= 0) g_config.transfers = 4;
            GetWindowTextA(g_hAdvEdits[3], g_config.cache_dir, sizeof(g_config.cache_dir));
            GetWindowTextA(g_hAdvEdits[4], g_config.vfs_cache_max_age, sizeof(g_config.vfs_cache_max_age));
            GetWindowTextA(g_hAdvEdits[5], g_config.vfs_read_chunk_size, sizeof(g_config.vfs_read_chunk_size));
            GetWindowTextA(g_hAdvEdits[6], g_config.vfs_read_chunk_size_limit, sizeof(g_config.vfs_read_chunk_size_limit));
            GetWindowTextA(g_hAdvEdits[7], g_config.volname, sizeof(g_config.volname));
            GetWindowTextA(g_hAdvEdits[8], g_config.vfs_cache_max_size, sizeof(g_config.vfs_cache_max_size));
            vfsSel = (int)SendMessageW(g_hAdvComboVfs, CB_GETCURSEL, 0, 0);
            g_config.vfs_cache_mode = (vfsSel != CB_ERR) ? vfsSel : 2;
            SaveConfig(&g_config);
            HideAdvPage(hwnd);
        } else if (LOWORD(wParam) == IDC_ADV_BTN_RESET) {
            SetWindowTextA(g_hAdvEdits[0], "72h");
            SetWindowTextA(g_hAdvEdits[1], "16M");
            SetWindowTextA(g_hAdvEdits[2], "4");
            SetWindowTextA(g_hAdvEdits[3], "");
            SetWindowTextA(g_hAdvEdits[4], "24h");
            SetWindowTextA(g_hAdvEdits[5], "128M");
            SetWindowTextA(g_hAdvEdits[6], "off");
            SetWindowTextA(g_hAdvEdits[7], "WebDAV_Disk");
            SetWindowTextA(g_hAdvEdits[8], "5G");
            SendMessageW(g_hAdvComboVfs, CB_SETCURSEL, 2, 0);
            SetWindowTextW(g_hAdvDescLabels[0], TR("STR_VFS_TIP_WRITES"));
        } else if (LOWORD(wParam) == IDC_ADV_COMBO_VFS && HIWORD(wParam) == CBN_SELCHANGE) {
            int vfsSel = (int)SendMessageW(g_hAdvComboVfs, CB_GETCURSEL, 0, 0);
            if (vfsSel != CB_ERR) {
                const wchar_t* vfsDesc = NULL;
                switch (vfsSel) {
                    case 0: vfsDesc = TR("STR_VFS_TIP_OFF"); break;
                    case 1: vfsDesc = TR("STR_VFS_TIP_MINIMAL"); break;
                    case 2: vfsDesc = TR("STR_VFS_TIP_WRITES"); break;
                    case 3: vfsDesc = TR("STR_VFS_TIP_FULL"); break;
                    default: vfsDesc = TR("STR_VFS_TIP_WRITES"); break;
                }
                SetWindowTextW(g_hAdvDescLabels[0], vfsDesc);
            }
        } else if (LOWORD(wParam) == IDC_ADV_BTN_BROWSE) {
            BROWSEINFOW bi;
            LPITEMIDLIST pidl;
            wchar_t selectedPath[MAX_PATH];
            char ansiPath[MAX_PATH];
            memset(&bi, 0, sizeof(bi));
            bi.hwndOwner = hwnd;
            bi.lpszTitle = TR("STR_ADV_CACHE_DIR");
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                if (SHGetPathFromIDListW(pidl, selectedPath)) {
                    WideCharToMultiByte(CP_ACP, 0, selectedPath, -1, ansiPath, MAX_PATH, NULL, NULL);
                    SetWindowTextA(g_hAdvEdits[3], ansiPath);
                }
                CoTaskMemFree(pidl);
            }
        } else if (LOWORD(wParam) == 4 || LOWORD(wParam) == IDM_EXIT) {
            RemoveTrayIcon();
            DestroyWindow(hwnd);
        } else if (LOWORD(wParam) == IDM_SHOW) {
            AddTrayIcon(hwnd);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        break;
    case WM_VSCROLL: {
        if (g_advPageActive) {
            SCROLLINFO si;
            int oldPos, delta;
            int contentH = 777;
            memset(&si, 0, sizeof(si));
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            oldPos = si.nPos;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: si.nPos -= 30; break;
                case SB_LINEDOWN: si.nPos += 30; break;
                case SB_PAGEUP: si.nPos -= si.nPage; break;
                case SB_PAGEDOWN: si.nPos += si.nPage; break;
                case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
            }
            if (si.nPos < 0) si.nPos = 0;
            if (si.nPos > contentH - (int)si.nPage) si.nPos = contentH - (int)si.nPage;
            si.fMask = SIF_POS;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            GetScrollInfo(hwnd, SB_VERT, &si);
            delta = oldPos - si.nPos;
            if (delta != 0) {
                g_scrollPos = si.nPos;
                ScrollWindowEx(hwnd, 0, delta, NULL, NULL, NULL, NULL, SW_INVALIDATE | SW_SCROLLCHILDREN);
                UpdateWindow(hwnd);
            }
        }
        break;
    }
    case WM_MOUSEWHEEL: {
        if (g_advPageActive) {
            SCROLLINFO si;
            int oldPos, delta, lines;
            int contentH = 777;
            memset(&si, 0, sizeof(si));
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            oldPos = si.nPos;
            lines = (int)(-(short)HIWORD(wParam) / WHEEL_DELTA) * 30;
            si.nPos -= lines;
            if (si.nPos < 0) si.nPos = 0;
            if (si.nPos > contentH - (int)si.nPage) si.nPos = contentH - (int)si.nPage;
            si.fMask = SIF_POS;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            GetScrollInfo(hwnd, SB_VERT, &si);
            delta = oldPos - si.nPos;
            if (delta != 0) {
                g_scrollPos = si.nPos;
                ScrollWindowEx(hwnd, 0, delta, NULL, NULL, NULL, NULL, SW_INVALIDATE | SW_SCROLLCHILDREN);
                UpdateWindow(hwnd);
            }
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        if (g_advPageActive) {
            int idx;
            HWND hCtrl;
            HDC hdc;
            hCtrl = (HWND)lParam;
            hdc = (HDC)wParam;
            for (idx = 0; idx < 10; idx++) {
                if (hCtrl == g_hAdvDescLabels[idx]) {
                    SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
                    SetBkMode(hdc, TRANSPARENT);
                    return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
                }
            }
        }
        break;
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
        if (g_hAdvDescFont) DeleteObject(g_hAdvDescFont);
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
    int i;
    wcscpy_s(uniqueId, MAX_PATH, exePath);
    for (i = 0; uniqueId[i] != L'\0'; i++) {
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

    // 3. 互斥体单实例检测机制?
    HANDLE hMutex = CreateMutexW(NULL, FALSE, uniqueClassName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // 如果当前路径下已有实例运行，查找它的主窗口?
        HWND hExistingWnd = FindWindowW(uniqueClassName, NULL);
        if (hExistingWnd) {
            // 发送自定义唤醒消息唤醒旧实例?
            SendMessageW(hExistingWnd, WM_WAKEUP, 0, 0);
        }
        CloseHandle(hMutex);
        return 0; // 新实例直接退出?
    }

    InitLogger();
    LogMessage("INFO", "Application boot sequence started.");

    if (!InitializeEnvironment(g_rclonePath, sizeof(g_rclonePath))) {
        CloseLogger();
        return 1;
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

    // 计算屏幕中央的坐标?
    int windowWidth = 580;
    int windowHeight = 470;
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenWidth - windowWidth) / 2;
    int posY = (screenHeight - windowHeight) / 2;

    HWND hwnd = CreateWindowExW(
        0, uniqueClassName, TR("STR_TITLE"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VSCROLL,
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
