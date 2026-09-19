#pragma once
#include "protocol.h"

/* SMB 专属配置（volname 已移至 CommonConfig） */
typedef struct {
    char server[512];    /* SMB 服务器地址 */
    char port[32];       /* SMB 端口，默认 445 */
    char share[256];     /* SMB 共享名称，如 \\server\share */
    char user[256];      /* 用户名 */
    char pass[256];      /* 密码 */
    int  vfs_cache_mode; /* VFS 缓存模式: 0=off, 1=minimal, 2=writes, 3=full */
    char dir_cache_time[32];
    char buffer_size[32];
    int  transfers;
    char cache_dir[MAX_PATH];
    char vfs_cache_max_age[32];
    char vfs_read_chunk_size[32];
    char vfs_read_chunk_size_limit[32];
    char vfs_cache_max_size[32];
} SmbConfig;

/* SMB 协议私有数据 */
typedef struct {
    SmbConfig     cfg;
    CommonConfig* commonCfg;
    HWND hServerBox, hPortBox, hShareBox, hUserBox, hPassBox;
    HWND hMainLabels[5];
    HWND hAdvLabels[10], hAdvEdits[9], hAdvComboVfs;
    HWND hAdvBtnBrowse, hAdvBtnBack, hAdvBtnSave, hAdvBtnReset, hAdvBtnClearCache;
    HWND hAdvDescLabels[10], hAdvParamLabels[10];
    HFONT hAdvDescFont;
} SmbData;

ProtocolHandler* CreateSmbHandler(CommonConfig* commonCfg);