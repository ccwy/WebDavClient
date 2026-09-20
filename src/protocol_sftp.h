#pragma once
#include "protocol.h"

/* SFTP 专属配置（仅包含 rclone SFTP 后端专属参数，VFS 通用参数已移至 ConnectionConfig）
   参数来源: https://rclone.org/sftp/ */
typedef struct {
    char host[512];           /* --sftp-host */
    char port[32];            /* --sftp-port, 默认 22 */
    char user[256];           /* --sftp-user */
    char pass[256];           /* --sftp-pass（需混淆） */
    char key_file[MAX_PATH];  /* --sftp-key-file, SSH 私钥文件路径 */
    char key_file_pass[256];  /* --sftp-key-file-pass（需混淆） */
    char shell_type[32];      /* --sftp-shell-type: "", "unix", "powershell", "cmd" */
    char idle_timeout[32];    /* --sftp-idle-timeout, 默认 1m0s */
    int  use_insecure_cipher; /* --sftp-use-insecure-cipher, 默认 0 */
    int  disable_hashcheck;   /* --sftp-disable-hashcheck, 默认 0 */
    int  set_modtime;         /* --sftp-set-modtime, 默认 0 */
    int  skip_links;          /* --sftp-skip-links, 默认 0 */
} SftpConfig;

/* SFTP 协议私有数据 */
typedef struct {
    SftpConfig     cfg;
    ConnectionConfig* connCfg;   /* 指向当前连接的通用配置，不拥有 */
    GlobalConfig*     globalCfg; /* 指向全局配置（debug_log 等），不拥有 */
    HWND hHostBox, hPortBox, hUserBox, hPassBox, hKeyFileBox;
    HWND hMainLabels[5];      /* 5个协议标签 + Drive标签 */
    HWND hAdvLabels[17];      /* 7行SFTP专属 + 10行通用VFS */
    HWND hAdvEdits[11];       /* [0-1] SFTP: key_file_pass, idle_timeout; [2-10] VFS */
    HWND hAdvChecks[4];       /* use_insecure_cipher, disable_hashcheck, set_modtime, skip_links */
    HWND hAdvComboVfs;
    HWND hAdvComboShell;
    HWND hAdvParamLabels[17];
    HWND hAdvDescLabels[17];
    HWND hAdvBtnBrowse, hAdvBtnBack, hAdvBtnSave, hAdvBtnReset, hAdvBtnClearCache;
    HFONT hAdvDescFont;
} SftpData;

ProtocolHandler* CreateSftpHandler(ConnectionConfig* connCfg, GlobalConfig* globalCfg);