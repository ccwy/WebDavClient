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

    // 1. 动态生成标准的 rclone.conf 配置文件
    char confPath[MAX_PATH];
    sprintf_s(confPath, sizeof(confPath), "%s\\rclone.conf", workDir);

    FILE* f = fopen(confPath, "w");
    if (!f) {
        LogMessage("ERROR", "Failed to create temporary rclone.conf at path: %s", confPath);
        return 0;
    }
    fprintf(f, "[webdav_remote]\n");
    fprintf(f, "type = webdav\n");
    fprintf(f, "url = %s\n", url);
    fprintf(f, "user = %s\n", user);
    fprintf(f, "pass = %s\n", pass);
    fclose(f);

    // 2. 构造详细日志输出路径
    char logPath[MAX_PATH];
    sprintf_s(logPath, sizeof(logPath), "%s\\rclone_error.log", workDir);

    // 3. 构造 rclone mount 命令行参数 (driveLetter 作为字符串拼入，如 Z)
    char cmd[2048];
    sprintf_s(cmd, sizeof(cmd), 
        "\"%s\" mount webdav_remote: %s: --config \"%s\" --vfs-cache-mode writes --volname \"WebDAV_Disk\" --log-file \"%s\" -vv",
        rclonePath, (driveLetter && driveLetter[0] != '\0') ? driveLetter : "Z", confPath, logPath
    );

    LogMessage("INFO", "Starting Rclone mount command: %s", cmd);

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // 隐藏 rclone 黑窗口

    if (g_rclonePi.hProcess != NULL) {
        CloseHandle(g_rclonePi.hProcess);
        CloseHandle(g_rclonePi.hThread);
        memset(&g_rclonePi, 0, sizeof(g_rclonePi));
    }

    // 4. 启动 rclone 进程
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