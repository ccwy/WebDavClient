#include <windows.h>
#include <stdio.h>
#include <ctype.h>
#include "rclone_manager.h"
#include "logger.h"

static PROCESS_INFORMATION g_rclonePi = { 0 };

static int CheckDriveExists(const char* driveLetter) {
    if (!driveLetter || driveLetter[0] == '\0') return 0;
    char rootPath[8];
    sprintf_s(rootPath, sizeof(rootPath), "%c:\\", (char)toupper((unsigned char)driveLetter[0]));
    UINT type = GetDriveTypeA(rootPath);
    return (type != DRIVE_UNKNOWN && type != DRIVE_NO_ROOT_DIR);
}

int StartRcloneMount(const char* rclonePath, const char* url, const char* user, const char* pass, const char* driveLetter) {
    // 获取程序当前所在目录
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    char logPath[MAX_PATH];
    sprintf_s(logPath, sizeof(logPath), "%s\\rclone_error.log", workDir);

    const char* targetDrive = (driveLetter && driveLetter[0] != '\0') ? driveLetter : "Z";

    // 直接通过命令行参数使用明文密码挂载，彻底抛弃加解密和配置文件
    char cmd[2048];
    sprintf_s(cmd, sizeof(cmd), 
        "\"%s\" mount :webdav: %s: --webdav-url \"%s\" --webdav-user \"%s\" --webdav-pass \"%s\" --vfs-cache-mode writes --volname \"WebDAV_Disk\" --log-file \"%s\" -vv",
        rclonePath, targetDrive, url, user, pass, logPath
    );

    LogMessage("INFO", "Starting Rclone mount command: %s", cmd);

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (g_rclonePi.hProcess != NULL) {
        CloseHandle(g_rclonePi.hProcess);
        CloseHandle(g_rclonePi.hThread);
        memset(&g_rclonePi, 0, sizeof(g_rclonePi));
    }

    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, workDir, &si, &g_rclonePi)) {
        LogMessage("ERROR", "Failed to start Rclone process. Error code: %lu", GetLastError());
        return 0;
    }

    // 智能状态轮询：检查进程是否崩溃以及盘符是否真实生成
    for (int i = 0; i < 6; i++) {
        Sleep(500);

        DWORD exitCode = 0;
        if (GetExitCodeProcess(g_rclonePi.hProcess, &exitCode)) {
            if (exitCode != STILL_ACTIVE) {
                LogMessage("ERROR", "Rclone process exited prematurely with code: %lu. Check rclone_error.log.", exitCode);
                StopRcloneMount();
                return 0;
            }
        }

        if (CheckDriveExists(targetDrive)) {
            LogMessage("INFO", "Mount verified successfully! Drive %s: is active.", targetDrive);
            return 1;
        }
    }

    LogMessage("ERROR", "Mount timeout: Drive %s: was not created. Check rclone_error.log.", targetDrive);
    StopRcloneMount();
    return 0;
}

void StopRcloneMount() {
    if (g_rclonePi.hProcess != NULL) {
        LogMessage("INFO", "Stopping Rclone mount process...");
        TerminateProcess(g_rclonePi.hProcess, 0);
        WaitForSingleObject(g_rclonePi.hProcess, 3000);
        CloseHandle(g_rclonePi.hProcess);
        CloseHandle(g_rclonePi.hThread);
        memset(&g_rclonePi, 0, sizeof(g_rclonePi));
    }
    system("taskkill /f /im rclone.exe >nul 2>&1");
    LogMessage("INFO", "Rclone mount stopped and cleaned up.");
}