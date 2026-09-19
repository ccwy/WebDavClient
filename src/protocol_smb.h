#pragma once
#include "protocol.h"

/* ======================================================================
   SMB 协议处理器 — 实现 ProtocolHandler 接口
   参数来源: https://rclone.org/smb/
   ====================================================================== */

/* SMB 专属配置（仅包含 rclone SMB 后端专属参数，VFS 通用参数已移至 CommonConfig） */
typedef struct {
    char server[512];          /* --smb-host: SMB 服务器地址 */
    char port[32];             /* --smb-port: SMB 端口，默认 445 */
    char share[256];           /* 共享名称（rclone 路径 :smb:sharename 的一部分） */
    char user[256];            /* --smb-user: 用户名 */
    char pass[256];            /* --smb-pass: 密码（需混淆） */
    char domain[128];          /* --smb-domain: NTLM 认证域名，默认 WORKGROUP */
    char spn[256];             /* --smb-spn: 服务主体名称，可选 */
    int  use_kerberos;         /* --smb-use-kerberos: 使用 Kerberos 认证，默认 0 */
    char idle_timeout[32];     /* --smb-idle-timeout: 空闲连接超时，默认 1m0s */
    int  hide_special_share;   /* --smb-hide-special-share: 隐藏特殊共享，默认 1 */
    int  case_insensitive;     /* --smb-case-insensitive: 大小写不敏感，默认 1 */
} SmbConfig;

/* SMB 协议私有数据 */
typedef struct {
    SmbConfig     cfg;
    CommonConfig* commonCfg;
    /* 主页面控件 (5 个协议字段 + 1 个通用 Drive 标签) */
    HWND hServerBox, hPortBox, hShareBox, hUserBox, hPassBox;
    HWND hMainLabels[6];       /* Server, Port, Share, User, Pass, Drive */
    /* 高级设置控件 (6行SMB专属 + 10行通用VFS/Mount = 16行) */
    HWND hAdvLabels[16];
    HWND hAdvEdits[12];        /* [0-2] SMB: domain, spn, idle_timeout; [3-11] VFS: dir_cache_time, buffer_size, transfers, cache_dir, vfs_cache_max_age, vfs_read_chunk_size, vfs_read_chunk_size_limit, volname, vfs_cache_max_size */
    HWND hAdvChecks[3];        /* use_kerberos, hide_special_share, case_insensitive */
    HWND hAdvComboVfs;         /* vfs-cache-mode 下拉框 */
    HWND hAdvParamLabels[16];
    HWND hAdvDescLabels[16];
    HWND hAdvBtnBrowse;        /* cache-dir 浏览按钮 */
    HWND hAdvBtnBack, hAdvBtnSave, hAdvBtnReset, hAdvBtnClearCache;
    HFONT hAdvDescFont;
} SmbData;

ProtocolHandler* CreateSmbHandler(CommonConfig* commonCfg);