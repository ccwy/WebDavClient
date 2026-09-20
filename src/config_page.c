#include "config_page.h"
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
   配置页模块实现
   ====================================================================== */

/* ---- 布局常量 ---- */
#define CP_MARGIN_X        30       /* 左边距 */
#define CP_LABEL_WIDTH     110      /* 标签宽度 */
#define CP_EDIT_X          145      /* 编辑框左边距 */
#define CP_EDIT_WIDTH      390      /* 编辑框宽度 */
#define CP_SMALL_EDIT_W    60       /* 小编辑框宽度（盘符） */
#define CP_ROW_HEIGHT      28       /* 行高度 */
#define CP_YOFFSET         115      /* 协议专属控件 y 偏移（通用控件占 185px，协议控件从 y=70 开始） */
#define CP_BUTTON_Y        400      /* 操作按钮行 y 位置 */
#define CP_BTN_WIDTH       121      /* 按钮宽度 */
#define CP_BTN_HEIGHT      32       /* 按钮高度 */
#define CP_ADV_TOP         55       /* 高级设置顶部偏移 */

/* ---- 控件 ID ---- */
#define CP_ID_NAME_BOX         3001
#define CP_ID_PROTOCOL_COMBO   3002
#define CP_ID_DRIVE_BOX        3003
#define CP_ID_SAVE_BTN         3004
#define CP_ID_CANCEL_BTN       3005
#define CP_ID_ADV_BTN          3006

/* ---- 已知的高级设置按钮 ID（用于拦截 Back/Save 点击） ---- */
#define CP_KNOWN_ADV_BACK_IDS   215, 308, 408, 504
#define CP_KNOWN_ADV_SAVE_IDS   216, 309, 409, 505

/* ---- 辅助函数 ---- */

/* 创建粗体标签 */
static HWND CPCreateBoldLabel(const wchar_t* text, int x, int y, int w, int h,
                               HWND parent, HFONT hBoldFont) {
    HWND hw = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, parent, NULL, NULL, NULL);
    SendMessageW(hw, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    return hw;
}

/* 创建带边框的编辑框（ANSI 版） */
static HWND CPCreateEditA(const char* initText, int x, int y, int w, int h,
                           HWND parent, HFONT hFont, DWORD extraStyle) {
    HWND hw = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", initText,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | extraStyle,
        x, y, w, h, parent, NULL, NULL, NULL);
    SendMessageW(hw, WM_SETFONT, (WPARAM)hFont, TRUE);
    return hw;
}

