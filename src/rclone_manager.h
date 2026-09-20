#pragma once

#include "protocol.h"

/* ======================================================================
   Rclone 进程管理 — 支持多连接并行挂载
   ====================================================================== */

/* 单个挂载进程信息 */
typedef struct {
    char        connId[64];          /* 对应连接 ID，如 "conn_1" */
    char        drive[4];            /* 挂载盘符，如 "Z" */
    PROCESS_INFORMATION pi;          /* 进程信息 */
    int         active;              /* 是否活跃（进程运行中） */
} MountEntry;

/* 最大并行挂载数 */
#define MAX_MOUNTS MAX_CONNECTIONS

/* 初始化/清理挂载管理器 */
void RcloneInit(void);
void RcloneCleanup(void);

/* 启动挂载进程（按连接 ID 跟踪） */
int StartRcloneProcess(const char* fullCmdLine, const char* driveLetter, const char* connId);

/* 停止指定连接的挂载 */
int StopRcloneMount(const char* connId);

/* 停止所有挂载 */
void StopAllMounts(void);

/* 检查指定连接是否已挂载 */
int IsMounted(const char* connId);

/* 获取挂载条目（用于遍历） */
const MountEntry* GetMountEntry(int index);
int GetMountCount(void);

/* 测试连接（不挂载） */
int TestRcloneConnection(const char* testCmd);

/* 密码混淆 */
int RcloneObscurePassword(const char* rclonePath, const char* plainPass, char* outObscured, size_t maxLen);