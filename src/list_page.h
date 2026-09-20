#pragma once

#include <windows.h>
#include "protocol.h"
#include "config.h"

/* ======================================================================
   列表页模块 — 显示所有已配置连接的管理列表
   ====================================================================== */

/* 页面模式回调：通知 main.c 切换到配置页（新增/编辑）或高级设置 */
typedef struct {
    /* 请求切换到配置页（新增连接） */
    void (*OnAddConnection)(void* ctx);
    /* 请求切换到配置页（编辑指定连接） */
    void (*OnEditConnection)(void* ctx, const char* connId);
    /* 请求切换到高级设置页（指定连接） */
    void (*OnAdvSettings)(void* ctx, const char* connId);
    /* 请求切换到全局高级设置页 */
    void (*OnGlobalAdvSettings)(void* ctx);
    /* 通知：应用需要退出 */
    void (*OnExit)(void* ctx);
    /* 通知：隐藏窗口 */
    void (*OnHide)(void* ctx);
    void* ctx;  /* 回调上下文，通常指向 main.c 的全局状态 */
} ListPageCallbacks;

/* 列表页状态 */
typedef struct {
    HWND        hwnd;           /* 主窗口句柄 */
    AppConfig*  appCfg;         /* 应用配置（连接列表） */
    char*       rclonePath;     /* rclone 路径 */
    HFONT       hFont;          /* 普通字体 */
    HFONT       hBoldFont;      /* 粗体字体 */

    /* 列表页控件 */
    HWND hTitleLabel;           /* 标题 "连接管理" */
    HWND hAddBtn;               /* "添加连接" 按钮 */
    HWND hMountAllBtn;          /* "全部挂载" 按钮 */
    HWND hUnmountAllBtn;        /* "全部卸载" 按钮 */
    HWND hHideBtn;              /* "隐藏运行" 按钮 */
    HWND hExitBtn;              /* "退出" 按钮 */
    HWND hGlobalAdvBtn;         /* "高级设置" 按钮（全局） */

    /* 每个连接行的控件数组（动态创建/销毁） */
    struct {
        char  connId[64];       /* 连接 ID */
        HWND  hNameLabel;       /* 连接名称标签 */
        HWND  hProtocolLabel;   /* 协议标签 */
        HWND  hDriveLabel;      /* 盘符标签 */
        HWND  hMountBtn;        /* 挂载/卸载按钮 */
        HWND  hAdvBtn;          /* 高级设置按钮 */
        HWND  hEditBtn;         /* 编辑按钮 */
        HWND  hDeleteBtn;       /* 删除按钮 */
        HWND  hStatusIcon;      /* 状态图标（挂载中/未挂载） */
    } rows[MAX_CONNECTIONS];

    int   rowCount;             /* 当前行数 */
    int   scrollPos;            /* 滚动位置 */
    int   contentHeight;        /* 内容总高度 */

    ListPageCallbacks callbacks; /* 页面回调 */
} ListPageData;

/* ---- 列表页生命周期 ---- */

/* 创建列表页（在 WM_CREATE 时调用） */
void ListPage_Create(ListPageData* data, HWND hwnd, AppConfig* appCfg,
                     char* rclonePath, HFONT hFont, HFONT hBoldFont,
                     ListPageCallbacks callbacks);

/* 销毁列表页控件（在 WM_DESTROY 时调用） */
void ListPage_Destroy(ListPageData* data);

/* ---- 列表页显示/隐藏 ---- */

/* 显示列表页（从其他页面切换回来时调用） */
void ListPage_Show(ListPageData* data);

/* 隐藏列表页（切换到配置页等时调用） */
void ListPage_Hide(ListPageData* data);

/* ---- 列表页数据刷新 ---- */

/* 刷新连接列表（增删改后调用，重建所有行控件） */
void ListPage_Refresh(ListPageData* data);

/* 更新指定连接的挂载状态显示 */
void ListPage_UpdateMountStatus(ListPageData* data, const char* connId);

/* ---- 列表页消息处理 ---- */

/* 处理 WM_COMMAND（按钮点击等），返回 1=已处理, 0=未处理 */
int  ListPage_HandleCommand(ListPageData* data, WPARAM wParam, LPARAM lParam);

/* 处理 WM_VSCROLL / WM_MOUSEWHEEL（列表滚动） */
void ListPage_HandleScroll(ListPageData* data, int delta);
void ListPage_HandleMouseWheel(ListPageData* data, int delta);

/* 处理 WM_ERASEBKGND */
void ListPage_HandleEraseBkgnd(ListPageData* data, HDC hdc);

/* 处理 WM_CTLCOLORSTATIC */
LRESULT ListPage_HandleCtlColor(ListPageData* data, HWND hCtrl, HDC hdc);