/* 创建下拉框 */
static HWND CPCreateCombo(HWND parent, int x, int y, int w, int h, HFONT hFont, int id) {
    HWND hw = CreateWindowExW(0, L"COMBOBOX", NULL,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(hw, WM_SETFONT, (WPARAM)hFont, TRUE);
    return hw;
}

/* 创建按钮 */
static HWND CPCreateButton(const wchar_t* text, int x, int y, int w, int h,
                            HWND parent, HFONT hFont, int id) {
    HWND hw = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(hw, WM_SETFONT, (WPARAM)hFont, TRUE);
    return hw;
}

/* 根据协议名创建 ProtocolHandler */
static ProtocolHandler* CreateHandlerForProtocol(const char* protocol,
                                                  ConnectionConfig* connCfg,
                                                  GlobalConfig* globalCfg) {
    if (strcmp(protocol, "webdav") == 0) return CreateWebDavHandler(connCfg, globalCfg);
    if (strcmp(protocol, "smb") == 0)    return CreateSmbHandler(connCfg, globalCfg);
    if (strcmp(protocol, "sftp") == 0)   return CreateSftpHandler(connCfg, globalCfg);
    if (strcmp(protocol, "ftp") == 0)    return CreateFtpHandler(connCfg, globalCfg);
    return NULL;
}

/* 检查控件 ID 是否为已知的高级设置 Back 按钮 */
static int IsAdvBackId(int ctrlId) {
    int ids[] = { CP_KNOWN_ADV_BACK_IDS };
    int i;
    for (i = 0; i < (int)(sizeof(ids)/sizeof(ids[0])); i++) {
        if (ctrlId == ids[i]) return 1;
    }
    return 0;
}

/* 检查控件 ID 是否为已知的高级设置 Save 按钮 */
static int IsAdvSaveId(int ctrlId) {
    int ids[] = { CP_KNOWN_ADV_SAVE_IDS };
    int i;
    for (i = 0; i < (int)(sizeof(ids)/sizeof(ids[0])); i++) {
        if (ctrlId == ids[i]) return 1;
    }
    return 0;
}

/* ---- 内部控件管理 ---- */

/* 创建通用控件（名称、协议、盘符） */
static void CreateCommonControls(ConfigPageData* data) {
    HWND hwnd = data->hwnd;
    HFONT hFont = data->hFont;
    HFONT hBoldFont = data->hBoldFont;

    /* 名称 */
    data->hNameLabel = CPCreateBoldLabel(TR("STR_CFG_NAME"), CP_MARGIN_X, 50,
        CP_LABEL_WIDTH, CP_ROW_HEIGHT, hwnd, hBoldFont);
    data->hNameBox = CPCreateEditA("", CP_EDIT_X, 50, CP_EDIT_WIDTH, CP_ROW_HEIGHT,
        hwnd, hFont, 0);

    /* 协议 */
    data->hProtocolLabel = CPCreateBoldLabel(TR("STR_CFG_PROTOCOL"), CP_MARGIN_X, 95,
        CP_LABEL_WIDTH, CP_ROW_HEIGHT, hwnd, hBoldFont);
    data->hProtocolCombo = CPCreateCombo(hwnd, CP_EDIT_X, 95, CP_EDIT_WIDTH, 200, hFont, CP_ID_PROTOCOL_COMBO);
    SendMessageW(data->hProtocolCombo, CB_ADDSTRING, 0, (LPARAM)L"WebDAV");
    SendMessageW(data->hProtocolCombo, CB_ADDSTRING, 0, (LPARAM)L"SMB");
    SendMessageW(data->hProtocolCombo, CB_ADDSTRING, 0, (LPARAM)L"SFTP");
    SendMessageW(data->hProtocolCombo, CB_ADDSTRING, 0, (LPARAM)L"FTP");

    /* 盘符 */
    data->hDriveLabel = CPCreateBoldLabel(TR("STR_DRIVE"), CP_MARGIN_X, 140,
        CP_LABEL_WIDTH, CP_ROW_HEIGHT, hwnd, hBoldFont);
    data->hDriveBox = CPCreateEditA("", CP_EDIT_X, 140, CP_SMALL_EDIT_W, CP_ROW_HEIGHT,
        hwnd, hFont, 0);
}

/* 销毁通用控件 */
static void DestroyCommonControls(ConfigPageData* data) {
    if (data->hTitleLabel)    { DestroyWindow(data->hTitleLabel);    data->hTitleLabel = NULL; }
    if (data->hNameLabel)     { DestroyWindow(data->hNameLabel);     data->hNameLabel = NULL; }
    if (data->hNameBox)       { DestroyWindow(data->hNameBox);       data->hNameBox = NULL; }
    if (data->hProtocolLabel) { DestroyWindow(data->hProtocolLabel); data->hProtocolLabel = NULL; }
    if (data->hProtocolCombo) { DestroyWindow(data->hProtocolCombo); data->hProtocolCombo = NULL; }
    if (data->hDriveLabel)    { DestroyWindow(data->hDriveLabel);    data->hDriveLabel = NULL; }
    if (data->hDriveBox)      { DestroyWindow(data->hDriveBox);      data->hDriveBox = NULL; }
    if (data->hSaveBtn)       { DestroyWindow(data->hSaveBtn);       data->hSaveBtn = NULL; }
    if (data->hCancelBtn)     { DestroyWindow(data->hCancelBtn);     data->hCancelBtn = NULL; }
    if (data->hAdvBtn)        { DestroyWindow(data->hAdvBtn);        data->hAdvBtn = NULL; }
}

/* 创建操作按钮 */
static void CreateActionButtons(ConfigPageData* data) {
    HWND hwnd = data->hwnd;
    HFONT hFont = data->hFont;
    int y = CP_BUTTON_Y;

    data->hSaveBtn = CPCreateButton(TR("STR_CFG_SAVE"), CP_MARGIN_X, y,
        CP_BTN_WIDTH, CP_BTN_HEIGHT, hwnd, hFont, CP_ID_SAVE_BTN);
    data->hCancelBtn = CPCreateButton(TR("STR_CFG_CANCEL"), CP_MARGIN_X + CP_BTN_WIDTH + 10, y,
        CP_BTN_WIDTH, CP_BTN_HEIGHT, hwnd, hFont, CP_ID_CANCEL_BTN);
    data->hAdvBtn = CPCreateButton(TR("STR_CFG_ADV_SETTINGS"), CP_MARGIN_X + (CP_BTN_WIDTH + 10) * 2, y,
        CP_BTN_WIDTH + 40, CP_BTN_HEIGHT, hwnd, hFont, CP_ID_ADV_BTN);
}

/* 显示通用控件和操作按钮 */
static void ShowCommonControls(ConfigPageData* data) {
    ShowWindow(data->hTitleLabel, SW_SHOW);
    ShowWindow(data->hNameLabel, SW_SHOW);
    ShowWindow(data->hNameBox, SW_SHOW);
    ShowWindow(data->hProtocolLabel, SW_SHOW);
    ShowWindow(data->hProtocolCombo, SW_SHOW);
    ShowWindow(data->hDriveLabel, SW_SHOW);
    ShowWindow(data->hDriveBox, SW_SHOW);
    ShowWindow(data->hSaveBtn, SW_SHOW);
    ShowWindow(data->hCancelBtn, SW_SHOW);
    ShowWindow(data->hAdvBtn, SW_SHOW);
}

/* 隐藏通用控件和操作按钮 */
static void HideCommonControls(ConfigPageData* data) {
    ShowWindow(data->hTitleLabel, SW_HIDE);
    ShowWindow(data->hNameLabel, SW_HIDE);
    ShowWindow(data->hNameBox, SW_HIDE);
    ShowWindow(data->hProtocolLabel, SW_HIDE);
    ShowWindow(data->hProtocolCombo, SW_HIDE);
    ShowWindow(data->hDriveLabel, SW_HIDE);
    ShowWindow(data->hDriveBox, SW_HIDE);
    ShowWindow(data->hSaveBtn, SW_HIDE);
    ShowWindow(data->hCancelBtn, SW_HIDE);
    ShowWindow(data->hAdvBtn, SW_HIDE);
}

/* ---- Handler 管理 ---- */

/* 销毁当前 handler 及其控件 */
static void DestroyCurrentHandler(ConfigPageData* data) {
    if (!data->handler) return;
    data->handler->DestroyControls(data->handler, data->hwnd);
    data->handler->Destroy(data->handler);
    free(data->handler);
    data->handler = NULL;
}

/* 创建 handler 并加载配置、创建控件 */
static int CreateCurrentHandler(ConfigPageData* data, ConnectionConfig* connCfg) {
    ProtocolHandler* h = CreateHandlerForProtocol(connCfg->protocol, connCfg, &data->appCfg->global);
    if (!h) return 0;
    data->handler = h;

    /* 保存 protocol，因为 LoadConfig 内部的 SetConnectionDefaults 会将其重置为 "webdav" */
    char savedProtocol[32];
    strcpy_s(savedProtocol, sizeof(savedProtocol), connCfg->protocol);

    h->LoadConfig(h);

    /* 恢复 protocol（LoadConnectionConfig 的 SetConnectionDefaults 会覆盖调用者设置的值） */
    strcpy_s(connCfg->protocol, sizeof(connCfg->protocol), savedProtocol);

    h->CreateMainControls(h, data->hwnd, data->hFont, data->hBoldFont, CP_YOFFSET);
    return 1;
}

/* 协议切换 */
static void SwitchProtocol(ConfigPageData* data) {
    int sel;
    const char* protocols[] = { "webdav", "smb", "sftp", "ftp" };
    char newProto[32];
    ConnectionConfig* cc;

    sel = (int)SendMessageW(data->hProtocolCombo, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel > 3) return;
    strcpy_s(newProto, sizeof(newProto), protocols[sel]);

    /* 协议未变化则跳过 */
    if (data->editMode) {
        int idx = FindConnectionById(data->appCfg, data->editConnId);
        if (idx < 0) return;
        cc = &data->appCfg->connections[idx];
    } else {
        cc = &data->tempConn;
    }
    if (strcmp(cc->protocol, newProto) == 0) return;

    /* 保存当前协议专属数据（通用字段保留在 connCfg 中） */
    if (data->handler) {
        data->handler->SaveMainFromUI(data->handler);
    }

    /* 销毁旧 handler */
    DestroyCurrentHandler(data);

    /* 更新协议 */
    strcpy_s(cc->protocol, sizeof(cc->protocol), newProto);

    /* 创建新 handler */
    if (!CreateCurrentHandler(data, cc)) return;

    /* 显示新 handler 的控件 */
    data->handler->ShowMainControls(data->handler, data->hwnd);
    InvalidateRect(data->hwnd, NULL, TRUE);
}

/* ---- 高级设置页面 ---- */

static void ShowAdvPage(ConfigPageData* data) {
    RECT rc;
    SCROLLINFO si;
    if (!data->handler) return;

    /* 保存主页面数据到 handler 内部，以便返回时恢复 */
    data->handler->SaveMainFromUI(data->handler);

    /* 隐藏主页面控件 */
    HideCommonControls(data);
    data->handler->HideMainControls(data->handler, data->hwnd);

    /* 创建并显示高级设置控件 */
    data->handler->CreateAdvControls(data->handler, data->hwnd,
        data->hFont, data->hBoldFont, data->hDescFont);
    data->handler->ShowAdvControls(data->handler, data->hwnd);

    data->advPageActive = 1;
    data->scrollPos = 0;
    data->contentHeight = data->handler->GetAdvContentHeight(data->handler);

    /* 设置滚动条 */
    ShowScrollBar(data->hwnd, SB_VERT, TRUE);
    GetClientRect(data->hwnd, &rc);
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = data->contentHeight;
    si.nPage = (UINT)(rc.bottom > 0 ? rc.bottom : 1);
    si.nPos = 0;
    SetScrollInfo(data->hwnd, SB_VERT, &si, TRUE);

    SetWindowTextW(data->hwnd, TR("STR_ADV_SETTINGS"));
    InvalidateRect(data->hwnd, NULL, TRUE);
}

static void HideAdvPage(ConfigPageData* data) {
    SCROLLINFO si;
    if (!data->handler) return;

    /* 隐藏并销毁高级设置控件 */
    data->handler->HideAdvControls(data->handler, data->hwnd);
    data->handler->DestroyControls(data->handler, data->hwnd);

    /* 隐藏滚动条 */
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = 0;
    si.nPage = 0;
    si.nPos = 0;
    SetScrollInfo(data->hwnd, SB_VERT, &si, TRUE);
    ShowScrollBar(data->hwnd, SB_VERT, FALSE);

    /* 重新创建主页面控件 */
    data->handler->CreateMainControls(data->handler, data->hwnd,
        data->hFont, data->hBoldFont, CP_YOFFSET);

    /* 显示主页面 */
    ShowCommonControls(data);
    data->handler->ShowMainControls(data->handler, data->hwnd);

    data->advPageActive = 0;
    data->scrollPos = 0;

    SetWindowTextW(data->hwnd, data->editMode ? TR("STR_CFG_EDIT_TITLE") : TR("STR_CFG_ADD_TITLE"));
    InvalidateRect(data->hwnd, NULL, TRUE);
}

/* ---- 保存逻辑 ---- */

static void DoSave(ConfigPageData* data) {
    ConnectionConfig* cc;

    if (!data->handler) return;

    /* 1. 从 UI 读取协议专属数据到 handler 内部 */
    data->handler->SaveMainFromUI(data->handler);

    /* 2. 如果高级设置页面激活，也保存高级设置 */
    if (data->advPageActive) {
        data->handler->SaveAdvSettingsFromUI(data->handler);
    }

    /* 3. 从通用控件读取值到 connCfg */
    if (data->editMode) {
        int idx = FindConnectionById(data->appCfg, data->editConnId);
        if (idx < 0) return;  /* 连接未找到，不应发生 */
        cc = &data->appCfg->connections[idx];
    } else {
        cc = &data->tempConn;
    }

    GetWindowTextA(data->hNameBox, cc->name, sizeof(cc->name));
    GetWindowTextA(data->hDriveBox, cc->drive, sizeof(cc->drive));
    /* 强制盘符为大写字母 */
    if (cc->drive[0] >= 'a' && cc->drive[0] <= 'z') {
        cc->drive[0] = (char)toupper((unsigned char)cc->drive[0]);
    }
    /* protocol 已在 SwitchProtocol 或初始化时设置 */

    if (data->editMode) {
        /* 编辑模式：直接保存 */
        data->handler->SaveConfig(data->handler);
    } else {
        /* 新增模式：添加连接后保存 */
        GenerateConnectionId(data->appCfg, cc->id, sizeof(cc->id));
        data->appCfg->next_conn_id++;  /* 递增以备下次使用 */
        AddConnection(data->appCfg, cc);
        /* tempConn 仍持有正确的 id，handler->SaveConfig 可正常写入 INI */
        data->handler->SaveConfig(data->handler);
        /* 保存全局配置（含 next_conn_id） */
        SaveAppConfig(data->appCfg);
    }

    /* 回调通知保存完成 */
    if (data->callbacks.OnSave) {
        data->callbacks.OnSave(data->callbacks.ctx);
    }
}

/* ---- 公共接口 ---- */

void ConfigPage_Create(ConfigPageData* data, HWND hwnd, AppConfig* appCfg,
                       char* rclonePath, HFONT hFont, HFONT hBoldFont, HFONT hDescFont,
                       ConfigPageCallbacks callbacks) {
    memset(data, 0, sizeof(ConfigPageData));
    data->hwnd = hwnd;
    data->appCfg = appCfg;
    data->rclonePath = rclonePath;
    data->hFont = hFont;
    data->hBoldFont = hBoldFont;
    data->hDescFont = hDescFont;
    data->callbacks = callbacks;
}

void ConfigPage_Destroy(ConfigPageData* data) {
    if (data->advPageActive && data->handler) {
        data->handler->HideAdvControls(data->handler, data->hwnd);
    }
    DestroyCurrentHandler(data);
    DestroyCommonControls(data);
    memset(data, 0, sizeof(ConfigPageData));
}

void ConfigPage_ShowForAdd(ConfigPageData* data) {
    ConnectionConfig* cc;

    /* 清理旧状态 */
    DestroyCurrentHandler(data);
    DestroyCommonControls(data);

    data->editMode = 0;
    data->editConnId[0] = '\0';
    data->advPageActive = 0;
    data->scrollPos = 0;

    /* 初始化临时连接配置 */
    cc = &data->tempConn;
    memset(cc, 0, sizeof(ConnectionConfig));
    strcpy_s(cc->name, sizeof(cc->name), "New Connection");
    strcpy_s(cc->protocol, sizeof(cc->protocol), "webdav");
    strcpy_s(cc->drive, sizeof(cc->drive), "Z");
    strcpy_s(cc->volname, sizeof(cc->volname), "Network_Disk");
    cc->vfs_cache_mode = 3;
    strcpy_s(cc->dir_cache_time, sizeof(cc->dir_cache_time), "24h");
    strcpy_s(cc->buffer_size, sizeof(cc->buffer_size), "64M");
    cc->transfers = 4;
    strcpy_s(cc->vfs_cache_max_age, sizeof(cc->vfs_cache_max_age), "24h");
    strcpy_s(cc->vfs_read_chunk_size, sizeof(cc->vfs_read_chunk_size), "128M");
    strcpy_s(cc->vfs_read_chunk_size_limit, sizeof(cc->vfs_read_chunk_size_limit), "off");
    strcpy_s(cc->vfs_cache_max_size, sizeof(cc->vfs_cache_max_size), "15G");

    /* 创建通用控件 */
    data->hTitleLabel = CPCreateBoldLabel(TR("STR_CFG_ADD_TITLE"), CP_MARGIN_X, 15,
        400, 30, data->hwnd, data->hBoldFont);
    CreateCommonControls(data);
    CreateActionButtons(data);

    /* 设置通用控件值 */
    SetWindowTextA(data->hNameBox, cc->name);
    SetWindowTextA(data->hDriveBox, cc->drive);
    SendMessageW(data->hProtocolCombo, CB_SETCURSEL, 0, 0);  /* WebDAV */

    /* 创建 handler */
    if (!CreateCurrentHandler(data, cc)) {
        /* handler 创建失败，回退到列表页 */
        if (data->callbacks.OnCancel) data->callbacks.OnCancel(data->callbacks.ctx);
        return;
    }
    data->handler->ShowMainControls(data->handler, data->hwnd);

    /* 禁用编辑模式下的协议切换 — 新增模式允许切换 */
    EnableWindow(data->hProtocolCombo, TRUE);

    SetWindowTextW(data->hwnd, TR("STR_CFG_ADD_TITLE"));
    InvalidateRect(data->hwnd, NULL, TRUE);
}

void ConfigPage_ShowForEdit(ConfigPageData* data, const char* connId) {
    ConnectionConfig* cc;
    int idx, protoSel;
    const char* protocols[] = { "webdav", "smb", "sftp", "ftp" };

    /* 清理旧状态 */
    DestroyCurrentHandler(data);
    DestroyCommonControls(data);

    data->editMode = 1;
    strcpy_s(data->editConnId, sizeof(data->editConnId), connId);
    data->advPageActive = 0;
    data->scrollPos = 0;

    /* 查找连接 */
    idx = FindConnectionById(data->appCfg, connId);
    if (idx < 0) {
        /* 连接未找到，回退到列表页 */
        if (data->callbacks.OnCancel) data->callbacks.OnCancel(data->callbacks.ctx);
        return;
    }
    cc = &data->appCfg->connections[idx];

    /* 创建通用控件 */
    data->hTitleLabel = CPCreateBoldLabel(TR("STR_CFG_EDIT_TITLE"), CP_MARGIN_X, 15,
        400, 30, data->hwnd, data->hBoldFont);
    CreateCommonControls(data);
    CreateActionButtons(data);

    /* 设置通用控件值 */
    SetWindowTextA(data->hNameBox, cc->name);
    SetWindowTextA(data->hDriveBox, cc->drive);

    /* 设置协议下拉框 */
    protoSel = 0;
    {
        int i;
        for (i = 0; i < 4; i++) {
            if (strcmp(cc->protocol, protocols[i]) == 0) { protoSel = i; break; }
        }
    }
    SendMessageW(data->hProtocolCombo, CB_SETCURSEL, protoSel, 0);

    /* 编辑模式下禁止切换协议 */
    EnableWindow(data->hProtocolCombo, FALSE);

    /* 创建 handler */
    if (!CreateCurrentHandler(data, cc)) {
        /* handler 创建失败，回退到列表页 */
        if (data->callbacks.OnCancel) data->callbacks.OnCancel(data->callbacks.ctx);
        return;
    }
    data->handler->ShowMainControls(data->handler, data->hwnd);

    SetWindowTextW(data->hwnd, TR("STR_CFG_EDIT_TITLE"));
    InvalidateRect(data->hwnd, NULL, TRUE);
}

void ConfigPage_Hide(ConfigPageData* data) {
    if (data->advPageActive && data->handler) {
        data->handler->HideAdvControls(data->handler, data->hwnd);
        /* 清零并隐藏高级设置滚动条 */
        SCROLLINFO si;
        memset(&si, 0, sizeof(si));
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        SetScrollInfo(data->hwnd, SB_VERT, &si, TRUE);
        ShowScrollBar(data->hwnd, SB_VERT, FALSE);
    }
    if (data->handler) {
        data->handler->HideMainControls(data->handler, data->hwnd);
    }
    HideCommonControls(data);
    data->advPageActive = 0;
}

/* ---- 消息处理 ---- */

int ConfigPage_HandleCommand(ConfigPageData* data, WPARAM wParam, LPARAM lParam) {
    int ctrlId = LOWORD(wParam);
    int notifyCode = HIWORD(wParam);

    /* 高级设置页面激活时，拦截 Back/Save 按钮 */
    if (data->advPageActive) {
        if (IsAdvSaveId(ctrlId)) {
            /* 高级设置 Save：保存数据并返回主页面 */
            if (data->handler) data->handler->SaveAdvSettingsFromUI(data->handler);
            HideAdvPage(data);
            return 1;
        }
        if (IsAdvBackId(ctrlId)) {
            /* 高级设置 Back：不保存，直接返回主页面 */
            HideAdvPage(data);
            return 1;
        }
        /* 其他高级设置事件委托给 handler */
        if (data->handler) {
            return data->handler->HandleCommand(data->handler, data->hwnd, wParam, lParam);
        }
        return 0;
    }

    /* 主页面控件 */
    switch (ctrlId) {
    case CP_ID_SAVE_BTN:
        DoSave(data);
        return 1;

    case CP_ID_CANCEL_BTN:
        if (data->callbacks.OnCancel) {
            data->callbacks.OnCancel(data->callbacks.ctx);
        }
        return 1;

    case CP_ID_ADV_BTN:
        ShowAdvPage(data);
        return 1;

    case CP_ID_PROTOCOL_COMBO:
        if (notifyCode == CBN_SELCHANGE) {
            SwitchProtocol(data);
            return 1;
        }
        break;
    }

    /* 委托给 handler 处理协议专属控件事件 */
    if (data->handler) {
        return data->handler->HandleCommand(data->handler, data->hwnd, wParam, lParam);
    }

    return 0;
}

void ConfigPage_HandleScroll(ConfigPageData* data, int delta) {
    int newY, maxScroll;
    SCROLLINFO si;
    RECT rc;
    if (!data->advPageActive || !data->handler) return;

    newY = data->scrollPos + delta;
    GetClientRect(data->hwnd, &rc);
    maxScroll = data->contentHeight - (rc.bottom > 0 ? (int)rc.bottom : 400);
    if (maxScroll < 0) maxScroll = 0;
    if (newY < 0) newY = 0;
    if (newY > maxScroll) newY = maxScroll;

    if (newY != data->scrollPos) {
        int actualDelta = newY - data->scrollPos;
        data->scrollPos = newY;
        /* 同步滚动条滑块位置 */
        memset(&si, 0, sizeof(si));
        si.cbSize = sizeof(si);
        si.fMask = SIF_POS;
        si.nPos = newY;
        SetScrollInfo(data->hwnd, SB_VERT, &si, TRUE);
        /* 滚动内容并更新控件位置 */
        ScrollWindowEx(data->hwnd, 0, -actualDelta, NULL, NULL, NULL, NULL,
            SW_INVALIDATE | SW_ERASE);
        data->handler->UpdateAdvPositions(data->handler, data->scrollPos);
    }
}

void ConfigPage_HandleMouseWheel(ConfigPageData* data, int delta) {
    ConfigPage_HandleScroll(data, -(delta / WHEEL_DELTA * 30));
}

void ConfigPage_HandleEraseBkgnd(ConfigPageData* data, HDC hdc) {
    RECT rc;
    GetClientRect(data->hwnd, &rc);
    FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
}

LRESULT ConfigPage_HandleCtlColor(ConfigPageData* data, HWND hCtrl, HDC hdc) {
    /* 高级设置页面激活时委托给 handler */
    if (data->advPageActive && data->handler) {
        return data->handler->HandleCtlColor(data->handler, hCtrl, hdc);
    }
    /* 主页面使用灰色背景匹配 HandleEraseBkgnd */
    SetBkMode(hdc, TRANSPARENT);
    return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
}

void ConfigPage_ShowAdv(ConfigPageData* data) {
    /* 如果尚未显示配置页，则无效 */
    if (!data->handler) return;
    /* 如果高级设置页面已激活，则无需操作 */
    if (data->advPageActive) return;
    ShowAdvPage(data);
}