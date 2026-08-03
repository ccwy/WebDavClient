#include "config.h"
#include "logger.h"
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>

// 获取 Windows 主版本号辅助函数
static int GetWindowsMajorVersion() {
    NTSTATUS(WINAPI * RtlGetVersion)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW rovi = { 0 };
    rovi.dwOSVersionInfoSize = sizeof(rovi);
    *(FARPROC*)&RtlGetVersion = GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
    if (RtlGetVersion) {
        RtlGetVersion(&rovi);
        return (int)rovi.dwMajorVersion;
    }
    return 6; // 默认按 Win7 处理
}

void LoadConfig(AppConfig* cfg) {
    strcpy_s(cfg->host, sizeof(cfg->host), "192.168.5.100");
    strcpy_s(cfg->port, sizeof(cfg->port), "50055");
    strcpy_s(cfg->path, sizeof(cfg->path), "/music/");
    strcpy_s(cfg->user, sizeof(cfg->user), "www");
    strcpy_s(cfg->pass, sizeof(cfg->pass), "www");
    strcpy_s(cfg->drive, sizeof(cfg->drive), "Z");
    cfg->ssl = 0;
    cfg->auto_start = 0;
    cfg->debug_log = 0; // 默认关闭

    // 根据系统版本区分默认启动参数：Win10 保持 off，Win7 按报错修改为 writes 解决 WinFsp 挂载失败问题
    int majorVer = GetWindowsMajorVersion();
    if (majorVer >= 10) {
        strcpy_s(cfg->vfs_cache_mode, sizeof(cfg->vfs_cache_mode), "off");
    } else {
        strcpy_s(cfg->vfs_cache_mode, sizeof(cfg->vfs_cache_mode), "writes");
    }

    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    char iniPath[MAX_PATH];
    sprintf_s(iniPath, sizeof(iniPath), "%s\\config.ini", workDir);

    FILE* fp = NULL;
    if (fopen_s(&fp, iniPath, "r") != 0 || !fp) return;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;

        if (strcmp(key, "host") == 0) strcpy_s(cfg->host, sizeof(cfg->host), val);
        else if (strcmp(key, "port") == 0) strcpy_s(cfg->port, sizeof(cfg->port), val);
        else if (strcmp(key, "path") == 0) strcpy_s(cfg->path, sizeof(cfg->path), val);
        else if (strcmp(key, "user") == 0) strcpy_s(cfg->user, sizeof(cfg->user), val);
        else if (strcmp(key, "pass") == 0) strcpy_s(cfg->pass, sizeof(cfg->pass), val);
        else if (strcmp(key, "drive") == 0) strcpy_s(cfg->drive, sizeof(cfg->drive), val);
        else if (strcmp(key, "ssl") == 0) cfg->ssl = atoi(val);
        else if (strcmp(key, "auto_start") == 0) cfg->auto_start = atoi(val);
        else if (strcmp(key, "debug_log") == 0) cfg->debug_log = atoi(val);
        else if (strcmp(key, "vfs_cache_mode") == 0) strcpy_s(cfg->vfs_cache_mode, sizeof(cfg->vfs_cache_mode), val);
    }
    fclose(fp);
}

void SaveConfig(const AppConfig* cfg) {
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    char iniPath[MAX_PATH];
    sprintf_s(iniPath, sizeof(iniPath), "%s\\config.ini", workDir);

    FILE* fp = NULL;
    if (fopen_s(&fp, iniPath, "w") != 0 || !fp) return;

    fprintf(fp, "host=%s\n", cfg->host);
    fprintf(fp, "port=%s\n", cfg->port);
    fprintf(fp, "path=%s\n", cfg->path);
    fprintf(fp, "user=%s\n", cfg->user);
    fprintf(fp, "pass=%s\n", cfg->pass);
    fprintf(fp, "drive=%s\n", cfg->drive);
    fprintf(fp, "ssl=%d\n", cfg->ssl);
    fprintf(fp, "auto_start=%d\n", cfg->auto_start);
    fprintf(fp, "debug_log=%d\n", cfg->debug_log);
    fprintf(fp, "vfs_cache_mode=%s\n", cfg->vfs_cache_mode);

    fclose(fp);
}

void SetAppAutoStart(int enable) {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    char workDir[MAX_PATH];
    strcpy_s(workDir, sizeof(workDir), exePath);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    char exeName[MAX_PATH];
    const char* pName = strrchr(exePath, '\\');
    if (pName) {
        strcpy_s(exeName, sizeof(exeName), pName + 1);
    } else {
        strcpy_s(exeName, sizeof(exeName), exePath);
    }

    char* dot = strrchr(exeName, '.');
    if (dot && _stricmp(dot, ".exe") == 0) {
        *dot = '\0';
    }

    char startupDir[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_STARTUP, NULL, 0, startupDir) != S_OK) return;

    char shortcutPath[MAX_PATH];
    sprintf_s(shortcutPath, sizeof(shortcutPath), "%s\\%s.lnk", startupDir, exeName);

    if (!enable) {
        DeleteFileA(shortcutPath);
        return;
    }

    CoInitialize(NULL);
    IShellLinkA* psl = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkA, (void**)&psl))) {
        psl->lpVtbl->SetPath(psl, exePath);
        psl->lpVtbl->SetWorkingDirectory(psl, workDir);
        psl->lpVtbl->SetArguments(psl, "--tray");

        IPersistFile* ppf = NULL;
        if (SUCCEEDED(psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (void**)&ppf))) {
            WCHAR wszPath[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, shortcutPath, -1, wszPath, MAX_PATH);
            ppf->lpVtbl->Save(ppf, wszPath, TRUE);
            ppf->lpVtbl->Release(ppf);
        }
        psl->lpVtbl->Release(psl);
    }
    CoUninitialize();
}