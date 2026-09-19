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
static HWND hVfsCacheCombo;
static HWND hVfsTip;
static HWND hVfsDescLabel;
static HWND hActionBtn;
static HWND hHideBtn;
static HWND hExitBtn;
static HWND hAdvBtn;
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

// 获取 VFS 缓存模式对应的悬浮提示文本
static const wchar_t* GetVfsCacheTip(int mode) {
    switch (mode) {
        case 0: return TR("STR_VFS_TIP_OFF");
        case 1: return TR("STR_VFS_TIP_MINIMAL");
        case 2: return TR("STR_VFS_TIP_WRITES");
        case 3: return TR("STR_VFS_TIP_FULL");
        default: return TR("STR_VFS_TIP_WRITES");
    }
}

// 更新 VFS 缓存模式的描述文本和 Tooltip
static void UpdateVfsCacheTip() {
    int sel = (int)SendMessageW(hVfsCacheCombo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) sel = 2;
    const wchar_t* tipText = GetVfsCacheTip(sel);
    SetWindowTextW(hVfsDescLabel, tipText);
    // 更新 Tooltip 文本
    TOOLINFOW ti = { 0 };
    ti.cbSize = sizeof(TOOLINFOW);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = GetParent(hVfsCacheCombo);
    ti.uId = (UINT_PTR)hVfsCacheCombo;
    ti.lpszText = (LPWSTR)tipText;
    SendMessageW(hVfsTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
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

// 高级设置对话框的 Tooltip
static HWND g_hAdvTip = NULL;

// 高级设置对话框窗口过程
static LRESULT CALLBACK AdvSettingsProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        /* 所有变量声明放在块顶部（C89兼容） */
        HFONT hFont;
        HFONT hBoldFont;
        int labelW, editW, editX, startY, rowH, y;
        HWND lbl1;
        HWND edt1;
        HWND lbl2;
        HWND edt2;
        HWND lbl3;
        HWND edt3;
        HWND lbl4;
        HWND edt4;
        HWND btnBrowse;
        HWND lbl5;
        HWND edt5;
        HWND lbl6;
        HWND edt6;
        HWND lbl7;
        HWND edt7;
        HWND btnOk;
        HWND btnReset;
        HWND btnCancel;
        char transfersStr[16];
        HWND tipEdits[7];
        const wchar_t* tipTexts[7];
        int i;
        TOOLINFOW ti;

        hFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei");
        hBoldFont = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei");
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)hFont);

        labelW = 160; editW = 280; editX = 190; startY = 20; rowH = 38;

        /* 行1: dir-cache-time */
        y = startY;
        lbl1 = CreateWindowExW(0, L"STATIC", TR("STR_ADV_DIR_CACHE_TIME"), WS_CHILD | WS_VISIBLE, 20, y + 3, labelW, 25, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl1, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
        edt1 = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.dir_cache_time, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, editX, y, editW, 25, hwnd, (HMENU)IDC_ADV_EDIT_DCT, NULL, NULL);
        SendMessageW(edt1, WM_SETFONT, (WPARAM)hFont, TRUE);

        /* 行2: buffer-size */
        y = startY + rowH;
        lbl2 = CreateWindowExW(0, L"STATIC", TR("STR_ADV_BUFFER_SIZE"), WS_CHILD | WS_VISIBLE, 20, y + 3, labelW, 25, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl2, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
        edt2 = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.buffer_size, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, editX, y, editW, 25, hwnd, (HMENU)IDC_ADV_EDIT_BS, NULL, NULL);
        SendMessageW(edt2, WM_SETFONT, (WPARAM)hFont, TRUE);

        /* 行3: transfers */
        y = startY + rowH * 2;
        lbl3 = CreateWindowExW(0, L"STATIC", TR("STR_ADV_TRANSFERS"), WS_CHILD | WS_VISIBLE, 20, y + 3, labelW, 25, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl3, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
        sprintf_s(transfersStr, sizeof(transfersStr), "%d", g_config.transfers);
        edt3 = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", transfersStr, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, editX, y, editW, 25, hwnd, (HMENU)IDC_ADV_EDIT_TR, NULL, NULL);
        SendMessageW(edt3, WM_SETFONT, (WPARAM)hFont, TRUE);

        /* 行4: cache-dir (with browse button) */
        y = startY + rowH * 3;
        lbl4 = CreateWindowExW(0, L"STATIC", TR("STR_ADV_CACHE_DIR"), WS_CHILD | WS_VISIBLE, 20, y + 3, labelW, 25, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl4, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
        edt4 = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.cache_dir, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, editX, y, editW - 70, 25, hwnd, (HMENU)IDC_ADV_EDIT_CD, NULL, NULL);
        SendMessageW(edt4, WM_SETFONT, (WPARAM)hFont, TRUE);
        btnBrowse = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, editX + editW - 60, y, 60, 25, hwnd, (HMENU)IDC_ADV_BTN_BROWSE, NULL, NULL);
        SendMessageW(btnBrowse, WM_SETFONT, (WPARAM)hFont, TRUE);

        /* 行5: vfs-cache-max-age */
        y = startY + rowH * 4;
        lbl5 = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_CACHE_MAX_AGE"), WS_CHILD | WS_VISIBLE, 20, y + 3, labelW, 25, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl5, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
        edt5 = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.vfs_cache_max_age, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, editX, y, editW, 25, hwnd, (HMENU)IDC_ADV_EDIT_CMA, NULL, NULL);
        SendMessageW(edt5, WM_SETFONT, (WPARAM)hFont, TRUE);

        /* 行6: vfs-read-chunk-size */
        y = startY + rowH * 5;
        lbl6 = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_READ_CHUNK"), WS_CHILD | WS_VISIBLE, 20, y + 3, labelW, 25, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl6, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
        edt6 = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.vfs_read_chunk_size, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, editX, y, editW, 25, hwnd, (HMENU)IDC_ADV_EDIT_RCS, NULL, NULL);
        SendMessageW(edt6, WM_SETFONT, (WPARAM)hFont, TRUE);

        /* 行7: vfs-read-chunk-size-limit */
        y = startY + rowH * 6;
        lbl7 = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_READ_CHUNK_LIMIT"), WS_CHILD | WS_VISIBLE, 20, y + 3, labelW, 25, hwnd, NULL, NULL, NULL);
        SendMessageW(lbl7, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
        edt7 = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.vfs_read_chunk_size_limit, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, editX, y, editW, 25, hwnd, (HMENU)IDC_ADV_EDIT_RCSL, NULL, NULL);
        SendMessageW(edt7, WM_SETFONT, (WPARAM)hFont, TRUE);

        /* 底部按钮 */
        y = startY + rowH * 7 + 15;
        btnOk = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_OK"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 100, y, 100, 32, hwnd, (HMENU)IDC_ADV_BTN_OK, NULL, NULL);
        SendMessageW(btnOk, WM_SETFONT, (WPARAM)hFont, TRUE);
        btnReset = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_RESET"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 220, y, 100, 32, hwnd, (HMENU)IDC_ADV_BTN_RESET, NULL, NULL);
        SendMessageW(btnReset, WM_SETFONT, (WPARAM)hFont, TRUE);
        btnCancel = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_CANCEL"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 340, y, 100, 32, hwnd, (HMENU)IDC_ADV_BTN_CANCEL, NULL, NULL);
        SendMessageW(btnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);

        /* 创建 Tooltip */
        g_hAdvTip = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL,
            WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, NULL, NULL, NULL);
        SendMessageW(g_hAdvTip, TTM_SETMAXTIPWIDTH, 0, 400);

        /* 为每个编辑框添加 Tooltip（使用数组替代匿名结构体，C89兼容） */
        tipEdits[0] = edt1; tipTexts[0] = TR("STR_ADV_HINT_DIR_CACHE_TIME");
        tipEdits[1] = edt2; tipTexts[1] = TR("STR_ADV_HINT_BUFFER_SIZE");
        tipEdits[2] = edt3; tipTexts[2] = TR("STR_ADV_HINT_TRANSFERS");
        tipEdits[3] = edt4; tipTexts[3] = TR("STR_ADV_HINT_CACHE_DIR");
        tipEdits[4] = edt5; tipTexts[4] = TR("STR_ADV_HINT_VFS_CACHE_MAX_AGE");
        tipEdits[5] = edt6; tipTexts[5] = TR("STR_ADV_HINT_VFS_READ_CHUNK");
        tipEdits[6] = edt7; tipTexts[6] = TR("STR_ADV_HINT_VFS_READ_CHUNK_LIMIT");
        for (i = 0; i < 7; i++) {
            memset(&ti, 0, sizeof(ti));
            ti.cbSize = sizeof(TOOLINFOW);
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = hwnd;
            ti.uId = (UINT_PTR)tipEdits[i];
            ti.lpszText = (LPWSTR)tipTexts[i];
            SendMessageW(g_hAdvTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        }

        SetPropW(hwnd, L"BOLD_FONT", hBoldFont);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_ADV_BTN_OK) {
            char transfersBuf[16];
            GetWindowTextA(GetDlgItem(hwnd, IDC_ADV_EDIT_DCT), g_config.dir_cache_time, sizeof(g_config.dir_cache_time));
            GetWindowTextA(GetDlgItem(hwnd, IDC_ADV_EDIT_BS), g_config.buffer_size, sizeof(g_config.buffer_size));
            memset(transfersBuf, 0, sizeof(transfersBuf));
            GetWindowTextA(GetDlgItem(hwnd, IDC_ADV_EDIT_TR), transfersBuf, sizeof(transfersBuf));
            g_config.transfers = atoi(transfersBuf);
            if (g_config.transfers <= 0) g_config.transfers = 4;
            GetWindowTextA(GetDlgItem(hwnd, IDC_ADV_EDIT_CD), g_config.cache_dir, sizeof(g_config.cache_dir));
            GetWindowTextA(GetDlgItem(hwnd, IDC_ADV_EDIT_CMA), g_config.vfs_cache_max_age, sizeof(g_config.vfs_cache_max_age));
            GetWindowTextA(GetDlgItem(hwnd, IDC_ADV_EDIT_RCS), g_config.vfs_read_chunk_size, sizeof(g_config.vfs_read_chunk_size));
            GetWindowTextA(GetDlgItem(hwnd, IDC_ADV_EDIT_RCSL), g_config.vfs_read_chunk_size_limit, sizeof(g_config.vfs_read_chunk_size_limit));
            SaveConfig(&g_config);
            DestroyWindow(hwnd);
        } else if (LOWORD(wParam) == IDC_ADV_BTN_CANCEL) {
            DestroyWindow(hwnd);
        } else if (LOWORD(wParam) == IDC_ADV_BTN_RESET) {
            SetWindowTextA(GetDlgItem(hwnd, IDC_ADV_EDIT_DCT), "72h");
            SetWindowTextA(GetDlgItem(hwnd, IDC_ADV_EDIT_BS), "16M");
            SetWindowTextA(GetDlgItem(hwnd, IDC_ADV_EDIT_TR), "4");
            SetWindowTextA(GetDlgItem(hwnd, IDC_ADV_EDIT_CD), "");
            SetWindowTextA(GetDlgItem(hwnd, IDC_ADV_EDIT_CMA), "24h");
            SetWindowTextA(GetDlgItem(hwnd, IDC_ADV_EDIT_RCS), "128M");
            SetWindowTextA(GetDlgItem(hwnd, IDC_ADV_EDIT_RCSL), "off");
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
                    SetWindowTextA(GetDlgItem(hwnd, IDC_ADV_EDIT_CD), ansiPath);
                }
                CoTaskMemFree(pidl);
            }
        }
        break;
    case WM_DESTROY: {
        HFONT hFont;
        HFONT hBoldFont;
        hFont = (HFONT)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (hFont) DeleteObject(hFont);
        hBoldFont = (HFONT)GetPropW(hwnd, L"BOLD_FONT");
        if (hBoldFont) { DeleteObject(hBoldFont); RemovePropW(hwnd, L"BOLD_FONT"); }
        g_hAdvTip = NULL;
        break;
    }
    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// 显示高级设置对话框（模态）
