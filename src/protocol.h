#pragma once

#include <windows.h>

/* ======================================================================
   协议处理器接口 — 为 WebDAV、SMB 等不同协议提供统一抽象层
   ====================================================================== */

/* 通用配置（跨协议共享） */
typedef struct {
    char drive[16];       /* 盘符，如 "Z" */
    char protocol[32];    /* 协议名称: "webdav", "smb", ... */
    char volname[64];     /* 磁盘卷标名称 */
    int  auto_start;      /* 开机自启 */
    int  debug_log;       /* 调试日志 */
    int  auto_hide;       /* 挂载后自动隐藏 */
} CommonConfig;

/* 协议处理器 — 每种协议实现此接口 */
typedef struct ProtocolHandler {

    /* ---- 标识 ---- */
    const char* name;              /* 协议名称: "webdav", "smb", ... */
    void*       data;              /* 协议私有数据（ opaque ） */

    /* ---- 主页面 UI ---- */
    void (*CreateMainControls)(struct ProtocolHandler* self,
                               HWND hwnd, HFONT hFont, HFONT hBoldFont);
    void (*ShowMainControls)(struct ProtocolHandler* self, HWND hwnd);
    void (*HideMainControls)(struct ProtocolHandler* self, HWND hwnd);

    /* ---- 高级设置页面 UI ---- */
    void (*CreateAdvControls)(struct ProtocolHandler* self,
                              HWND hwnd, HFONT hFont,
                              HFONT hBoldFont, HFONT hDescFont);
    void (*ShowAdvControls)(struct ProtocolHandler* self, HWND hwnd);
    void (*HideAdvControls)(struct ProtocolHandler* self, HWND hwnd);
    void (*DestroyControls)(struct ProtocolHandler* self, HWND hwnd);
    void (*UpdateAdvPositions)(struct ProtocolHandler* self, int scrollPos);
    int  (*GetAdvContentHeight)(struct ProtocolHandler* self);
    LRESULT (*HandleCtlColor)(struct ProtocolHandler* self,
                              HWND hCtrl, HDC hdc);

    /* ---- 命令处理 ---- */
    int  (*HandleCommand)(struct ProtocolHandler* self,
                          HWND hwnd, WPARAM wParam, LPARAM lParam);

    /* ---- 配置 ---- */
    void (*LoadConfig)(struct ProtocolHandler* self);
    void (*SaveConfig)(struct ProtocolHandler* self);
    void (*SaveAdvSettingsFromUI)(struct ProtocolHandler* self);
    void (*ResetAdvSettings)(struct ProtocolHandler* self);

    /* ---- 挂载 ---- */
    int  (*ExecuteMount)(struct ProtocolHandler* self,
                         HWND hwnd, const char* rclonePath, int isAuto);

    /* ---- 清理 ---- */
    void (*Destroy)(struct ProtocolHandler* self);

} ProtocolHandler;