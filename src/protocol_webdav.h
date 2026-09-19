#pragma once

#include "protocol.h"

/* ======================================================================
   WebDAV 协议处理器 — 实现 ProtocolHandler 接口
   ====================================================================== */

/* WebDAV 专属配置 */
typedef struct {
    char host[512];
    char port[32];
    char path[256];
    char user[256];
    char pass[256];
    int  ssl;
    int  vfs_cache_mode;   /* 0=off, 1=minimal, 2=writes, 3=full */
    char dir_cache_time[32];
    char buffer_size[32];
    int  transfers;
    char cache_dir[MAX_PATH];
    char vfs_cache_max_age[32];
    char vfs_read_chunk_size[32];
    char vfs_read_chunk_size_limit[32];
    char vfs_cache_max_size[32];
} WebDavConfig;

/* WebDAV 协议私有数据（控件句柄 + 配置） */
typedef struct {
    WebDavConfig  cfg;
    CommonConfig* commonCfg;   /* 指向 main.c 中的通用配置，不拥有 */

    /* 主页面控件 */
    HWND hHostBox;
    HWND hPortBox;
    HWND hPathBox;
    HWND hSslCheck;
    HWND hUserBox;
    HWND hPassBox;
    HWND hMainLabels[6];

    /* 高级设置控件 */
    HWND hAdvLabels[10];
    HWND hAdvEdits[9];
    HWND hAdvComboVfs;
    HWND hAdvBtnBrowse;
    HWND hAdvBtnBack;
    HWND hAdvBtnSave;
    HWND hAdvBtnReset;
    HWND hAdvBtnClearCache;
    HWND hAdvDescLabels[10];
    HWND hAdvParamLabels[10];
    HFONT hAdvDescFont;
} WebDavData;

/* 创建 WebDAV 协议处理器（调用者负责调用 Destroy 释放） */
ProtocolHandler* CreateWebDavHandler(CommonConfig* commonCfg);