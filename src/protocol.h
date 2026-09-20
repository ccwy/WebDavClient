#pragma once

#include <windows.h>

/* ======================================================================
   数据结构定义 — 批量管理模式
   ====================================================================== */

/* 全局配置（应用级设置，所有连接共享） */
typedef struct {
    int  auto_start;      /* 开机自启 */
    int  debug_log;       /* 调试日志 */
    int  auto_hide;       /* 挂载后自动隐藏 */
} GlobalConfig;

/* 连接通用配置（每条连接独立） */
typedef struct {
    char id[64];          /* 唯一标识，如 "conn_1"，对应 INI [section] 名 */
    char name[128];       /* 显示名称 */
    char protocol[32];    /* 协议名称: "webdav", "smb", "sftp", "ftp" */
    char drive[16];       /* 盘符，如 "Z" */
    char volname[64];     /* 磁盘卷标名称 --volname */

    /* rclone 通用 mount/VFS 参数（适用于所有协议） */
    int  vfs_cache_mode;  /* --vfs-cache-mode: 0=off, 1=minimal, 2=writes, 3=full */
    char dir_cache_time[32];      /* --dir-cache-time, 如 "24h" */
    char buffer_size[32];         /* --buffer-size, 如 "64M" */
    int  transfers;               /* --transfers, 如 4 */
    char cache_dir[MAX_PATH];     /* --cache-dir, 缓存文件存放目录 */
    char vfs_cache_max_age[32];   /* --vfs-cache-max-age, 如 "24h" */
    char vfs_read_chunk_size[32]; /* --vfs-read-chunk-size, 如 "128M" */
    char vfs_read_chunk_size_limit[32]; /* --vfs-read-chunk-size-limit, 如 "off" */
    char vfs_cache_max_size[32];  /* --vfs-cache-max-size, 如 "15G" */
} ConnectionConfig;

/* 最大连接数（A-Z 盘符最多 26 个） */
#define MAX_CONNECTIONS 26

/* 应用完整配置 */
typedef struct {
    GlobalConfig global;
    ConnectionConfig connections[MAX_CONNECTIONS];
    int count;
    int next_conn_id;     /* 下一个连接 ID 的序号，用于生成 "conn_N" */
} AppConfig;

/* ======================================================================
   协议处理器接口 — 为 WebDAV、SMB 等不同协议提供统一抽象层
   ====================================================================== */

/* 协议处理器 — 每种协议实现此接口 */
typedef struct ProtocolHandler {

    /* ---- 标识 ---- */
    const char* name;              /* 协议名称: "webdav", "smb", ... */
    void*       data;              /* 协议私有数据（ opaque ） */

    /* ---- 主页面 UI（编辑/新增连接页面） ---- */
    void (*CreateMainControls)(struct ProtocolHandler* self,
                               HWND hwnd, HFONT hFont, HFONT hBoldFont, int yOffset);
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
    void (*SaveMainFromUI)(struct ProtocolHandler* self);      /* 从主页面 UI 控件读取值到内部数据 */
    void (*SaveAdvSettingsFromUI)(struct ProtocolHandler* self);
    void (*ResetAdvSettings)(struct ProtocolHandler* self);

    /* ---- 挂载 ---- */
    int  (*ExecuteMount)(struct ProtocolHandler* self,
                         HWND hwnd, const char* rclonePath, int isAuto);
    /* 从已保存的配置直接挂载（不读取 UI 控件，用于列表页快速挂载） */
    int  (*ExecuteMountFromConfig)(struct ProtocolHandler* self,
                                   const char* rclonePath);

    /* ---- 清理 ---- */
    void (*Destroy)(struct ProtocolHandler* self);

} ProtocolHandler;