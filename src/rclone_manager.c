#include <windows.h>
#include <stdio.h>
#include "rclone_manager.h"
#include "logger.h"

static PROCESS_INFORMATION g_rclonePi = { 0 };

int StartRcloneMount(const char* rclonePath, const char* url, const char* user, const char* pass, const char* driveLetter) {
    char tempDir[MAX_PATH];
    GetTempPathA(sizeof(tempDir), tempDir);
    
    char workDir[MAX_PATH];
    sprintf_s(workDir, sizeof(workDir), "%sWebDavClientEnv", tempDir);

    // 构造详细日志输出路径（如果还有异常，依然会记录在这里）
    char logPath[MAX_PATH];
    sprintf_s(logPath, sizeof(logPath), "%s\\rclone_error.log", workDir);

    // 直接通过命令行参数传递明文参数（无需 rclone.conf，彻底避开密码解密报错）
    char cmd[2048];
    sprintf_s(cmd, sizeof(cmd), 
        "\"%s\" mount :webdav: %s: --webdav-url \"%s\" --webdav-user \"%s\" --webdav-pass \"%s\" --vfs-cache-mode writes --volname \"WebDAV_Disk\" --log-file \"%s\" -vv",
        rclonePath, 
        (driveLetter && driveLetter[0] != '\0') ? driveLetter : "Z", 
        url, user, pass, logPath
    );

    LogMessage("INFO", "Starting Rclone mount command: %s", cmd);

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // 隐藏 rclone 后台黑窗口

    if (g_rclonePi.hProcess != NULL) {
        CloseHandle(g_rclonePi.hProcess);
        CloseHandle(g_rclonePi.hThread);
        memset(&g_rclonePi, 0, sizeof(g_rclonePi));
    }

    // 启动 rclone 进程
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, workDir, &si, &g_rclonePi)) {
        LogMessage("INFO", "Rclone mount process started successfully. PID: %lu", g_rclonePi.dwProcessId);
        return 1;
    } else {
        LogMessage("ERROR", "Failed to start Rclone process. Error code: %lu", GetLastError());
        return 0;
    }
}

void StopRcloneMount() {
    if (g_rclonePi.hProcess != NULL) {
        LogMessage("INFO", "Stopping Rclone mount process...");
        TerminateProcess(g_rclonePi.hProcess, 0);
        WaitForSingleObject(g_rclonePi.hProcess, 3000);
        CloseHandle(g_rclonePi.hProcess);
        CloseHandle(g_rclonePi.hThread);
        memset(&g_rclonePi, 0, sizeof(g_rclonePi));
        system("taskkill /f /im rclone.exe >nul 2>&1");
        LogMessage("INFO", "Rclone mount stopped and cleaned up.");
    } else {
        system("taskkill /f /im rclone.exe >nul 2>&1");
    }
}