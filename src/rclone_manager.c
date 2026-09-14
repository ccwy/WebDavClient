#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <ctype.h>
#include "rclone_manager.h"
#include "logger.h"

static PROCESS_INFORMATION g_rclonePi = { 0 };
static char g_cacheDir[MAX_PATH] = { 0 };

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

    char obscuredPass[256] = { 0 };
    GetObscuredPassword(rclonePath, pass, obscuredPass, sizeof(obscuredPass));

    // 设置缓存目录为程序目录下的 cache 子目录
    sprintf_s(g_cacheDir, sizeof(g_cacheDir), "%s\\cache", workDir);
    CreateDirectoryA(g_cacheDir, NULL);

    char cmd[2048];
    if (debug_log) {
        char logPath[MAX_PATH];
        sprintf_s(logPath, sizeof(logPath), "%s\\rclone_error.log", workDir);

        sprintf_s(cmd, sizeof(cmd), 
            "\"%s\" mount :webdav: %s: --webdav-url \"%s\" --webdav-user \"%s\" --webdav-pass \"%s\" "
            "--vfs-cache-mode full "
            "--vfs-cache-max-size 10G "
            "--vfs-cache-max-age 72h "
            "--vfs-cache-poll-interval 5m "
            "--dir-cache-time 72h "
            "--vfs-read-chunk-size 16M "
            "--vfs-read-chunk-size-limit 64M "
            "--cache-dir \"%s\" "
            "--no-check-certificate "
            "--volname \"WebDAV_Disk\" --log-file \"%s\" -vv",
            rclonePath, targetDrive, url, user, obscuredPass, g_cacheDir, logPath
        );
        LogMessage("INFO", "Starting Rclone mount with full-cache, chunked-read and debug logging enabled.");
    } else {
        sprintf_s(cmd, sizeof(cmd), 
            "\"%s\" mount :webdav: %s: --webdav-url \"%s\" --webdav-user \"%s\" --webdav-pass \"%s\" "
            "--vfs-cache-mode full "
            "--vfs-cache-max-size 10G "
            "--vfs-cache-max-age 72h "
            "--vfs-cache-poll-interval 5m "
            "--dir-cache-time 72h "
            "--vfs-read-chunk-size 16M "
            "--vfs-read-chunk-size-limit 64M "
            "--cache-dir \"%s\" "
            "--no-check-certificate "
            "--volname \"WebDAV_Disk\"",
            rclonePath, targetDrive, url, user, obscuredPass, g_cacheDir
        );
        LogMessage("INFO", "Starting Rclone mount with full-cache, chunked-read and debug logging disabled.");
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

// 递归删除指定目录下的过期缓存文件
// 清理策略：删除超过 72 小时的缓存文件，保留目录结构
static void DeleteOldFilesRecursive(const char* dirPath, FILETIME* cutoffTime) {
    char searchPath[MAX_PATH];
    sprintf_s(searchPath, sizeof(searchPath), "%s\\*", dirPath);

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) continue;

        char fullPath[MAX_PATH];
        sprintf_s(fullPath, sizeof(fullPath), "%s\\%s", dirPath, findData.cFileName);

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // 递归进入子目录
            DeleteOldFilesRecursive(fullPath, cutoffTime);
        } else {
            // 检查文件最后修改时间是否超过截止时间
            if (CompareFileTime(&findData.ftLastWriteTime, cutoffTime) < 0) {
                if (DeleteFileA(fullPath)) {
                    LogMessage("INFO", "Cleaned expired cache file: %s", fullPath);
                }
            }
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
}

void CleanRcloneCache() {
    if (g_cacheDir[0] == '\0') {
        // 缓存目录未设置，尝试使用默认路径
        char workDir[MAX_PATH];
        GetModuleFileNameA(NULL, workDir, MAX_PATH);
        char* lastSlash = strrchr(workDir, '\\');
        if (lastSlash) *lastSlash = '\0';
        sprintf_s(g_cacheDir, sizeof(g_cacheDir), "%s\\cache", workDir);
    }

    // 检查缓存目录是否存在
    if (GetFileAttributesA(g_cacheDir) == INVALID_FILE_ATTRIBUTES) {
        LogMessage("INFO", "Cache directory does not exist, skipping cleanup.");
        return;
    }

    // 计算截止时间：当前时间往前推 72 小时
    SYSTEMTIME st;
    GetSystemTime(&st);
    FILETIME ftNow, ftCutoff;
    SystemTimeToFileTime(&st, &ftNow);

    // 72 小时 = 72 * 60 * 60 * 1000 万分之一秒 = 259,200,000,000 (100ns units)
    ULARGE_INTEGER ulNow, ulCutoff;
    ulNow.LowPart = ftNow.dwLowDateTime;
    ulNow.HighPart = ftNow.dwHighDateTime;
    ulCutoff.QuadPart = ulNow.QuadPart - (ULONGLONG)72 * 60 * 60 * 10000000;
    ftCutoff.dwLowDateTime = ulCutoff.LowPart;
    ftCutoff.dwHighDateTime = ulCutoff.HighPart;

    LogMessage("INFO", "Cleaning rclone cache directory: %s (removing files older than 72h)", g_cacheDir);
    DeleteOldFilesRecursive(g_cacheDir, &ftCutoff);
}

