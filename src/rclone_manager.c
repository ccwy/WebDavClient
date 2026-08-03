#include <windows.h>
#include <stdio.h>
#include <ctype.h>
#include "rclone_manager.h"
#include "logger.h"

static PROCESS_INFORMATION g_rclonePi = { 0 };

// 依照 Rclone 官方规范：通过 rclone obscure 将明文密码转换为 rclone 识别的密文
static int GetObscuredPassword(const char* rclonePath, const char* plainPass, char* outObscured, size_t maxLen) {
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
            // 去除末尾的换行符和空格
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

static int CheckDriveExists(const char* driveLetter) {
    if (!driveLetter || driveLetter[0] == '\0') return 0;
    char rootPath[8];
    sprintf_s(rootPath, sizeof(rootPath), "%c:\\", (char)toupper((unsigned char)driveLetter[0]));
    UINT type = GetDriveTypeA(rootPath);
    return (type != DRIVE_UNKNOWN && type != DRIVE_NO_ROOT_DIR);
}

int StartRcloneMount(const char* rclonePath, const char* url, const char* user, const char* pass, const char* driveLetter) {
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    char logPath[MAX_PATH];
    sprintf_s(logPath, sizeof(logPath), "%s\\rclone_error.log", workDir);

    const char* targetDrive = (driveLetter && driveLetter[0] != '\0') ? driveLetter : "Z";

    // 1. 核心步骤：将前端传来的明文密码转换为 Rclone 要求的密文格式
    char obscuredPass[256] = { 0 };
    GetObscuredPassword(rclonePath, pass, obscuredPass, sizeof(obscuredPass));

    // 2. 构造符合 Rclone 规范的挂载命令
    char cmd[2048];
    sprintf_s(cmd, sizeof(cmd), 
        "\"%s\" mount :webdav: %s: --webdav-url \"%s\" --webdav-user \"%s\" --webdav-pass \"%s\" --vfs-cache-mode writes --volname \"WebDAV_Disk\" --log-file \"%s\" -vv",
        rclonePath, targetDrive, url, user, obscuredPass, logPath
    );

    LogMessage("INFO", "Starting Rclone mount command with obscured password.");

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

    // 3. 智能状态轮询：验证挂载是否成功
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