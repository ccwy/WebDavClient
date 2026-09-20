#include "list_page.h"
#include "i18n.h"
#include "rclone_manager.h"
#include "logger.h"
#include "protocol_webdav.h"
#include "protocol_smb.h"
#include "protocol_sftp.h"
#include "protocol_ftp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================================================================
   列表页模块实现
   ====================================================================== */

/* ---- 布局常量 ---- */
#define LP_MARGIN_X        15       /* 左右边距 */
#define LP_ROW_HEIGHT      50       /* 每行高度 */
#define LP_ROW_GAP         6        /* 行间距 */
#define LP_TOP_OFFSET      55       /* 顶部偏移（标题+添加按钮行） */
#define LP_BOTTOM_BAR      55       /* 底部操作栏高度 */
#define LP_BTN_WIDTH       70       /* 操作按钮宽度 */
#define LP_BTN_HEIGHT      32       /* 操作按钮高度 */
#define LP_ADD_BTN_WIDTH   120      /* 添加按钮宽度 */
#define LP_NAME_WIDTH      140      /* 名称标签宽度 */
#define LP_PROTO_WIDTH     50       /* 协议标签宽度 */
#define LP_DRIVE_WIDTH     35       /* 盘符标签宽度 */

/* ---- 控件 ID 范围 ---- */
/* 行按钮 ID: 1000 + row*10 + offset
 *   offset: 0=挂载/卸载, 1=高级, 2=编辑, 3=删除
 * 底部按钮: 2000+范围
 */
#define LP_ID_ROW_BASE     1000
#define LP_ID_MOUNT_ALL    2000
#define LP_ID_UNMOUNT_ALL  2001
#define LP_ID_HIDE         2002
#define LP_ID_EXIT         2003
#define LP_ID_GLOBAL_ADV   2004
#define LP_ID_ADD          2005

/* ---- 辅助函数 ---- */

/* 协议名称转显示名 */
static const wchar_t* ProtocolDisplayName(const char* proto) {
    if (strcmp(proto, "webdav") == 0) return L"WebDAV";
    if (strcmp(proto, "smb") == 0)    return L"SMB";
    if (strcmp(proto, "sftp") == 0)   return L"SFTP";
    if (strcmp(proto, "ftp") == 0)    return L"FTP";
    return L"?";
}

/* 从按钮 ID 反推行索引，返回 -1 表示不是行按钮 */
static int RowFromCtrlId(int ctrlId) {
    if (ctrlId < LP_ID_ROW_BASE) return -1;
    int row = (ctrlId - LP_ID_ROW_BASE) / 10;
    int offset = (ctrlId - LP_ID_ROW_BASE) % 10;
    if (offset < 0 || offset > 3) return -1;
    return row;
}

/* 从行索引和偏移生成控件 ID */
static int CtrlIdFromRow(int row, int offset) {
    return LP_ID_ROW_BASE + row * 10 + offset;
}

/* ---- 销毁所有行控件 ---- */
static void DestroyAllRows(ListPageData* data) {
    int i;
    for (i = 0; i < data->rowCount; i++) {
        if (data->rows[i].hNameLabel)     { DestroyWindow(data->rows[i].hNameLabel);     data->rows[i].hNameLabel = NULL; }
        if (data->rows[i].hProtocolLabel) { DestroyWindow(data->rows[i].hProtocolLabel); data->rows[i].hProtocolLabel = NULL; }
        if (data->rows[i].hDriveLabel)    { DestroyWindow(data->rows[i].hDriveLabel);    data->rows[i].hDriveLabel = NULL; }
        if (data->rows[i].hMountBtn)      { DestroyWindow(data->rows[i].hMountBtn);      data->rows[i].hMountBtn = NULL; }
        if (data->rows[i].hAdvBtn)        { DestroyWindow(data->rows[i].hAdvBtn);        data->rows[i].hAdvBtn = NULL; }
        if (data->rows[i].hEditBtn)       { DestroyWindow(data->rows[i].hEditBtn);       data->rows[i].hEditBtn = NULL; }
        if (data->rows[i].hDeleteBtn)     { DestroyWindow(data->rows[i].hDeleteBtn);     data->rows[i].hDeleteBtn = NULL; }
        if (data->rows[i].hStatusIcon)    { DestroyWindow(data->rows[i].hStatusIcon);    data->rows[i].hStatusIcon = NULL; }
        data->rows[i].connId[0] = '\0';
    }
    data->rowCount = 0;
}