// 递归删除指定目录下的所有文件和子目录（用于卸载/退出时彻底清理缓存）
static void DeleteAllFilesRecursive(const char* dirPath) {
    char searchPath[MAX_PATH];
    sprintf_s(searchPath, sizeof(searchPath), "%s\\*", dirPath);

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) continue;

        char fullPath[MAX_PATH];
        sprintf_s(fullPath, sizeof(fullPath), "%s\\%s", dirPath, findData.cFileName);

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // 先递归删除子目录内容，再删除子目录本身
            DeleteAllFilesRecursive(fullPath);
            if (RemoveDirectoryA(fullPath)) {
                LogMessage("INFO", "Removed cache directory: %s", fullPath);
            }
        } else {
            if (DeleteFileA(fullPath)) {
                LogMessage("INFO", "Purged cache file: %s", fullPath);
            }
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
}

// 彻底清理缓存：删除缓存目录下所有文件和子目录
void PurgeRcloneCache() {
    if (g_cacheDir[0] == '\0') {
        char workDir[MAX_PATH];
        GetModuleFileNameA(NULL, workDir, MAX_PATH);
        char* lastSlash = strrchr(workDir, '\\');
        if (lastSlash) *lastSlash = '\0';
        sprintf_s(g_cacheDir, sizeof(g_cacheDir), "%s\\cache", workDir);
    }

    if (GetFileAttributesA(g_cacheDir) == INVALID_FILE_ATTRIBUTES) {
        LogMessage("INFO", "Cache directory does not exist, skipping purge.");
        return;
    }

    LogMessage("INFO", "Purging rclone cache directory: %s", g_cacheDir);
    DeleteAllFilesRecursive(g_cacheDir);

    // 删除缓存根目录本身
    if (RemoveDirectoryA(g_cacheDir)) {
        LogMessage("INFO", "Removed cache root directory: %s", g_cacheDir);
    }

    // 重置缓存目录标记，避免后续误操作
    g_cacheDir[0] = '\0';
}

void StopRcloneMount() {
    if (g_rclonePi.hProcess != NULL) {
        LogMessage("INFO", "Stopping Rclone mount process gracefully (flushing cache)...");

        // 第一步：优雅停止 —— 使用 taskkill 不带 /f，让 rclone 收到信号后刷新缓存并自行退出
        char taskkillCmd[256];
        sprintf_s(taskkillCmd, sizeof(taskkillCmd), "taskkill /pid %lu", g_rclonePi.dwProcessId);

        STARTUPINFOA si = { sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = { 0 };
        if (CreateProcessA(NULL, taskkillCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }

        // 等待 rclone 进程优雅退出（最多 10 秒，确保缓存刷新完成）
        DWORD waitResult = WaitForSingleObject(g_rclonePi.hProcess, 10000);
        if (waitResult == WAIT_TIMEOUT) {
            LogMessage("WARN", "Rclone did not exit gracefully within 10s, forcing termination...");
            TerminateProcess(g_rclonePi.hProcess, 0);
            WaitForSingleObject(g_rclonePi.hProcess, 3000);
        } else {
            LogMessage("INFO", "Rclone exited gracefully, cache flushed.");
        }

        CloseHandle(g_rclonePi.hProcess);
        CloseHandle(g_rclonePi.hThread);
        memset(&g_rclonePi, 0, sizeof(g_rclonePi));
    }

    // 第二步：强制终止所有残留的 rclone 进程（防止僵尸进程）
    STARTUPINFOA si2 = { sizeof(si2) };
    si2.dwFlags = STARTF_USESHOWWINDOW;
    si2.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi2 = { 0 };
    if (CreateProcessA(NULL, (LPSTR)"taskkill /f /im rclone.exe", NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si2, &pi2)) {
        WaitForSingleObject(pi2.hProcess, 1500);
        CloseHandle(pi2.hProcess);
        CloseHandle(pi2.hThread);
    }

    // 第三步：彻底清理缓存目录（卸载/退出时清空所有缓存文件）
    PurgeRcloneCache();

    // 第四步：触发资源管理器刷新，清除左侧残留盘符
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    LogMessage("INFO", "Rclone mount stopped, cache flushed, purged and explorer refreshed.");
}