static void ShowAdvancedSettingsDialog(HWND hParent) {
    static int registered = 0;
    int dlgW, dlgH;
    RECT rcParent;
    int posX, posY;
    HWND hDlg;
    MSG msg;

    /* 注册对话框窗口类（只需注册一次） */
    if (!registered) {
        WNDCLASSW wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = AdvSettingsProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"AdvSettingsDlgClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = 1;
    }

    /* 计算居中位置（相对于父窗口） */
    dlgW = 510; dlgH = 360;
    GetWindowRect(hParent, &rcParent);
    posX = rcParent.left + (rcParent.right - rcParent.left - dlgW) / 2;
    posY = rcParent.top + (rcParent.bottom - rcParent.top - dlgH) / 2;

    /* 创建模态对话框 */
    hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME, L"AdvSettingsDlgClass", TR("STR_ADV_SETTINGS"),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        posX, posY, dlgW, dlgH,
        hParent, NULL, GetModuleHandleW(NULL), NULL
    );

    if (!hDlg) return;

    /* 禁用父窗口 */
    EnableWindow(hParent, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    /* 模态消息循环 */
    while (IsWindow(hDlg) && GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    /* 重新启用父窗口 */
    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);
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

    int vfsSel = (int)SendMessageW(hVfsCacheCombo, CB_GETCURSEL, 0, 0);
    g_config.vfs_cache_mode = (vfsSel != CB_ERR) ? vfsSel : 2;

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

        // VFS 缓存模式下拉框
        CreateBoldLabelW(TR("STR_VFS_CACHE_MODE"), 30, 295, 110, 28, hwnd);
        hVfsCacheCombo = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            145, 293, 150, 200, hwnd, (HMENU)9, NULL, NULL);
        if (g_hFont) SendMessageW(hVfsCacheCombo, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hVfsCacheCombo, CB_ADDSTRING, 0, (LPARAM)TR("STR_VFS_CACHE_OFF"));
        SendMessageW(hVfsCacheCombo, CB_ADDSTRING, 0, (LPARAM)TR("STR_VFS_CACHE_MINIMAL"));
        SendMessageW(hVfsCacheCombo, CB_ADDSTRING, 0, (LPARAM)TR("STR_VFS_CACHE_WRITES"));
        SendMessageW(hVfsCacheCombo, CB_ADDSTRING, 0, (LPARAM)TR("STR_VFS_CACHE_FULL"));
        SendMessageW(hVfsCacheCombo, CB_SETCURSEL, (WPARAM)g_config.vfs_cache_mode, 0);

        // VFS 缓存模式描述文本（两行高度，自动换行）
        hVfsDescLabel = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 30, 320, 505, 50, hwnd, NULL, NULL, NULL);
        if (g_hFont) SendMessageW(hVfsDescLabel, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // 创建 Tooltip 控件
        hVfsTip = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL,
            WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, NULL, NULL, NULL);
        TOOLINFOW ti = { 0 };
        ti.cbSize = sizeof(TOOLINFOW);
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = hwnd;
        ti.uId = (UINT_PTR)hVfsCacheCombo;
        ti.lpszText = (LPWSTR)GetVfsCacheTip(g_config.vfs_cache_mode);
        SendMessageW(hVfsTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        SendMessageW(hVfsTip, TTM_SETMAXTIPWIDTH, 0, 400);
        // 也为 ComboBox 内的 Edit 子控件添加 Tooltip，确保鼠标悬停在编辑区也能触发
        HWND hComboEdit = FindWindowExW(hVfsCacheCombo, NULL, L"Edit", NULL);
        if (hComboEdit) {
            TOOLINFOW tiEdit = { 0 };
            tiEdit.cbSize = sizeof(TOOLINFOW);
            tiEdit.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            tiEdit.hwnd = hwnd;
            tiEdit.uId = (UINT_PTR)hComboEdit;
            tiEdit.lpszText = (LPWSTR)GetVfsCacheTip(g_config.vfs_cache_mode);
            SendMessageW(hVfsTip, TTM_ADDTOOLW, 0, (LPARAM)&tiEdit);
        }

        // 初始化描述文本
        UpdateVfsCacheTip();

        hAutoHideCheck = CreateStyledWindowExW(0, L"BUTTON", TR("STR_AUTO_HIDE"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 30, 365, 250, 28, hwnd, (HMENU)8, NULL, NULL);
        if (g_config.auto_hide) SendMessageA(hAutoHideCheck, BM_SETCHECK, BST_CHECKED, 0);

        hAdvBtn = CreateStyledWindowExW(0, L"BUTTON", TR("STR_ADV_SETTINGS"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 300, 363, 120, 32, hwnd, (HMENU)10, NULL, NULL);

        hActionBtn = CreateStyledWindowExW(0, L"BUTTON", TR("STR_MOUNT_BTN"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 30, 410, 160, 42, hwnd, (HMENU)1, NULL, NULL);
        hHideBtn   = CreateStyledWindowExW(0, L"BUTTON", TR("STR_HIDE_BTN"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 205, 410, 175, 42, hwnd, (HMENU)7, NULL, NULL);
        hExitBtn   = CreateStyledWindowExW(0, L"BUTTON", TR("STR_TRAY_EXIT"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 395, 410, 155, 42, hwnd, (HMENU)4, NULL, NULL);

        CreateStyledWindowExW(0, L"STATIC", TR("STR_HIDE_TIP"), WS_CHILD | WS_VISIBLE | SS_CENTER, 30, 470, 520, 25, hwnd, NULL, NULL, NULL);

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
            // 高级设置按钮
            ShowAdvancedSettingsDialog(hwnd);
        } else if (LOWORD(wParam) == 9 && HIWORD(wParam) == CBN_SELCHANGE) {
            // VFS 缓存模式下拉框选择变更
            int sel = (int)SendMessageW(hVfsCacheCombo, CB_GETCURSEL, 0, 0);
            if (sel != CB_ERR) {
                g_config.vfs_cache_mode = sel;
                SaveConfig(&g_config);
                UpdateVfsCacheTip();
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

    // 计算屏幕中央的坐标
    int windowWidth = 580;
    int windowHeight = 560;
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