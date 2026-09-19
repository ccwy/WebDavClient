#include "config.h"
#include "logger.h"
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>

/* ======================================================================
   内部辅助：统一配置文件 config.ini 读写
   通用字段无前缀，WebDAV 字段 wd_ 前缀，SMB 字段 smb_ 前缀
   ====================================================================== */

#define MAX_CONFIG_LINES 128
#define MAX_LINE_LEN 512

/* 获取 config.ini 完整路径 */
static void GetConfigPath(char* buf, size_t bufSize) {
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';
    sprintf_s(buf, bufSize, "%s\\config.ini", workDir);
}

/* 更新配置文件：更新指定键值对，保留其他行不变
   keys[]/vals[] 为要更新的键值对，count 为数量
   若键已存在则更新值，若不存在则追加到文件末尾 */
static void UpdateConfigFile(const char* const keys[], const char* const vals[], int count) {
    char path[MAX_PATH];
    GetConfigPath(path, sizeof(path));

    char lines[MAX_CONFIG_LINES][MAX_LINE_LEN];
    int lineCount = 0;

    /* 读取现有文件内容 */
    FILE* fp = NULL;
    if (fopen_s(&fp, path, "r") == 0 && fp) {
        while (lineCount < MAX_CONFIG_LINES && fgets(lines[lineCount], MAX_LINE_LEN, fp)) {
            lines[lineCount][strcspn(lines[lineCount], "\r\n")] = 0;
            lineCount++;
        }
        fclose(fp);
    }

    /* 跟踪哪些键已在文件中找到并更新 */
    int* found = (int*)calloc(count, sizeof(int));

    /* 遍历现有行，更新匹配的键 */
    for (int i = 0; i < lineCount; i++) {
        char lineCopy[MAX_LINE_LEN];
        strcpy_s(lineCopy, sizeof(lineCopy), lines[i]);
        char* eq = strchr(lineCopy, '=');
        if (!eq) continue;
        *eq = '\0';

        for (int j = 0; j < count; j++) {
            if (strcmp(lineCopy, keys[j]) == 0) {
                sprintf_s(lines[i], MAX_LINE_LEN, "%s=%s", keys[j], vals[j]);
                found[j] = 1;
                break;
            }
        }
    }

    /* 追加文件中不存在的键 */
    for (int j = 0; j < count; j++) {
        if (!found[j] && lineCount < MAX_CONFIG_LINES) {
            sprintf_s(lines[lineCount], MAX_LINE_LEN, "%s=%s", keys[j], vals[j]);
            lineCount++;
        }
    }

    free(found);

    /* 写回文件 */
    if (fopen_s(&fp, path, "w") == 0 && fp) {
        for (int i = 0; i < lineCount; i++) {
            fprintf(fp, "%s\n", lines[i]);
        }
        fclose(fp);
    }
}

/* ======================================================================
   通用配置（无前缀键）
   ====================================================================== */

void LoadCommonConfig(CommonConfig* cfg) {
    strcpy_s(cfg->drive, sizeof(cfg->drive), "Z");
    strcpy_s(cfg->protocol, sizeof(cfg->protocol), "webdav");
    strcpy_s(cfg->volname, sizeof(cfg->volname), "Network_Disk");
    cfg->auto_start = 0;
    cfg->debug_log = 0;
    cfg->auto_hide = 0;

    /* rclone 通用 mount/VFS 参数默认值 */
    cfg->vfs_cache_mode = 3;  /* full */
    strcpy_s(cfg->dir_cache_time, sizeof(cfg->dir_cache_time), "24h");
    strcpy_s(cfg->buffer_size, sizeof(cfg->buffer_size), "64M");
    cfg->transfers = 4;
    cfg->cache_dir[0] = '\0';
    strcpy_s(cfg->vfs_cache_max_age, sizeof(cfg->vfs_cache_max_age), "24h");
    strcpy_s(cfg->vfs_read_chunk_size, sizeof(cfg->vfs_read_chunk_size), "128M");
    strcpy_s(cfg->vfs_read_chunk_size_limit, sizeof(cfg->vfs_read_chunk_size_limit), "off");
    strcpy_s(cfg->vfs_cache_max_size, sizeof(cfg->vfs_cache_max_size), "15G");

    char path[MAX_PATH];
    GetConfigPath(path, sizeof(path));

    FILE* fp = NULL;
    if (fopen_s(&fp, path, "r") != 0 || !fp) return;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;

        if (strcmp(key, "drive") == 0)                     strcpy_s(cfg->drive, sizeof(cfg->drive), val);
        else if (strcmp(key, "protocol") == 0)             strcpy_s(cfg->protocol, sizeof(cfg->protocol), val);
        else if (strcmp(key, "volname") == 0)              strcpy_s(cfg->volname, sizeof(cfg->volname), val);
        else if (strcmp(key, "auto_start") == 0)           cfg->auto_start = atoi(val);
        else if (strcmp(key, "debug_log") == 0)            cfg->debug_log = atoi(val);
        else if (strcmp(key, "auto_hide") == 0)            cfg->auto_hide = atoi(val);
        else if (strcmp(key, "vfs_cache_mode") == 0)       cfg->vfs_cache_mode = atoi(val);
        else if (strcmp(key, "dir_cache_time") == 0)       strcpy_s(cfg->dir_cache_time, sizeof(cfg->dir_cache_time), val);
        else if (strcmp(key, "buffer_size") == 0)          strcpy_s(cfg->buffer_size, sizeof(cfg->buffer_size), val);
        else if (strcmp(key, "transfers") == 0)            cfg->transfers = atoi(val);
        else if (strcmp(key, "cache_dir") == 0)            strcpy_s(cfg->cache_dir, sizeof(cfg->cache_dir), val);
        else if (strcmp(key, "vfs_cache_max_age") == 0)    strcpy_s(cfg->vfs_cache_max_age, sizeof(cfg->vfs_cache_max_age), val);
        else if (strcmp(key, "vfs_read_chunk_size") == 0)  strcpy_s(cfg->vfs_read_chunk_size, sizeof(cfg->vfs_read_chunk_size), val);
        else if (strcmp(key, "vfs_read_chunk_size_limit") == 0) strcpy_s(cfg->vfs_read_chunk_size_limit, sizeof(cfg->vfs_read_chunk_size_limit), val);
        else if (strcmp(key, "vfs_cache_max_size") == 0)   strcpy_s(cfg->vfs_cache_max_size, sizeof(cfg->vfs_cache_max_size), val);
    }
    fclose(fp);
}

