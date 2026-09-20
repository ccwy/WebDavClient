#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stddef.h>
#include "rclone_manager.h"
#include "logger.h"

static PROCESS_INFORMATION g_rclonePi = { 0 };
static char g_mountedDrive[4] = { 0 };  /* 记录当前挂载的盘符，用于卸载时通知资源管理器 */

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

    /* 读取 stderr/stdout 输出（rclone 将错误信息输出到 stderr） */
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

    /* 等待进程完成，最多15秒（网络操作可能需要较长时间） */
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
        /* 去除尾部空白 */
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
            LogMessage("ERROR", "Connection test failed with exit code: %lu. Check credentials and server settings.", exitCode);
        }
        return 0;
    }

    LogMessage("INFO", "Connection test passed. Proceeding with mount.");
    return 1;
}

int StartRcloneProcess(const char* fullCmdLine, const char* driveLetter) {
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    const char* targetDrive = (driveLetter && driveLetter[0] != '\0') ? driveLetter : "Z";
    strcpy_s(g_mountedDrive, sizeof(g_mountedDrive), targetDrive);

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

    /* 第二阶段：稳定性验证 - 等待2秒确认盘符稳定
     * 预认证已在挂载前验证凭据，此处仅需确认盘符创建后稳定 */
    LogMessage("INFO", "Drive %s: detected, performing stability verification...", targetDrive);
    Sleep(1000);

    /* 检查进程是否仍在运行 */
    DWORD verifyCode = 0;
    if (GetExitCodeProcess(g_rclonePi.hProcess, &verifyCode)) {
        if (verifyCode != STILL_ACTIVE) {
            LogMessage("ERROR", "Rclone process exited after drive creation (code: %lu). Check rclone_error.log.", verifyCode);
            StopRcloneMount();
            return 0;
        }
    }

    /* 确认盘符仍然存在 */
    if (!CheckDriveExists(targetDrive)) {
        LogMessage("ERROR", "Drive %s: disappeared after creation. Mount may be unstable. Check rclone_error.log.", targetDrive);
        StopRcloneMount();
        return 0;
    }

    LogMessage("INFO", "Mount verified successfully! Drive %s: is active and stable.", targetDrive);

    /* 刷新资源管理器，使新盘符立即显示 */
    {
        char rootPath[8];
        sprintf_s(rootPath, sizeof(rootPath), "%c:\\", targetDrive[0]);
        SHChangeNotify(SHCNE_DRIVEADD, SHCNF_PATH | SHCNF_FLUSHNOWAIT, rootPath, NULL);
    }

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

    /* 刷新资源管理器，移除盘符显示 */
    if (g_mountedDrive[0] != '\0') {
        char rootPath[8];
        sprintf_s(rootPath, sizeof(rootPath), "%c:\\", g_mountedDrive[0]);
        SHChangeNotify(SHCNE_DRIVEREMOVED, SHCNF_PATH | SHCNF_FLUSHNOWAIT, rootPath, NULL);
        g_mountedDrive[0] = '\0';
    }

    LogMessage("INFO", "Rclone mount stopped, cleaned up and explorer refreshed.");
}