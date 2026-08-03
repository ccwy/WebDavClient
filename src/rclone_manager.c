#include <windows.h>
#include <stdio.h>
#include "rclone_manager.h"
#include "logger.h"

// 全局保存 rclone 进程句柄，方便后续卸载/关闭
static PROCESS_INFORMATION g_rclonePi = { 0 };

int StartRcloneMount(const char* rclonePath, const char* url, const char* user, const char* pass, const char* driveLetter) {
    char tempDir[MAX_PATH];
    GetTempPathA(sizeof(tempDir), tempDir);
    
    char workDir[MAX_PATH];
    sprintf_s(workDir, sizeof(workDir), "%sWebDavClientEnv", tempDir);

    // 1. 动态生成标准的 rclone.conf 配置文件
    // 这样可以彻底避免命令行明文传参时密码格式解析错误或特殊字符被转义的问题
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

    // 2. 构造详细日志输出路径（如果挂载失败，可以在这里查看精确原因）
    char logPath[MAX_PATH];
    sprintf_s(logPath, sizeof(logPath), "%s\\rclone_error.log", workDir);

    // 3. 构造 rclone mount 命令行参数
    // 使用配置文件 (webdav_remote:)，开启详细调试日志 (-vv)
    char cmd[2048];
    sprintf_s(cmd, sizeof(cmd), 
        "\"%s\" mount webdav_remote: %s: --config \"%s\" --vfs-cache-mode writes --volname \"WebDAV_Disk\" --log-file \"%s\" -vv",
        rclonePath, driveLetter, confPath, logPath
    );

    LogMessage("INFO", "Starting Rclone mount command: %s", cmd);

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // 隐藏 rclone 的黑窗口

    // 如果之前有残留的挂载进程，先清理
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
        
        // 终止 rclone 进程
        TerminateProcess(g_rclonePi.hProcess, 0);
        WaitForSingleObject(g_rclonePi.hProcess, 3000);
        
        CloseHandle(g_rclonePi.hProcess);
        CloseHandle(g_rclonePi.hThread);
        memset(&g_rclonePi, 0, sizeof(g_rclonePi));
        
        // 强制杀掉可能残留的 rclone 实例
        system("taskkill /f /im rclone.exe >nul 2>&1");
        
        LogMessage("INFO", "Rclone mount stopped and cleaned up.");
    } else {
        // 保底清理
        system("taskkill /f /im rclone.exe >nul 2>&1");
    }
}