#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <ctype.h>
#include "rclone_manager.h"
#include "logger.h"

static PROCESS_INFORMATION g_rclonePi = { 0 };

// 内部辅助函数：获取 Windows 真实主版本号
static int GetWindowsMajorVersion() {
    typedef LONG(WINAPI* RtlGetVersionPtr)(OSVERSIONINFOW*);
    OSVERSIONINFOW rovi = { 0 };
    rovi.dwOSVersionInfoSize = sizeof(rovi);
    HMODULE hMod = GetModuleHandleA("ntdll.dll");
    if (hMod) {
        RtlGetVersionPtr RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
        if (RtlGetVersion) {
            RtlGetVersion(&rovi);
            return (int)rovi.dwMajorVersion;
        }
    }
    return 6; // 如果获取失败，默认按 Win7 (内核版本 6.1) 处理
}

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

int StartRcloneMount(const char* rclonePath, const char* url, const char* user, const char* pass, const char* driveLetter, int debug_log) {
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    const char* targetDrive = (driveLetter && driveLetter[0] != '\0') ? driveLetter : "Z";

    // ---------------------------------------------------------
    // 核心修改：动态判断系统版本，选择缓存模式
    // ---------------------------------------------------------
    int majorVer = GetWindowsMajorVersion();
    const char* cacheMode = (majorVer >= 10) ? "off" : "writes"; // Win10+ 用 off，Win7 用 writes

    char obscuredPass[256] = { 0 };
    GetObscuredPassword(rclonePath, pass, obscuredPass, sizeof(obscuredPass));

    char cmd[2048];
    if (debug_log) {
        char logPath[MAX_PATH];
        sprintf_s(logPath, sizeof(logPath), "%s\\rclone_error.log", workDir);

        sprintf_s(cmd, sizeof(cmd), 
            "\"%s\" mount :webdav: %s: --webdav-url \"%s\" --webdav-user \"%s\" --webdav-pass \"%s\" "
            "--vfs-cache-mode %s "
            "--no-check-certificate "
            "--volname \"WebDAV_Disk\" --log-file \"%s\" -vv",
            rclonePath, targetDrive, url, user, obscuredPass, cacheMode, logPath
        );
        LogMessage("INFO", "Starting Rclone mount with cache-mode %s and debug logging enabled.", cacheMode);
    } else {
        sprintf_s(cmd, sizeof(cmd), 
            "\"%s\" mount :webdav: %s: --webdav-url \"%s\" --webdav-user \"%s\" --webdav-pass \"%s\" "
            "--vfs-cache-mode %s "
            "--no-check-certificate "
            "--volname \"WebDAV_Disk\" --log-level OFF",
            rclonePath, targetDrive, url, user, obscuredPass, cacheMode
        );
        LogMessage("INFO", "Starting Rclone mount with cache-mode %s and debug logging disabled.", cacheMode);
    }

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

    // 彻底摒弃 system()，改用完全隐藏的 CreateProcessA 执行 taskkill，彻底消除黑框闪烁
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = { 0 };
    if (CreateProcessA(NULL, (LPSTR)"taskkill /f /im rclone.exe", NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 1500);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    // 触发资源管理器刷新，清除左侧残留盘符
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    LogMessage("INFO", "Rclone mount stopped, cleaned up and explorer refreshed.");
}