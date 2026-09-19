#include "config.h"
#include "logger.h"
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>

void LoadConfig(AppConfig* cfg) {
    strcpy_s(cfg->host, sizeof(cfg->host), "192.168.5.100");
    strcpy_s(cfg->port, sizeof(cfg->port), "50055");
    strcpy_s(cfg->path, sizeof(cfg->path), "/music");
    strcpy_s(cfg->user, sizeof(cfg->user), "www");
    strcpy_s(cfg->pass, sizeof(cfg->pass), "www");
    strcpy_s(cfg->drive, sizeof(cfg->drive), "Z");
    cfg->ssl = 0;
    cfg->auto_start = 0;
    cfg->debug_log = 0; // 默认关闭
    cfg->auto_hide = 0;
    cfg->vfs_cache_mode = 3; // 默认 full
    // 高级设置默认值
    strcpy_s(cfg->dir_cache_time, sizeof(cfg->dir_cache_time), "72h");
    strcpy_s(cfg->buffer_size, sizeof(cfg->buffer_size), "16M");
    cfg->transfers = 4;
    cfg->cache_dir[0] = '\0'; // 空表示使用rclone默认临时目录
    strcpy_s(cfg->vfs_cache_max_age, sizeof(cfg->vfs_cache_max_age), "24h");
    strcpy_s(cfg->vfs_read_chunk_size, sizeof(cfg->vfs_read_chunk_size), "128M");
    strcpy_s(cfg->vfs_read_chunk_size_limit, sizeof(cfg->vfs_read_chunk_size_limit), "off");
    strcpy_s(cfg->volname, sizeof(cfg->volname), "WebDAV_Disk");
    strcpy_s(cfg->vfs_cache_max_size, sizeof(cfg->vfs_cache_max_size), "5G");

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
        else if (strcmp(key, "auto_hide") == 0) cfg->auto_hide = atoi(val);
        else if (strcmp(key, "vfs_cache_mode") == 0) cfg->vfs_cache_mode = atoi(val);
        else if (strcmp(key, "dir_cache_time") == 0) strcpy_s(cfg->dir_cache_time, sizeof(cfg->dir_cache_time), val);
        else if (strcmp(key, "buffer_size") == 0) strcpy_s(cfg->buffer_size, sizeof(cfg->buffer_size), val);
        else if (strcmp(key, "transfers") == 0) cfg->transfers = atoi(val);
        else if (strcmp(key, "cache_dir") == 0) strcpy_s(cfg->cache_dir, sizeof(cfg->cache_dir), val);
        else if (strcmp(key, "vfs_cache_max_age") == 0) strcpy_s(cfg->vfs_cache_max_age, sizeof(cfg->vfs_cache_max_age), val);
        else if (strcmp(key, "vfs_read_chunk_size") == 0) strcpy_s(cfg->vfs_read_chunk_size, sizeof(cfg->vfs_read_chunk_size), val);
        else if (strcmp(key, "vfs_read_chunk_size_limit") == 0) strcpy_s(cfg->vfs_read_chunk_size_limit, sizeof(cfg->vfs_read_chunk_size_limit), val);
        else if (strcmp(key, "volname") == 0) strcpy_s(cfg->volname, sizeof(cfg->volname), val);
        else if (strcmp(key, "vfs_cache_max_size") == 0) strcpy_s(cfg->vfs_cache_max_size, sizeof(cfg->vfs_cache_max_size), val);
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
    fprintf(fp, "auto_hide=%d\n", cfg->auto_hide);
    fprintf(fp, "vfs_cache_mode=%d\n", cfg->vfs_cache_mode);
    fprintf(fp, "dir_cache_time=%s\n", cfg->dir_cache_time);
    fprintf(fp, "buffer_size=%s\n", cfg->buffer_size);
    fprintf(fp, "transfers=%d\n", cfg->transfers);
    fprintf(fp, "cache_dir=%s\n", cfg->cache_dir);
    fprintf(fp, "vfs_cache_max_age=%s\n", cfg->vfs_cache_max_age);
    fprintf(fp, "vfs_read_chunk_size=%s\n", cfg->vfs_read_chunk_size);
    fprintf(fp, "vfs_read_chunk_size_limit=%s\n", cfg->vfs_read_chunk_size_limit);
    fprintf(fp, "volname=%s\n", cfg->volname);
    fprintf(fp, "vfs_cache_max_size=%s\n", cfg->vfs_cache_max_size);

    fclose(fp);
}

void SetAppAutoStart(int enable) {
    // 1. 获取当前程序的全路径，如: "C:\Program Files\MyApp\CustomWebDAV.exe"
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    // 2. 获取程序所在的工作目录
    char workDir[MAX_PATH];
    strcpy_s(workDir, sizeof(workDir), exePath);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    // 3. 【核心修正】：从全路径中提取当前程序文件名（如 "CustomWebDAV.exe"）
    char exeName[MAX_PATH];
    const char* pName = strrchr(exePath, '\\');
    if (pName) {
        strcpy_s(exeName, sizeof(exeName), pName + 1); // 跳过反斜杠
    } else {
        strcpy_s(exeName, sizeof(exeName), exePath);
    }

    // 4. 去掉文件名的 .exe 后缀（若存在），得到纯文件名 "CustomWebDAV"
    char* dot = strrchr(exeName, '.');
    if (dot && _stricmp(dot, ".exe") == 0) {
        *dot = '\0';
    }

    // 5. 获取系统“启动”文件夹路径
    char startupDir[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_STARTUP, NULL, 0, startupDir) != S_OK) return;

    // 6. 动态拼装出对应当前 exe 名称的 .lnk 快捷方式路径
    // 例如: "C:\Users\...\AppData\Roaming\...\Startup\CustomWebDAV.lnk"
    char shortcutPath[MAX_PATH];
    sprintf_s(shortcutPath, sizeof(shortcutPath), "%s\\%s.lnk", startupDir, exeName);

    // 7. 取消自启：直接删除对应动态名称的快捷方式
    if (!enable) {
        DeleteFileA(shortcutPath);
        return;
    }

    // 8. 开启自启：创建对应动态名称的快捷方式
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
