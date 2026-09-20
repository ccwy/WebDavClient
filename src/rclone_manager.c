#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "rclone_manager.h"
#include "logger.h"

/* ======================================================================
   挂载进程池 — 管理多个并行 rclone 挂载进程
   ====================================================================== */

static MountEntry g_mounts[MAX_MOUNTS];
static int g_mountCount = 0;

/* 查找挂载条目索引，-1 表示未找到 */
static int FindMountIndex(const char* connId) {
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (g_mounts[i].active && strcmp(g_mounts[i].connId, connId) == 0) {
            return i;
        }
    }
    return -1;
}

/* 查找空闲槽位，-1 表示已满 */
static int FindFreeSlot(void) {
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (!g_mounts[i].active) return i;
    }
    return -1;
}

/* 检查盘符是否已存在 */
static int CheckDriveExists(const char* driveLetter) {
    if (!driveLetter || driveLetter[0] == '\0') return 0;
    char rootPath[8];
    sprintf_s(rootPath, sizeof(rootPath), "%c:\\", (char)toupper((unsigned char)driveLetter[0]));
    UINT type = GetDriveTypeA(rootPath);
    return (type != DRIVE_UNKNOWN && type != DRIVE_NO_ROOT_DIR);
}

/* ======================================================================
   初始化 / 清理
   ====================================================================== */

void RcloneInit(void) {
    memset(g_mounts, 0, sizeof(g_mounts));
    g_mountCount = 0;
}

void RcloneCleanup(void) {
    StopAllMounts();
}

/* ======================================================================
   启动挂载进程
   ====================================================================== */

int StartRcloneProcess(const char* fullCmdLine, const char* driveLetter, const char* connId) {
    /* 检查该连接是否已挂载 */
    if (FindMountIndex(connId) >= 0) {
        LogMessage("WARN", "Connection %s is already mounted.", connId);
        return 0;
    }

    /* 查找空闲槽位 */
    int slot = FindFreeSlot();
    if (slot < 0) {
        LogMessage("ERROR", "No free mount slot available (max %d).", MAX_MOUNTS);
        return 0;
    }

    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    const char* targetDrive = (driveLetter && driveLetter[0] != '\0') ? driveLetter : "Z";

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };
    char mutableCmd[4096];
    strcpy_s(mutableCmd, sizeof(mutableCmd), fullCmdLine);

    if (!CreateProcessA(NULL, mutableCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, workDir, &si, &pi)) {
        LogMessage("ERROR", "Failed to start Rclone process for %s. Error code: %lu", connId, GetLastError());
        return 0;
    }

    LogMessage("INFO", "Rclone process started for %s, waiting for drive %s: ...", connId, targetDrive);

    /* 第一阶段：等待盘符出现（最多10秒） */
    int driveFound = 0;
    for (int i = 0; i < 20; i++) {
        Sleep(500);

        DWORD exitCode = 0;
        if (GetExitCodeProcess(pi.hProcess, &exitCode)) {
            if (exitCode != STILL_ACTIVE) {
                LogMessage("ERROR", "Rclone process for %s exited prematurely with code: %lu.", connId, exitCode);
                TerminateProcess(pi.hProcess, 0);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                return 0;
            }
        }

        if (CheckDriveExists(targetDrive)) {
            driveFound = 1;
            break;
        }
    }

    if (!driveFound) {
        LogMessage("ERROR", "Mount timeout for %s: Drive %s: was not created within 10 seconds.", connId, targetDrive);
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 0;
    }

    /* 第二阶段：稳定性验证 */
    LogMessage("INFO", "Drive %s: detected for %s, performing stability verification...", targetDrive, connId);
    Sleep(1000);

    DWORD verifyCode = 0;
    if (GetExitCodeProcess(pi.hProcess, &verifyCode)) {
        if (verifyCode != STILL_ACTIVE) {
            LogMessage("ERROR", "Rclone process for %s exited after drive creation (code: %lu).", connId, verifyCode);
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return 0;
        }
    }

    if (!CheckDriveExists(targetDrive)) {
        LogMessage("ERROR", "Drive %s: for %s disappeared after creation.", targetDrive, connId);
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 0;
    }

    /* 记录到挂载池 */
    strcpy_s(g_mounts[slot].connId, sizeof(g_mounts[slot].connId), connId);
    strcpy_s(g_mounts[slot].drive, sizeof(g_mounts[slot].drive), targetDrive);
    g_mounts[slot].pi = pi;
    g_mounts[slot].active = 1;
    g_mountCount++;

    LogMessage("INFO", "Mount verified for %s! Drive %s: is active and stable.", connId, targetDrive);

    /* 刷新资源管理器 */
    {
        char rootPath[8];
        sprintf_s(rootPath, sizeof(rootPath), "%c:\\", targetDrive[0]);
        SHChangeNotify(SHCNE_DRIVEADD, SHCNF_PATH | SHCNF_FLUSHNOWAIT, rootPath, NULL);
    }

    return 1;
}

/* ======================================================================
   停止指定连接的挂载（按 PID 终止，不影响其他连接）
   ====================================================================== */

