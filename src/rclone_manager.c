#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <ctype.h>
#include "rclone_manager.h"
#include "logger.h"

static PROCESS_INFORMATION g_rclonePi = { 0 };

static int GetObscuredPassword(const char* rclonePath, const char* plainPass, char* outObscured, size_t maxLen) {
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

static const char* GetVfsCacheModeStr(int mode) {
    switch (mode) {
        case 0: return "off";
        case 1: return "minimal";
        case 2: return "writes";
        case 3: return "full";
        default: return "writes";
    }
}

int StartRcloneMount(const char* rclonePath, const char* url, const AppConfig* cfg) {
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    const char* targetDrive = (cfg->drive && cfg->drive[0] != '\0') ? cfg->drive : "Z";

    char obscuredPass[256] = { 0 };
    GetObscuredPassword(rclonePath, cfg->pass, obscuredPass, sizeof(obscuredPass));

    const char* cacheMode = GetVfsCacheModeStr(cfg->vfs_cache_mode);

    // 构建高级参数字符串
    char advParams[1024] = { 0 };
    char tmpBuf[256];

    // --dir-cache-time
    if (cfg->dir_cache_time[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--dir-cache-time %s ", cfg->dir_cache_time);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    // --buffer-size
    if (cfg->buffer_size[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--buffer-size %s ", cfg->buffer_size);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    // --transfers
    if (cfg->transfers > 0) {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--transfers %d ", cfg->transfers);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    // --cache-dir
    if (cfg->cache_dir[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--cache-dir \"%s\" ", cfg->cache_dir);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    // --vfs-cache-max-age
    if (cfg->vfs_cache_max_age[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-cache-max-age %s ", cfg->vfs_cache_max_age);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    // --vfs-read-chunk-size
    if (cfg->vfs_read_chunk_size[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-read-chunk-size %s ", cfg->vfs_read_chunk_size);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    // --vfs-read-chunk-size-limit
    if (cfg->vfs_read_chunk_size_limit[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-read-chunk-size-limit %s ", cfg->vfs_read_chunk_size_limit);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }

    char cmd[4096];
    if (cfg->debug_log) {
        char logPath[MAX_PATH];
        sprintf_s(logPath, sizeof(logPath), "%s\\rclone_error.log", workDir);

        sprintf_s(cmd, sizeof(cmd), 
            "\"%s\" mount :webdav: %s: --webdav-url \"%s\" --webdav-user \"%s\" --webdav-pass \"%s\" "
            "--vfs-cache-mode %s "
            "--vfs-cache-max-size 5G "
            "%s"
            "--no-check-certificate "
            "--volname \"WebDAV_Disk\" --log-file \"%s\" -vv",
            rclonePath, targetDrive, url, cfg->user, obscuredPass, cacheMode, advParams, logPath
        );
        LogMessage("INFO", "Starting Rclone mount with vfs-cache-mode=%s and debug logging enabled.", cacheMode);
    } else {
        sprintf_s(cmd, sizeof(cmd), 
            "\"%s\" mount :webdav: %s: --webdav-url \"%s\" --webdav-user \"%s\" --webdav-pass \"%s\" "
            "--vfs-cache-mode %s "
            "--vfs-cache-max-size 5G "
            "%s"
            "--no-check-certificate "
            "--volname \"WebDAV_Disk\"",
            rclonePath, targetDrive, url, cfg->user, obscuredPass, cacheMode, advParams
        );
        LogMessage("INFO", "Starting Rclone mount with vfs-cache-mode=%s and debug logging disabled.", cacheMode);
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
                LogMessage("ERROR", "Rclone process exited prematurely with code: %lu. Check rclone_error.log[cite: 3].", exitCode);
                StopRcloneMount();
                return 0;
            }
        }

        if (CheckDriveExists(targetDrive)) {
            LogMessage("INFO", "Mount verified successfully! Drive %s: is active[cite: 3].", targetDrive);
            return 1;
        }
    }

    LogMessage("ERROR", "Mount timeout: Drive %s: was not created. Check rclone_error.log[cite: 3].", targetDrive);
    StopRcloneMount();
    return 0;
}

void StopRcloneMount() {
    if (g_rclonePi.hProcess != NULL) {
        LogMessage("INFO", "Stopping Rclone mount process...[cite: 3]");
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

    // 触发资源管理器刷新，清除左侧残留盘符[cite: 3]
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    LogMessage("INFO", "Rclone mount stopped, cleaned up and explorer refreshed[cite: 3].");
}