/* ---- 创建单行控件 ---- */
static void CreateRowControls(ListPageData* data, int row, int y) {
    ConnectionConfig* conn = &data->appCfg->connections[row];
    HWND hwnd = data->hwnd;
    HFONT hFont = data->hFont;
    int x = LP_MARGIN_X;
    wchar_t wbuf[256];

    /* 保存连接 ID */
    strcpy_s(data->rows[row].connId, sizeof(data->rows[row].connId), conn->id);

    /* 状态图标（用静态文本模拟：● 绿=已挂载, ○ 灰=未挂载） */
    data->rows[row].hStatusIcon = CreateWindowExW(0, L"STATIC",
        IsMounted(conn->id) ? L"\u25CF" : L"\u25CB",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        x, y + 14, 20, LP_ROW_HEIGHT - 28,
        hwnd, NULL, NULL, NULL);
    if (data->rows[row].hStatusIcon && hFont)
        SendMessageW(data->rows[row].hStatusIcon, WM_SETFONT, (WPARAM)hFont, TRUE);
    x += 24;

    /* 连接名称 */
    MultiByteToWideChar(CP_UTF8, 0, conn->name, -1, wbuf, 256);
    data->rows[row].hNameLabel = CreateWindowExW(0, L"STATIC", wbuf,
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        x, y, LP_NAME_WIDTH, LP_ROW_HEIGHT,
        hwnd, NULL, NULL, NULL);
    if (data->rows[row].hNameLabel && hFont)
        SendMessageW(data->rows[row].hNameLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    x += LP_NAME_WIDTH;

    /* 协议标签 */
    data->rows[row].hProtocolLabel = CreateWindowExW(0, L"STATIC",
        ProtocolDisplayName(conn->protocol),
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        x, y, LP_PROTO_WIDTH, LP_ROW_HEIGHT,
        hwnd, NULL, NULL, NULL);
    if (data->rows[row].hProtocolLabel && hFont)
        SendMessageW(data->rows[row].hProtocolLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    x += LP_PROTO_WIDTH;

    /* 盘符标签 */
    wchar_t driveW[8];
    MultiByteToWideChar(CP_ACP, 0, conn->drive, -1, driveW, 8);
    data->rows[row].hDriveLabel = CreateWindowExW(0, L"STATIC", driveW,
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        x, y, LP_DRIVE_WIDTH, LP_ROW_HEIGHT,
        hwnd, NULL, NULL, NULL);
    if (data->rows[row].hDriveLabel && hFont)
        SendMessageW(data->rows[row].hDriveLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    x += LP_DRIVE_WIDTH + 10;

    /* 挂载/卸载按钮 */
    const wchar_t* mountText = IsMounted(conn->id) ? TR("STR_UNMOUNT_BTN") : TR("STR_MOUNT_BTN");
    data->rows[row].hMountBtn = CreateWindowExW(0, L"BUTTON", mountText,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y + 9, LP_BTN_WIDTH, LP_BTN_HEIGHT,
        hwnd, (HMENU)(INT_PTR)CtrlIdFromRow(row, 0), NULL, NULL);
    if (data->rows[row].hMountBtn && hFont)
        SendMessageW(data->rows[row].hMountBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    x += LP_BTN_WIDTH + 4;

    /* 高级设置按钮 */
    data->rows[row].hAdvBtn = CreateWindowExW(0, L"BUTTON", TR("STR_LIST_ADV_BTN"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y + 9, LP_BTN_WIDTH, LP_BTN_HEIGHT,
        hwnd, (HMENU)(INT_PTR)CtrlIdFromRow(row, 1), NULL, NULL);
    if (data->rows[row].hAdvBtn && hFont)
        SendMessageW(data->rows[row].hAdvBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    x += LP_BTN_WIDTH + 4;

    /* 编辑按钮 */
    data->rows[row].hEditBtn = CreateWindowExW(0, L"BUTTON", TR("STR_LIST_EDIT"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y + 9, LP_BTN_WIDTH, LP_BTN_HEIGHT,
        hwnd, (HMENU)(INT_PTR)CtrlIdFromRow(row, 2), NULL, NULL);
    if (data->rows[row].hEditBtn && hFont)
        SendMessageW(data->rows[row].hEditBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    x += LP_BTN_WIDTH + 4;

    /* 删除按钮 */
    data->rows[row].hDeleteBtn = CreateWindowExW(0, L"BUTTON", TR("STR_LIST_DELETE"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y + 9, LP_BTN_WIDTH, LP_BTN_HEIGHT,
        hwnd, (HMENU)(INT_PTR)CtrlIdFromRow(row, 3), NULL, NULL);
    if (data->rows[row].hDeleteBtn && hFont)
        SendMessageW(data->rows[row].hDeleteBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
}

/* ---- 创建底部操作栏 ---- */
static void CreateBottomBar(ListPageData* data) {
    HWND hwnd = data->hwnd;
    HFONT hFont = data->hFont;
    RECT rc;
    int y, x, btnW;

    GetClientRect(hwnd, &rc);
    y = rc.bottom - LP_BOTTOM_BAR + 10;
    x = LP_MARGIN_X;
    btnW = 100;

    data->hMountAllBtn = CreateWindowExW(0, L"BUTTON", TR("STR_LIST_MOUNT_ALL"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, btnW, LP_BTN_HEIGHT + 4,
        hwnd, (HMENU)(INT_PTR)LP_ID_MOUNT_ALL, NULL, NULL);
    if (data->hMountAllBtn && hFont)
        SendMessageW(data->hMountAllBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    x += btnW + 6;

    data->hUnmountAllBtn = CreateWindowExW(0, L"BUTTON", TR("STR_LIST_UNMOUNT_ALL"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, btnW, LP_BTN_HEIGHT + 4,
        hwnd, (HMENU)(INT_PTR)LP_ID_UNMOUNT_ALL, NULL, NULL);
    if (data->hUnmountAllBtn && hFont)
        SendMessageW(data->hUnmountAllBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    x += btnW + 6;

    data->hGlobalAdvBtn = CreateWindowExW(0, L"BUTTON", TR("STR_LIST_SETTINGS_BTN"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, btnW, LP_BTN_HEIGHT + 4,
        hwnd, (HMENU)(INT_PTR)LP_ID_GLOBAL_ADV, NULL, NULL);
    if (data->hGlobalAdvBtn && hFont)
        SendMessageW(data->hGlobalAdvBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    x += btnW + 6;

    data->hHideBtn = CreateWindowExW(0, L"BUTTON", TR("STR_HIDE_BTN"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, btnW, LP_BTN_HEIGHT + 4,
        hwnd, (HMENU)(INT_PTR)LP_ID_HIDE, NULL, NULL);
    if (data->hHideBtn && hFont)
        SendMessageW(data->hHideBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    x += btnW + 6;

    data->hExitBtn = CreateWindowExW(0, L"BUTTON", TR("STR_TRAY_EXIT"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, btnW, LP_BTN_HEIGHT + 4,
        hwnd, (HMENU)(INT_PTR)LP_ID_EXIT, NULL, NULL);
    if (data->hExitBtn && hFont)
        SendMessageW(data->hExitBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
}

/* ---- 更新滚动范围 ---- */
static void UpdateScrollRange(ListPageData* data) {
    RECT rc;
    SCROLLINFO si;
    GetClientRect(data->hwnd, &rc);

    data->contentHeight = LP_TOP_OFFSET + data->rowCount * (LP_ROW_HEIGHT + LP_ROW_GAP)
                          + LP_BOTTOM_BAR + 20;

    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = data->contentHeight;
    si.nPage = (UINT)(rc.bottom > 0 ? rc.bottom : 1);
    si.nPos = data->scrollPos;
    SetScrollInfo(data->hwnd, SB_VERT, &si, TRUE);

    if (data->contentHeight <= (int)si.nPage) {
        ShowScrollBar(data->hwnd, SB_VERT, FALSE);
    } else {
        ShowScrollBar(data->hwnd, SB_VERT, TRUE);
    }
}

/* ---- 重新定位所有行控件（滚动后） ---- */
static void RepositionRows(ListPageData* data) {
    int i;
    int yBase = LP_TOP_OFFSET - data->scrollPos;

    for (i = 0; i < data->rowCount; i++) {
        int y = yBase + i * (LP_ROW_HEIGHT + LP_ROW_GAP);
        int x = LP_MARGIN_X;

        /* 状态图标 */
        if (data->rows[i].hStatusIcon)
            MoveWindow(data->rows[i].hStatusIcon, x, y + 14, 20, LP_ROW_HEIGHT - 28, TRUE);
        x += 24;

        /* 名称 */
        if (data->rows[i].hNameLabel)
            MoveWindow(data->rows[i].hNameLabel, x, y, LP_NAME_WIDTH, LP_ROW_HEIGHT, TRUE);
        x += LP_NAME_WIDTH;

        /* 协议 */
        if (data->rows[i].hProtocolLabel)
            MoveWindow(data->rows[i].hProtocolLabel, x, y, LP_PROTO_WIDTH, LP_ROW_HEIGHT, TRUE);
        x += LP_PROTO_WIDTH;

        /* 盘符 */
        if (data->rows[i].hDriveLabel)
            MoveWindow(data->rows[i].hDriveLabel, x, y, LP_DRIVE_WIDTH, LP_ROW_HEIGHT, TRUE);
        x += LP_DRIVE_WIDTH + 10;

        /* 挂载/卸载按钮 */
        if (data->rows[i].hMountBtn)
            MoveWindow(data->rows[i].hMountBtn, x, y + 9, LP_BTN_WIDTH, LP_BTN_HEIGHT, TRUE);
        x += LP_BTN_WIDTH + 4;

        /* 高级设置按钮 */
        if (data->rows[i].hAdvBtn)
            MoveWindow(data->rows[i].hAdvBtn, x, y + 9, LP_BTN_WIDTH, LP_BTN_HEIGHT, TRUE);
        x += LP_BTN_WIDTH + 4;

        /* 编辑按钮 */
        if (data->rows[i].hEditBtn)
            MoveWindow(data->rows[i].hEditBtn, x, y + 9, LP_BTN_WIDTH, LP_BTN_HEIGHT, TRUE);
        x += LP_BTN_WIDTH + 4;

        /* 删除按钮 */
        if (data->rows[i].hDeleteBtn)
            MoveWindow(data->rows[i].hDeleteBtn, x, y + 9, LP_BTN_WIDTH, LP_BTN_HEIGHT, TRUE);
    }
}

/* ======================================================================
   公共接口实现
   ====================================================================== */

void ListPage_Create(ListPageData* data, HWND hwnd, AppConfig* appCfg,
                     char* rclonePath, HFONT hFont, HFONT hBoldFont,
                     ListPageCallbacks callbacks) {
    memset(data, 0, sizeof(ListPageData));
    data->hwnd = hwnd;
    data->appCfg = appCfg;
    data->rclonePath = rclonePath;
    data->hFont = hFont;
    data->hBoldFont = hBoldFont;
    data->callbacks = callbacks;
    data->scrollPos = 0;

    /* 创建标题 */
    data->hTitleLabel = CreateWindowExW(0, L"STATIC", TR("STR_LIST_TITLE"),
        WS_CHILD | SS_LEFT,
        LP_MARGIN_X, 12, 300, 28, hwnd, NULL, NULL, NULL);
    if (data->hTitleLabel && hBoldFont)
        SendMessageW(data->hTitleLabel, WM_SETFONT, (WPARAM)hBoldFont, TRUE);

    /* 创建添加按钮 */
    data->hAddBtn = CreateWindowExW(0, L"BUTTON", TR("STR_LIST_ADD"),
        WS_CHILD | BS_PUSHBUTTON,
        LP_MARGIN_X + 310, 10, LP_ADD_BTN_WIDTH, LP_BTN_HEIGHT + 4,
        hwnd, (HMENU)(INT_PTR)LP_ID_ADD, NULL, NULL);
    if (data->hAddBtn && hFont)
        SendMessageW(data->hAddBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    /* 创建底部操作栏 */
    CreateBottomBar(data);

    /* 初始不显示，等 ListPage_Show 时再创建行控件 */
}

void ListPage_Destroy(ListPageData* data) {
    DestroyAllRows(data);

    if (data->hTitleLabel)    { DestroyWindow(data->hTitleLabel);    data->hTitleLabel = NULL; }
    if (data->hAddBtn)        { DestroyWindow(data->hAddBtn);        data->hAddBtn = NULL; }
    if (data->hMountAllBtn)   { DestroyWindow(data->hMountAllBtn);   data->hMountAllBtn = NULL; }
    if (data->hUnmountAllBtn) { DestroyWindow(data->hUnmountAllBtn); data->hUnmountAllBtn = NULL; }
    if (data->hHideBtn)       { DestroyWindow(data->hHideBtn);       data->hHideBtn = NULL; }
    if (data->hExitBtn)       { DestroyWindow(data->hExitBtn);       data->hExitBtn = NULL; }
    if (data->hGlobalAdvBtn)  { DestroyWindow(data->hGlobalAdvBtn);  data->hGlobalAdvBtn = NULL; }
}

void ListPage_Show(ListPageData* data) {
    /* 重新加载配置数据 */
    LoadAppConfig(data->appCfg);

    /* 显示标题和添加按钮 */
    ShowWindow(data->hTitleLabel, SW_SHOW);
    ShowWindow(data->hAddBtn, SW_SHOW);

    /* 显示底部操作栏 */
    ShowWindow(data->hMountAllBtn, SW_SHOW);
    ShowWindow(data->hUnmountAllBtn, SW_SHOW);
    ShowWindow(data->hHideBtn, SW_SHOW);
    ShowWindow(data->hExitBtn, SW_SHOW);
    ShowWindow(data->hGlobalAdvBtn, SW_SHOW);

    /* 重建行控件 */
    ListPage_Refresh(data);

    /* 更新窗口标题 */
    SetWindowTextW(data->hwnd, TR("STR_LIST_TITLE"));

    InvalidateRect(data->hwnd, NULL, TRUE);
}

void ListPage_Hide(ListPageData* data) {
    int i;

    /* 隐藏标题和添加按钮 */
    ShowWindow(data->hTitleLabel, SW_HIDE);
    ShowWindow(data->hAddBtn, SW_HIDE);

    /* 隐藏底部操作栏 */
    ShowWindow(data->hMountAllBtn, SW_HIDE);
    ShowWindow(data->hUnmountAllBtn, SW_HIDE);
    ShowWindow(data->hHideBtn, SW_HIDE);
    ShowWindow(data->hExitBtn, SW_HIDE);
    ShowWindow(data->hGlobalAdvBtn, SW_HIDE);

    /* 隐藏所有行控件 */
    for (i = 0; i < data->rowCount; i++) {
        if (data->rows[i].hStatusIcon)    ShowWindow(data->rows[i].hStatusIcon, SW_HIDE);
        if (data->rows[i].hNameLabel)     ShowWindow(data->rows[i].hNameLabel, SW_HIDE);
        if (data->rows[i].hProtocolLabel) ShowWindow(data->rows[i].hProtocolLabel, SW_HIDE);
        if (data->rows[i].hDriveLabel)    ShowWindow(data->rows[i].hDriveLabel, SW_HIDE);
        if (data->rows[i].hMountBtn)      ShowWindow(data->rows[i].hMountBtn, SW_HIDE);
        if (data->rows[i].hAdvBtn)        ShowWindow(data->rows[i].hAdvBtn, SW_HIDE);
        if (data->rows[i].hEditBtn)       ShowWindow(data->rows[i].hEditBtn, SW_HIDE);
        if (data->rows[i].hDeleteBtn)     ShowWindow(data->rows[i].hDeleteBtn, SW_HIDE);
    }

    /* 清零并隐藏滚动条 */
    {
        SCROLLINFO si;
        memset(&si, 0, sizeof(si));
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        SetScrollInfo(data->hwnd, SB_VERT, &si, TRUE);
        ShowScrollBar(data->hwnd, SB_VERT, FALSE);
    }
}

void ListPage_Refresh(ListPageData* data) {
    int i;
    int yBase;
    int y;

    /* 先销毁旧行控件 */
    DestroyAllRows(data);

    /* 重新加载配置 */
    LoadAppConfig(data->appCfg);

    /* 创建新行控件 */
    data->rowCount = data->appCfg->count;
    yBase = LP_TOP_OFFSET - data->scrollPos;

    for (i = 0; i < data->rowCount; i++) {
        y = yBase + i * (LP_ROW_HEIGHT + LP_ROW_GAP);
        CreateRowControls(data, i, y);
    }

    /* 更新滚动范围 */
    UpdateScrollRange(data);

    InvalidateRect(data->hwnd, NULL, TRUE);
}

void ListPage_UpdateMountStatus(ListPageData* data, const char* connId) {
    int i;
    for (i = 0; i < data->rowCount; i++) {
        if (strcmp(data->rows[i].connId, connId) == 0) {
            int mounted = IsMounted(connId);
            /* 更新状态图标 */
            if (data->rows[i].hStatusIcon) {
                SetWindowTextW(data->rows[i].hStatusIcon, mounted ? L"\u25CF" : L"\u25CB");
            }
            /* 更新挂载/卸载按钮文字 */
            if (data->rows[i].hMountBtn) {
                SetWindowTextW(data->rows[i].hMountBtn,
                    mounted ? TR("STR_UNMOUNT_BTN") : TR("STR_MOUNT_BTN"));
                InvalidateRect(data->rows[i].hMountBtn, NULL, TRUE);
            }
            break;
        }
    }
}

int ListPage_HandleCommand(ListPageData* data, WPARAM wParam, LPARAM lParam) {
    int cmdId = LOWORD(wParam);
    int row, offset;
    const char* connId;
    ConnectionConfig* conn;
    int mounted;

    /* ---- 底部按钮 ---- */
    if (cmdId == LP_ID_ADD) {
        if (data->callbacks.OnAddConnection)
            data->callbacks.OnAddConnection(data->callbacks.ctx);
        return 1;
    }
    if (cmdId == LP_ID_MOUNT_ALL) {
        /* 全部挂载：遍历所有未挂载的连接，调用 ExecuteMountFromConfig */
        int i;
        int mountOk = 0, mountFail = 0;
        for (i = 0; i < data->appCfg->count; i++) {
            conn = &data->appCfg->connections[i];
            if (!IsMounted(conn->id)) {
                /* 创建临时协议处理器执行挂载 */
                ProtocolHandler* h = NULL;
                if (strcmp(conn->protocol, "webdav") == 0)
                    h = CreateWebDavHandler(conn, &data->appCfg->global);
                else if (strcmp(conn->protocol, "smb") == 0)
                    h = CreateSmbHandler(conn, &data->appCfg->global);
                else if (strcmp(conn->protocol, "sftp") == 0)
                    h = CreateSftpHandler(conn, &data->appCfg->global);
                else if (strcmp(conn->protocol, "ftp") == 0)
                    h = CreateFtpHandler(conn, &data->appCfg->global);

                if (h) {
                    int result;
                    h->LoadConfig(h);
                    result = h->ExecuteMountFromConfig(h, data->hwnd, data->rclonePath);
                    h->Destroy(h);
                    free(h);
                    if (result > 0) mountOk++;
                    else mountFail++;
                }
                ListPage_UpdateMountStatus(data, conn->id);
            }
        }
        /* 汇总提示 */
        if (mountOk > 0 || mountFail > 0) {
            wchar_t msg[256];
            swprintf_s(msg, 256, TR("MSG_MOUNT_RESULT"), mountOk, mountFail);
            MessageBoxW(data->hwnd, msg, TR("MSG_INFO"),
                        MB_OK | (mountFail > 0 ? MB_ICONWARNING : MB_ICONINFORMATION));
        }
        if (mountOk > 0 && data->appCfg->global.auto_hide && data->callbacks.OnHide)
            data->callbacks.OnHide(data->callbacks.ctx);
        return 1;
    }
    if (cmdId == LP_ID_UNMOUNT_ALL) {
        StopAllMounts();
        /* 刷新所有行状态 */
        {
            int i;
            for (i = 0; i < data->rowCount; i++) {
                ListPage_UpdateMountStatus(data, data->rows[i].connId);
            }
        }
        MessageBoxW(data->hwnd, TR("MSG_UNMOUNT_ALL_OK"), TR("MSG_INFO"),
                    MB_OK | MB_ICONINFORMATION);
        return 1;
    }
    if (cmdId == LP_ID_HIDE) {
        if (data->callbacks.OnHide)
            data->callbacks.OnHide(data->callbacks.ctx);
        return 1;
    }
    if (cmdId == LP_ID_EXIT) {
        if (data->callbacks.OnExit)
            data->callbacks.OnExit(data->callbacks.ctx);
        return 1;
    }
    if (cmdId == LP_ID_GLOBAL_ADV) {
        if (data->callbacks.OnGlobalAdvSettings)
            data->callbacks.OnGlobalAdvSettings(data->callbacks.ctx);
        return 1;
    }

    /* ---- 行按钮 ---- */
    row = RowFromCtrlId(cmdId);
    if (row < 0 || row >= data->rowCount) return 0;

    offset = (cmdId - LP_ID_ROW_BASE) % 10;
    connId = data->rows[row].connId;
    conn = &data->appCfg->connections[row];
    mounted = IsMounted(connId);

    if (offset == 0) {
        /* 挂载/卸载按钮 */
        if (mounted) {
            StopRcloneMount(connId);
            ListPage_UpdateMountStatus(data, connId);
            MessageBoxW(data->hwnd, TR("MSG_UNMOUNT_OK"), TR("MSG_INFO"),
                        MB_OK | MB_ICONINFORMATION);
        } else {
            /* 创建临时协议处理器执行挂载 */
            ProtocolHandler* h = NULL;
            if (strcmp(conn->protocol, "webdav") == 0)
                h = CreateWebDavHandler(conn, &data->appCfg->global);
            else if (strcmp(conn->protocol, "smb") == 0)
                h = CreateSmbHandler(conn, &data->appCfg->global);
            else if (strcmp(conn->protocol, "sftp") == 0)
                h = CreateSftpHandler(conn, &data->appCfg->global);
            else if (strcmp(conn->protocol, "ftp") == 0)
                h = CreateFtpHandler(conn, &data->appCfg->global);

            if (h) {
                int result;
                h->LoadConfig(h);
                result = h->ExecuteMountFromConfig(h, data->hwnd, data->rclonePath);
                h->Destroy(h);
                free(h);

                if (result > 0) {
                    ListPage_UpdateMountStatus(data, connId);
                    if (!data->appCfg->global.auto_hide) {
                        MessageBoxW(data->hwnd, TR("MSG_MOUNT_OK"), TR("MSG_INFO"),
                                    MB_OK | MB_ICONINFORMATION);
                    }
                    if (data->appCfg->global.auto_hide && data->callbacks.OnHide)
                        data->callbacks.OnHide(data->callbacks.ctx);
                } else if (result == 0) {
                    /* result=0: 失败且未显示具体错误，弹通用错误；result=-1: 已显示具体错误 */
                    MessageBoxW(data->hwnd, TR("MSG_MOUNT_FAIL"), TR("MSG_ERROR"),
                                MB_OK | MB_ICONERROR);
                }
            }
        }
        return 1;
    }
    if (offset == 1) {
        /* 高级设置按钮 */
        if (data->callbacks.OnAdvSettings)
            data->callbacks.OnAdvSettings(data->callbacks.ctx, connId);
        return 1;
    }
    if (offset == 2) {
        /* 编辑按钮 */
        if (data->callbacks.OnEditConnection)
            data->callbacks.OnEditConnection(data->callbacks.ctx, connId);
        return 1;
    }
    if (offset == 3) {
        /* 删除按钮 */
        wchar_t msg[512];
        wchar_t nameW[128];
        MultiByteToWideChar(CP_UTF8, 0, conn->name, -1, nameW, 128);
        swprintf_s(msg, 512, L"%ls\n%ls", nameW, TR("MSG_DELETE_CONFIRM"));
        if (MessageBoxW(data->hwnd, msg, TR("MSG_INFO"),
                        MB_YESNO | MB_ICONQUESTION) == IDYES) {
            /* 先卸载（如果已挂载） */
            if (IsMounted(connId)) {
                StopRcloneMount(connId);
            }
            /* 从配置中删除 */
            RemoveConnection(data->appCfg, connId);
            /* 刷新列表 */
            ListPage_Refresh(data);
        }
        return 1;
    }

    return 0;
}

void ListPage_HandleScroll(ListPageData* data, int delta) {
    SCROLLINFO si;
    int newPos;

    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(data->hwnd, SB_VERT, &si);

    newPos = (int)si.nPos + delta;
    if (newPos < 0) newPos = 0;
    if (newPos > data->contentHeight - (int)si.nPage)
        newPos = data->contentHeight - (int)si.nPage;

    if (newPos != data->scrollPos) {
        data->scrollPos = newPos;
        si.fMask = SIF_POS;
        si.nPos = newPos;
        SetScrollInfo(data->hwnd, SB_VERT, &si, TRUE);

        SendMessage(data->hwnd, WM_SETREDRAW, FALSE, 0);
        RepositionRows(data);
        SendMessage(data->hwnd, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(data->hwnd, NULL, NULL,
                     RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
    }
}

void ListPage_HandleMouseWheel(ListPageData* data, int delta) {
    ListPage_HandleScroll(data, -(delta / WHEEL_DELTA * 30));
}

void ListPage_HandleEraseBkgnd(ListPageData* data, HDC hdc) {
    RECT rc;
    GetClientRect(data->hwnd, &rc);
    FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
}

LRESULT ListPage_HandleCtlColor(ListPageData* data, HWND hCtrl, HDC hdc) {
    /* 列表页使用默认背景色 */
    (void)data; (void)hCtrl;
    SetBkMode(hdc, TRANSPARENT);
    return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
}