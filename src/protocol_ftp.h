#pragma once
#include "protocol.h"

/* FTP 专属配置（仅包含 rclone FTP 后端专属参数，VFS 通用参数已移至 CommonConfig）
   参数来源: https://rclone.org/ftp/ */
typedef struct {
    char host[512];           /* --ftp-host */
    char port[32];            /* --ftp-port, 默认 21 */
    char user[256];           /* --ftp-user */
    char pass[256];           /* --ftp-pass（需混淆） */
    int  tls;                 /* --ftp-tls, 启用隐式 TLS */
    int  explicit_tls;        /* --ftp-explicit-tls, 启用显式 TLS */
    int  no_check_certificate;/* --ftp-no-check-certificate, 默认 0 */
    char idle_timeout[32];    /* --ftp-idle-timeout, 默认 1m0s */
    char concurrency[16];     /* --ftp-concurrency, 默认 0 */
} FtpConfig;

/* FTP 协议私有数据 */
typedef struct {
    FtpConfig      cfg;
    CommonConfig*  commonCfg;
    HWND hHostBox, hPortBox, hUserBox, hPassBox;
    HWND hTlsCheck, hExplicitTlsCheck;
    HWND hMainLabels[6];      /* 5个协议标签 + Drive标签 */
    HWND hAdvLabels[13];      /* 3行FTP专属 + 10行通用VFS */
    HWND hAdvEdits[11];       /* [0-1] FTP: idle_timeout, concurrency; [2-10] VFS */
    HWND hAdvChecks[1];       /* no_check_certificate */
    HWND hAdvComboVfs;
    HWND hAdvParamLabels[13];
    HWND hAdvDescLabels[13];
    HWND hAdvBtnBrowse, hAdvBtnBack, hAdvBtnSave, hAdvBtnReset, hAdvBtnClearCache;
    HFONT hAdvDescFont;
} FtpData;

ProtocolHandler* CreateFtpHandler(CommonConfig* commonCfg);