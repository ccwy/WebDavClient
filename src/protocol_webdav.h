#pragma once

#include "protocol.h"

/* ======================================================================
   WebDAV 协议处理器 — 实现 ProtocolHandler 接口
   ====================================================================== */

/* WebDAV 专属配置（仅包含 rclone WebDAV 后端专属参数，VFS 通用参数已移至 ConnectionConfig） */
typedef struct {
    char host[512];          /* 主机地址（构建 --webdav-url 的一部分） */
    char port[32];           /* 端口（构建 --webdav-url 的一部分） */
    char path[256];          /* 路径（构建 --webdav-url 的一部分） */
    char user[256];          /* --webdav-user */
    char pass[256];          /* --webdav-pass（需混淆） */
    int  ssl;                /* 启用 HTTPS（构建 --webdav-url 的 scheme） */
    char vendor[32];         /* --webdav-vendor: other/nextcloud/owncloud/infinitescale/sharepoint/sharepoint-ntlm/rclone/fastmail */
    char headers[512];       /* --webdav-headers: 自定义 HTTP 头，逗号分隔的 key,value 对 */
    int  no_check_cert;      /* --no-check-certificate: 跳过 TLS 证书验证（自签名证书时使用） */
} WebDavConfig;

/* WebDAV 协议私有数据（控件句柄 + 配置） */
typedef struct {
    WebDavConfig  cfg;
    ConnectionConfig* connCfg;   /* 指向当前连接的通用配置，不拥有 */
    GlobalConfig*     globalCfg; /* 指向全局配置（debug_log 等），不拥有 */

    /* 主页面控件 */
    HWND hHostBox;
    HWND hPortBox;
    HWND hPathBox;
    HWND hSslCheck;
    HWND hUserBox;
    HWND hPassBox;
    HWND hMainLabels[5];

    /* 高级设置控件（10行通用VFS + 3行WebDAV专属 = 13行） */
    HWND hAdvLabels[13];
    HWND hAdvEdits[10];        /* [0-8]通用VFS编辑框, [9]headers */
    HWND hAdvComboVfs;         /* Row 0: vfs-cache-mode 下拉框 */
    HWND hAdvComboVendor;      /* Row 10: vendor 下拉框 */
    HWND hAdvCheckNoCert;      /* Row 12: no-check-certificate 复选框 */
    HWND hAdvBtnBrowse;
    HWND hAdvBtnBack;
    HWND hAdvBtnSave;
    HWND hAdvBtnReset;
    HWND hAdvBtnClearCache;
    HWND hAdvDescLabels[13];
    HWND hAdvParamLabels[13];
    HFONT hAdvDescFont;
} WebDavData;

/* 创建 WebDAV 协议处理器（调用者负责调用 Destroy 释放） */
ProtocolHandler* CreateWebDavHandler(ConnectionConfig* connCfg, GlobalConfig* globalCfg);