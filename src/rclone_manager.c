#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stddef.h>
#include "rclone_manager.h"
#include "logger.h"

static PROCESS_INFORMATION g_rclonePi = { 0 };

int RcloneObscurePassword(const char* rclonePath, const char* plainPass, char* outObscured, size_t maxLen) {
    char cmd[MAX_PATH + 256];
    sprintf_s(cmd, sizeof(cmd), "\"%s\" obscure \"%s\"", rclonePath, plainPass);

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hReadPipe;
    HANDLE hWritePipe;
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

static int CheckDriveExists(const char* driveLetter) {
    if (!driveLetter || driveLetter[0] == '\0') return 0;
    char rootPath[8];
    sprintf_s(rootPath, sizeof(rootPath), "%c:\\", (char)toupper((unsigned char)driveLetter[0]));
    UINT type = GetDriveTypeA(rootPath);
    return (type != DRIVE_UNKNOWN && type != DRIVE_NO_ROOT_DIR);
}

int StartRcloneProcess(const char* fullCmdLine, const char* driveLetter) {
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    const char* targetDrive = (driveLetter && driveLetter[0] != '\0') ? driveLetter : "Z";

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (g_rclonePi.hProcess != NULL) {
        CloseHandle(g_rclonePi.hProcess);
        CloseHandle(g_rclonePi.hThread);
        memset(&g_rclonePi, 0, sizeof(g_rclonePi));
    }

    /* fullCmdLine 是调用方已构建好的完整命令行，直接执行 */
    char mutableCmd[4096];
    strcpy_s(mutableCmd, sizeof(mutableCmd), fullCmdLine);

    if (!CreateProcessA(NULL, mutableCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, workDir, &si, &g_rclonePi)) {
        LogMessage("ERROR", "Failed to start Rclone process. Error code: %lu", GetLastError());
        return 0;
    }

    LogMessage("INFO", "Rclone process started, waiting for drive %s: ...", targetDrive);

    /* 第一阶段：等待盘符出现（最多10秒） */
    int driveFound = 0;
    for (int i = 0; i < 20; i++) {  /* 20次 × 500ms = 10秒超时 */
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
            driveFound = 1;
            break;
        }
    }

    if (!driveFound) {
        LogMessage("ERROR", "Mount timeout: Drive %s: was not created within 10 seconds. Check rclone_error.log.", targetDrive);
        StopRcloneMount();
        return 0;
    }

    /* 第二阶段：二次验证 - 等待2秒后确认盘符仍然存在且进程仍在运行 */
    LogMessage("INFO", "Drive %s: detected, performing stability verification...", targetDrive);
    Sleep(2000);

    /* 检查进程是否仍在运行 */
    DWORD verifyCode = 0;
    if (GetExitCodeProcess(g_rclonePi.hProcess, &verifyCode)) {
        if (verifyCode != STILL_ACTIVE) {
            LogMessage("ERROR", "Rclone process exited after drive creation (code: %lu). Authentication or connection may have failed. Check rclone_error.log.", verifyCode);
            StopRcloneMount();
            return 0;
        }
    }

    /* 检查盘符是否仍然存在 */
    if (!CheckDriveExists(targetDrive)) {
        LogMessage("ERROR", "Drive %s: disappeared after creation. Mount may be unstable. Check rclone_error.log.", targetDrive);
        StopRcloneMount();
        return 0;
    }

    LogMessage("INFO", "Mount verified successfully! Drive %s: is active and stable.", targetDrive);
    return 1;
}

void StopRcloneMount(void) {
    if (g_rclonePi.hProcess != NULL) {
        LogMessage("INFO", "Stopping Rclone mount process...");
        TerminateProcess(g_rclonePi.hProcess, 0);
        WaitForSingleObject(g_rclonePi.hProcess, 3000);
        CloseHandle(g_rclonePi.hProcess);
        CloseHandle(g_rclonePi.hThread);
        memset(&g_rclonePi, 0, sizeof(g_rclonePi));
    }

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = { 0 };
    if (CreateProcessA(NULL, (LPSTR)"taskkill /f /im rclone.exe", NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 1500);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    LogMessage("INFO", "Rclone mount stopped, cleaned up and explorer refreshed.");
}