void SaveCommonConfig(const CommonConfig* cfg) {
    char v_auto_start[8], v_debug_log[8], v_auto_hide[8], v_vfs_cache_mode[8], v_transfers[8];
    sprintf_s(v_auto_start, sizeof(v_auto_start), "%d", cfg->auto_start);
    sprintf_s(v_debug_log, sizeof(v_debug_log), "%d", cfg->debug_log);
    sprintf_s(v_auto_hide, sizeof(v_auto_hide), "%d", cfg->auto_hide);
    sprintf_s(v_vfs_cache_mode, sizeof(v_vfs_cache_mode), "%d", cfg->vfs_cache_mode);
    sprintf_s(v_transfers, sizeof(v_transfers), "%d", cfg->transfers);

    const char* keys[] = {
        "drive", "protocol", "volname", "auto_start", "debug_log", "auto_hide",
        "vfs_cache_mode", "dir_cache_time", "buffer_size", "transfers", "cache_dir",
        "vfs_cache_max_age", "vfs_read_chunk_size", "vfs_read_chunk_size_limit", "vfs_cache_max_size"
    };
    const char* vals[] = {
        cfg->drive, cfg->protocol, cfg->volname, v_auto_start, v_debug_log, v_auto_hide,
        v_vfs_cache_mode, cfg->dir_cache_time, cfg->buffer_size, v_transfers, cfg->cache_dir,
        cfg->vfs_cache_max_age, cfg->vfs_read_chunk_size, cfg->vfs_read_chunk_size_limit, cfg->vfs_cache_max_size
    };

    UpdateConfigFile(keys, vals, 15);
}

/* ======================================================================
   WebDAV 专属配置（wd_ 前缀键）
   ====================================================================== */

void LoadWebDavConfig(WebDavConfig* cfg) {
    strcpy_s(cfg->host, sizeof(cfg->host), "192.168.5.100");
    strcpy_s(cfg->port, sizeof(cfg->port), "50055");
    strcpy_s(cfg->path, sizeof(cfg->path), "/music");
    strcpy_s(cfg->user, sizeof(cfg->user), "www");
    strcpy_s(cfg->pass, sizeof(cfg->pass), "www");
    cfg->ssl = 0;
    strcpy_s(cfg->vendor, sizeof(cfg->vendor), "other");
    cfg->headers[0] = '\0';
    cfg->no_check_cert = 0;

    char path[MAX_PATH];
    GetConfigPath(path, sizeof(path));

    FILE* fp = NULL;
    if (fopen_s(&fp, path, "r") != 0 || !fp) return;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;

        if (strcmp(key, "wd_host") == 0)              strcpy_s(cfg->host, sizeof(cfg->host), val);
        else if (strcmp(key, "wd_port") == 0)         strcpy_s(cfg->port, sizeof(cfg->port), val);
        else if (strcmp(key, "wd_path") == 0)         strcpy_s(cfg->path, sizeof(cfg->path), val);
        else if (strcmp(key, "wd_user") == 0)         strcpy_s(cfg->user, sizeof(cfg->user), val);
        else if (strcmp(key, "wd_pass") == 0)         strcpy_s(cfg->pass, sizeof(cfg->pass), val);
        else if (strcmp(key, "wd_ssl") == 0)          cfg->ssl = atoi(val);
        else if (strcmp(key, "wd_vendor") == 0)       strcpy_s(cfg->vendor, sizeof(cfg->vendor), val);
        else if (strcmp(key, "wd_headers") == 0)      strcpy_s(cfg->headers, sizeof(cfg->headers), val);
        else if (strcmp(key, "wd_no_check_cert") == 0) cfg->no_check_cert = atoi(val);
    }
    fclose(fp);
}

