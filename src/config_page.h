#pragma once

#include <windows.h>
#include "protocol.h"
#include "config.h"

/* ======================================================================
   配置页模块 — 新增/编辑连接的配置界面
   ====================================================================== */

/* 配置页回调：通知 main.c 返回列表页 */
typedef struct {
    void (*OnSave)(void* ctx);      /* 保存成功后回调（列表页刷新） */
    void (*OnCancel)(void* ctx);    /* 取消编辑回调（返回列表页） */
    void* ctx;                      /* 回调上下文，通常指向 main.c 的全局状态 */
} ConfigPageCallbacks;

/* 配置页状态 */
typedef struct {
    HWND        hwnd;           /* 主窗口句柄 */
    AppConfig*  appCfg;         /* 应用配置（连接列表） */
    char*       rclonePath;     /* rclone 路径 */
    HFONT       hFont;          /* 普通字体 */
    HFONT       hBoldFont;      /* 粗体字体 */
    HFONT       hDescFont;      /* 高级设置描述字体 */

    ProtocolHandler* handler;   /* 当前协议处理器 */
    ConnectionConfig tempConn;  /* 临时连接配置（新增模式使用） */
    int         editMode;       /* 0=新增, 1=编辑 */
    char        editConnId[64]; /* 编辑模式下的连接ID */

    /* 通用控件（所有协议共享） */
    HWND hTitleLabel;           /* 标题 "新增连接" / "编辑连接" */
    HWND hNameLabel, hNameBox;  /* 名称 */
    HWND hProtocolLabel, hProtocolCombo; /* 协议选择 */
    HWND hDriveLabel, hDriveBox;         /* 盘符 */

    /* 操作按钮 */
    HWND hSaveBtn, hCancelBtn, hAdvBtn;

    /* 高级设置页面状态 */
    int  advPageActive;         /* 高级设置页面是否激活 */
    int  scrollPos;             /* 高级设置页面滚动位置 */
    int  contentHeight;         /* 高级设置内容高度 */

    ConfigPageCallbacks callbacks;
} ConfigPageData;

/* 生命周期 */
void ConfigPage_Create(ConfigPageData* data, HWND hwnd, AppConfig* appCfg,
                       char* rclonePath, HFONT hFont, HFONT hBoldFont, HFONT hDescFont,
                       ConfigPageCallbacks callbacks);
void ConfigPage_Destroy(ConfigPageData* data);

/* 显示/隐藏 */
void ConfigPage_ShowForAdd(ConfigPageData* data);
void ConfigPage_ShowForEdit(ConfigPageData* data, const char* connId);
void ConfigPage_Hide(ConfigPageData* data);
void ConfigPage_ShowAdv(ConfigPageData* data);

/* 消息处理 */
int  ConfigPage_HandleCommand(ConfigPageData* data, WPARAM wParam, LPARAM lParam);
void ConfigPage_HandleScroll(ConfigPageData* data, int delta);
void ConfigPage_HandleMouseWheel(ConfigPageData* data, int delta);
void ConfigPage_HandleEraseBkgnd(ConfigPageData* data, HDC hdc);
LRESULT ConfigPage_HandleCtlColor(ConfigPageData* data, HWND hCtrl, HDC hdc);