int StopRcloneMount(const char* connId) {
    int idx = FindMountIndex(connId);
    if (idx < 0) {
        LogMessage("WARN", "Connection %s is not mounted.", connId);
        return 0;
    }

    MountEntry* entry = &g_mounts[idx];
    LogMessage("INFO", "Stopping mount for %s (drive %s:)...", connId, entry->drive);

    if (entry->pi.hProcess != NULL) {
        TerminateProcess(entry->pi.hProcess, 0);
        WaitForSingleObject(entry->pi.hProcess, 3000);
        CloseHandle(entry->pi.hProcess);
        CloseHandle(entry->pi.hThread);
    }

    /* 刷新资源管理器，移除盘符显示 */
    if (entry->drive[0] != '\0') {
        char rootPath[8];
        sprintf_s(rootPath, sizeof(rootPath), "%c:\\", entry->drive[0]);
        SHChangeNotify(SHCNE_DRIVEREMOVED, SHCNF_PATH | SHCNF_FLUSHNOWAIT, rootPath, NULL);
    }

    memset(entry, 0, sizeof(MountEntry));
    g_mountCount--;

    LogMessage("INFO", "Mount for %s stopped and cleaned up.", connId);
    return 1;
}

/* ======================================================================
   停止所有挂载
   ====================================================================== */

void StopAllMounts(void) {
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (g_mounts[i].active) {
            if (g_mounts[i].pi.hProcess != NULL) {
                TerminateProcess(g_mounts[i].pi.hProcess, 0);
                WaitForSingleObject(g_mounts[i].pi.hProcess, 1000);
                CloseHandle(g_mounts[i].pi.hProcess);
                CloseHandle(g_mounts[i].pi.hThread);
            }

            if (g_mounts[i].drive[0] != '\0') {
                char rootPath[8];
                sprintf_s(rootPath, sizeof(rootPath), "%c:\\", g_mounts[i].drive[0]);
                SHChangeNotify(SHCNE_DRIVEREMOVED, SHCNF_PATH | SHCNF_FLUSHNOWAIT, rootPath, NULL);
            }

            memset(&g_mounts[i], 0, sizeof(MountEntry));
        }
    }
    g_mountCount = 0;
    LogMessage("INFO", "All mounts stopped.");
}

/* ======================================================================
   查询接口
   ====================================================================== */

int IsMounted(const char* connId) {
    return FindMountIndex(connId) >= 0;
}

const MountEntry* GetMountEntry(int index) {
    if (index < 0 || index >= MAX_MOUNTS) return NULL;
    return &g_mounts[index];
}

int GetMountCount(void) {
    return g_mountCount;
}

/* ======================================================================
   连接测试（不挂载）
   ====================================================================== */

int TestRcloneConnection(const char* testCmd) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        LogMessage("ERROR", "TestRcloneConnection: Failed to create pipe.");
        return 0;
    }

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };
    char mutableCmd[4096];
    strcpy_s(mutableCmd, sizeof(mutableCmd), testCmd);

    if (!CreateProcessA(NULL, mutableCmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        LogMessage("ERROR", "TestRcloneConnection: Failed to create process. Error: %lu", GetLastError());
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return 0;
    }
    CloseHandle(hWritePipe);

    char errorMsg[2048] = { 0 };
    DWORD totalRead = 0;
    DWORD readBytes;
    while (totalRead < sizeof(errorMsg) - 1) {
        if (!ReadFile(hReadPipe, errorMsg + totalRead, sizeof(errorMsg) - 1 - totalRead, &readBytes, NULL) || readBytes == 0)
            break;
        totalRead += readBytes;
    }
    errorMsg[totalRead] = '\0';
    CloseHandle(hReadPipe);

    DWORD waitResult = WaitForSingleObject(pi.hProcess, 15000);
    if (waitResult == WAIT_TIMEOUT) {
        LogMessage("ERROR", "TestRcloneConnection: Connection test timed out after 15 seconds.");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 0;
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0) {
        for (int i = (int)strlen(errorMsg) - 1; i >= 0; i--) {
            if (errorMsg[i] == '\r' || errorMsg[i] == '\n' || errorMsg[i] == ' ' || errorMsg[i] == '\t') {
                errorMsg[i] = '\0';
            } else {
                break;
            }
        }
        if (errorMsg[0] != '\0') {
            LogMessage("ERROR", "Connection test failed: %s", errorMsg);
        } else {
            LogMessage("ERROR", "Connection test failed with exit code: %lu.", exitCode);
        }
        return 0;
    }

    LogMessage("INFO", "Connection test passed.");
    return 1;
}

/* ======================================================================
   密码混淆
   ====================================================================== */

int RcloneObscurePassword(const char* rclonePath, const char* plainPass, char* outObscured, size_t maxLen) {
    char cmd[MAX_PATH + 256];
    sprintf_s(cmd, sizeof(cmd), "\"%s\" obscure \"%s\"", rclonePath, plainPass);

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        strcpy_s(outObscured, maxLen, plainPass);
        return 0;
    }

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };
    if (CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hWritePipe);

        DWORD readBytes;
        char buffer[256] = { 0 };
        if (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &readBytes, NULL) && readBytes > 0) {
            buffer[readBytes] = '\0';
            for (int i = (int)strlen(buffer) - 1; i >= 0; i--) {
                if (buffer[i] == '\r' || buffer[i] == '\n' || buffer[i] == ' ' || buffer[i] == '\t') {
                    buffer[i] = '\0';
                } else {
                    break;
                }
            }
            strcpy_s(outObscured, maxLen, buffer);
            CloseHandle(hReadPipe);
            WaitForSingleObject(pi.hProcess, 2000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return 1;
        }
        CloseHandle(hReadPipe);
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
    }

    strcpy_s(outObscured, maxLen, plainPass);
    return 0;
}