void SaveWebDavConfig(const WebDavConfig* cfg) {
    char v_ssl[8], v_no_check_cert[8];
    sprintf_s(v_ssl, sizeof(v_ssl), "%d", cfg->ssl);
    sprintf_s(v_no_check_cert, sizeof(v_no_check_cert), "%d", cfg->no_check_cert);

    const char* keys[] = {
        "wd_host", "wd_port", "wd_path", "wd_user", "wd_pass", "wd_ssl",
        "wd_vendor", "wd_headers", "wd_no_check_cert"
    };
    const char* vals[] = {
        cfg->host, cfg->port, cfg->path, cfg->user, cfg->pass, v_ssl,
        cfg->vendor, cfg->headers, v_no_check_cert
    };

    UpdateConfigFile(keys, vals, 9);
}

/* ======================================================================
   SMB 专属配置（smb_ 前缀键）
   ====================================================================== */

void LoadSmbConfig(SmbConfig* cfg) {
    strcpy_s(cfg->server, sizeof(cfg->server), "192.168.5.100");
    strcpy_s(cfg->port, sizeof(cfg->port), "445");
    strcpy_s(cfg->share, sizeof(cfg->share), "share");
    strcpy_s(cfg->user, sizeof(cfg->user), "guest");
    cfg->pass[0] = '\0';
    strcpy_s(cfg->domain, sizeof(cfg->domain), "WORKGROUP");
    cfg->spn[0] = '\0';
    cfg->use_kerberos = 0;
    strcpy_s(cfg->idle_timeout, sizeof(cfg->idle_timeout), "1m0s");
    cfg->hide_special_share = 1;
    cfg->case_insensitive = 1;

    char path[MAX_PATH];
    GetConfigPath(path, sizeof(path));

    FILE* fp = NULL;
    if (fopen_s(&fp, path, "r") != 0 || !fp) return;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;

        if (strcmp(key, "smb_server") == 0)              strcpy_s(cfg->server, sizeof(cfg->server), val);
        else if (strcmp(key, "smb_port") == 0)           strcpy_s(cfg->port, sizeof(cfg->port), val);
        else if (strcmp(key, "smb_share") == 0)          strcpy_s(cfg->share, sizeof(cfg->share), val);
        else if (strcmp(key, "smb_user") == 0)           strcpy_s(cfg->user, sizeof(cfg->user), val);
        else if (strcmp(key, "smb_pass") == 0)           strcpy_s(cfg->pass, sizeof(cfg->pass), val);
        else if (strcmp(key, "smb_domain") == 0)         strcpy_s(cfg->domain, sizeof(cfg->domain), val);
        else if (strcmp(key, "smb_spn") == 0)            strcpy_s(cfg->spn, sizeof(cfg->spn), val);
        else if (strcmp(key, "smb_use_kerberos") == 0)   cfg->use_kerberos = atoi(val);
        else if (strcmp(key, "smb_idle_timeout") == 0)   strcpy_s(cfg->idle_timeout, sizeof(cfg->idle_timeout), val);
        else if (strcmp(key, "smb_hide_special_share") == 0) cfg->hide_special_share = atoi(val);
        else if (strcmp(key, "smb_case_insensitive") == 0)   cfg->case_insensitive = atoi(val);
    }
    fclose(fp);
}

void SaveSmbConfig(const SmbConfig* cfg) {
    char v_use_kerberos[8], v_hide_special_share[8], v_case_insensitive[8];
    sprintf_s(v_use_kerberos, sizeof(v_use_kerberos), "%d", cfg->use_kerberos);
    sprintf_s(v_hide_special_share, sizeof(v_hide_special_share), "%d", cfg->hide_special_share);
    sprintf_s(v_case_insensitive, sizeof(v_case_insensitive), "%d", cfg->case_insensitive);

    const char* keys[] = {
        "smb_server", "smb_port", "smb_share", "smb_user", "smb_pass", "smb_domain",
        "smb_spn", "smb_use_kerberos", "smb_idle_timeout", "smb_hide_special_share", "smb_case_insensitive"
    };
    const char* vals[] = {
        cfg->server, cfg->port, cfg->share, cfg->user, cfg->pass, cfg->domain,
        cfg->spn, v_use_kerberos, cfg->idle_timeout, v_hide_special_share, v_case_insensitive
    };

    UpdateConfigFile(keys, vals, 11);
}

/* ======================================================================
   开机自启
   ====================================================================== */

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