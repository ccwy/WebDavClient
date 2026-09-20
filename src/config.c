#include "config.h"
#include "logger.h"
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================================================================
   内部辅助 — 使用 Windows INI API 读写 config.ini
   GetPrivateProfileString / WritePrivateProfileString 自动按 [section] 分段
   ====================================================================== */

/* 获取 config.ini 完整路径（调用方需保证 buf 足够大） */
static void GetConfigPath(char* buf, size_t bufSize) {
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';
    sprintf_s(buf, bufSize, "%s\\config.ini", workDir);
}

/* 缓存配置文件路径，避免反复计算 */
static const char* ConfigPath(void) {
    static char s_path[MAX_PATH] = { 0 };
    if (s_path[0] == '\0') {
        GetConfigPath(s_path, sizeof(s_path));
    }
    return s_path;
}

/* ======================================================================
   全局配置 [global]
   ====================================================================== */

void LoadGlobalConfig(GlobalConfig* cfg) {
    const char* path = ConfigPath();
    cfg->auto_start = GetPrivateProfileIntA("global", "auto_start", 0, path);
    cfg->debug_log  = GetPrivateProfileIntA("global", "debug_log", 0, path);
    cfg->auto_hide  = GetPrivateProfileIntA("global", "auto_hide", 0, path);
}

void SaveGlobalConfig(const GlobalConfig* cfg) {
    const char* path = ConfigPath();
    char v[8];
    sprintf_s(v, sizeof(v), "%d", cfg->auto_start);
    WritePrivateProfileStringA("global", "auto_start", v, path);
    sprintf_s(v, sizeof(v), "%d", cfg->debug_log);
    WritePrivateProfileStringA("global", "debug_log", v, path);
    sprintf_s(v, sizeof(v), "%d", cfg->auto_hide);
    WritePrivateProfileStringA("global", "auto_hide", v, path);
}

/* ======================================================================
   连接通用配置 [conn_N]
   ====================================================================== */

/* 设置 ConnectionConfig 的默认值 */
static void SetConnectionDefaults(ConnectionConfig* cfg) {
    cfg->id[0] = '\0';
    strcpy_s(cfg->name, sizeof(cfg->name), "New Connection");
    strcpy_s(cfg->protocol, sizeof(cfg->protocol), "webdav");
    strcpy_s(cfg->drive, sizeof(cfg->drive), "Z");
    strcpy_s(cfg->volname, sizeof(cfg->volname), "Network_Disk");

    cfg->vfs_cache_mode = 3;  /* full */
    strcpy_s(cfg->dir_cache_time, sizeof(cfg->dir_cache_time), "24h");
    strcpy_s(cfg->buffer_size, sizeof(cfg->buffer_size), "64M");
    cfg->transfers = 4;
    cfg->cache_dir[0] = '\0';
    strcpy_s(cfg->vfs_cache_max_age, sizeof(cfg->vfs_cache_max_age), "24h");
    strcpy_s(cfg->vfs_read_chunk_size, sizeof(cfg->vfs_read_chunk_size), "128M");
    strcpy_s(cfg->vfs_read_chunk_size_limit, sizeof(cfg->vfs_read_chunk_size_limit), "off");
    strcpy_s(cfg->vfs_cache_max_size, sizeof(cfg->vfs_cache_max_size), "15G");
}

void LoadConnectionConfig(ConnectionConfig* cfg, const char* section) {
    const char* path = ConfigPath();

    /* 保存 section 副本，因为 section 可能指向 cfg->id，
       而 SetConnectionDefaults 会清空 cfg->id，导致 section 变成空字符串 */
    char sectionCopy[64];
    strcpy_s(sectionCopy, sizeof(sectionCopy), section);

    SetConnectionDefaults(cfg);

    strcpy_s(cfg->id, sizeof(cfg->id), sectionCopy);

    char buf[256];
    GetPrivateProfileStringA(sectionCopy, "name", cfg->name, cfg->name, sizeof(cfg->name), path);
    GetPrivateProfileStringA(sectionCopy, "protocol", cfg->protocol, cfg->protocol, sizeof(cfg->protocol), path);
    GetPrivateProfileStringA(sectionCopy, "drive", cfg->drive, cfg->drive, sizeof(cfg->drive), path);
    GetPrivateProfileStringA(sectionCopy, "volname", cfg->volname, cfg->volname, sizeof(cfg->volname), path);

    cfg->vfs_cache_mode = GetPrivateProfileIntA(sectionCopy, "vfs_cache_mode", cfg->vfs_cache_mode, path);
    GetPrivateProfileStringA(sectionCopy, "dir_cache_time", cfg->dir_cache_time, cfg->dir_cache_time, sizeof(cfg->dir_cache_time), path);
    GetPrivateProfileStringA(sectionCopy, "buffer_size", cfg->buffer_size, cfg->buffer_size, sizeof(cfg->buffer_size), path);
    cfg->transfers = GetPrivateProfileIntA(sectionCopy, "transfers", cfg->transfers, path);
    GetPrivateProfileStringA(sectionCopy, "cache_dir", cfg->cache_dir, cfg->cache_dir, sizeof(cfg->cache_dir), path);
    GetPrivateProfileStringA(sectionCopy, "vfs_cache_max_age", cfg->vfs_cache_max_age, cfg->vfs_cache_max_age, sizeof(cfg->vfs_cache_max_age), path);
    GetPrivateProfileStringA(sectionCopy, "vfs_read_chunk_size", cfg->vfs_read_chunk_size, cfg->vfs_read_chunk_size, sizeof(cfg->vfs_read_chunk_size), path);
    GetPrivateProfileStringA(sectionCopy, "vfs_read_chunk_size_limit", cfg->vfs_read_chunk_size_limit, cfg->vfs_read_chunk_size_limit, sizeof(cfg->vfs_read_chunk_size_limit), path);
    GetPrivateProfileStringA(sectionCopy, "vfs_cache_max_size", cfg->vfs_cache_max_size, cfg->vfs_cache_max_size, sizeof(cfg->vfs_cache_max_size), path);
}

void SaveConnectionConfig(const ConnectionConfig* cfg) {
    const char* path = ConfigPath();
    const char* section = cfg->id;

    WritePrivateProfileStringA(section, "name", cfg->name, path);
    WritePrivateProfileStringA(section, "protocol", cfg->protocol, path);
    WritePrivateProfileStringA(section, "drive", cfg->drive, path);
    WritePrivateProfileStringA(section, "volname", cfg->volname, path);

    char v[8];
    sprintf_s(v, sizeof(v), "%d", cfg->vfs_cache_mode);
    WritePrivateProfileStringA(section, "vfs_cache_mode", v, path);
    WritePrivateProfileStringA(section, "dir_cache_time", cfg->dir_cache_time, path);
    WritePrivateProfileStringA(section, "buffer_size", cfg->buffer_size, path);
    sprintf_s(v, sizeof(v), "%d", cfg->transfers);
    WritePrivateProfileStringA(section, "transfers", v, path);
    WritePrivateProfileStringA(section, "cache_dir", cfg->cache_dir, path);
    WritePrivateProfileStringA(section, "vfs_cache_max_age", cfg->vfs_cache_max_age, path);
    WritePrivateProfileStringA(section, "vfs_read_chunk_size", cfg->vfs_read_chunk_size, path);
    WritePrivateProfileStringA(section, "vfs_read_chunk_size_limit", cfg->vfs_read_chunk_size_limit, path);
    WritePrivateProfileStringA(section, "vfs_cache_max_size", cfg->vfs_cache_max_size, path);
}

/* ======================================================================
   应用完整配置加载/保存
   ====================================================================== */

void LoadAppConfig(AppConfig* cfg) {
    const char* path = ConfigPath();

    /* 加载全局配置 */
    LoadGlobalConfig(&cfg->global);

    /* 枚举所有 section 名 */
    char sectionBuf[4096];
    GetPrivateProfileSectionNamesA(sectionBuf, sizeof(sectionBuf), path);

    cfg->count = 0;
    cfg->next_conn_id = 1;

    char* p = sectionBuf;
    while (*p && cfg->count < MAX_CONNECTIONS) {
        /* 只处理以 "conn_" 开头的 section */
        if (strncmp(p, "conn_", 5) == 0) {
            LoadConnectionConfig(&cfg->connections[cfg->count], p);
            cfg->count++;

            /* 更新 next_conn_id，取 conn_N 中最大的 N+1 */
            int idNum = atoi(p + 5);
            if (idNum >= cfg->next_conn_id) {
                cfg->next_conn_id = idNum + 1;
            }
        }
        p += strlen(p) + 1;
    }
}

void SaveAppConfig(const AppConfig* cfg) {
    const char* path = ConfigPath();

    /* 保存全局配置 */
    SaveGlobalConfig(&cfg->global);

    /* 保存每个连接的通用配置（协议专属配置由各处理器自行保存） */
    for (int i = 0; i < cfg->count; i++) {
        SaveConnectionConfig(&cfg->connections[i]);
    }

    /* 保存 next_conn_id 到 [global] */
    char v[16];
    sprintf_s(v, sizeof(v), "%d", cfg->next_conn_id);
    WritePrivateProfileStringA("global", "next_conn_id", v, path);
}

/* ======================================================================
   连接 CRUD
   ====================================================================== */

int AddConnection(AppConfig* cfg, const ConnectionConfig* conn) {
    if (cfg->count >= MAX_CONNECTIONS) return -1;
    cfg->connections[cfg->count] = *conn;
    cfg->count++;
    return cfg->count - 1;
}

int RemoveConnection(AppConfig* cfg, const char* id) {
    int idx = FindConnectionById(cfg, id);
    if (idx < 0) return -1;

    /* 从 INI 文件中删除整个 section */
    const char* path = ConfigPath();
    WritePrivateProfileStringA(id, NULL, NULL, path);

    /* 从数组中移除 */
    for (int i = idx; i < cfg->count - 1; i++) {
        cfg->connections[i] = cfg->connections[i + 1];
    }
    cfg->count--;
    return 0;
}

int FindConnectionById(const AppConfig* cfg, const char* id) {
    for (int i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->connections[i].id, id) == 0) return i;
    }
    return -1;
}

void GenerateConnectionId(const AppConfig* cfg, char* outId, size_t maxLen) {
    /* 使用 cfg->next_conn_id 生成唯一 ID */
    sprintf_s(outId, maxLen, "conn_%d", cfg->next_conn_id);
}

/* ======================================================================
   WebDAV 专属配置 [conn_N] — 键名无前缀，与通用字段同 section
   ====================================================================== */

void LoadWebDavConfig(const char* section, WebDavConfig* cfg) {
    const char* path = ConfigPath();

    /* 默认值 */
    strcpy_s(cfg->host, sizeof(cfg->host), "192.168.5.100");
    strcpy_s(cfg->port, sizeof(cfg->port), "50055");
    strcpy_s(cfg->path, sizeof(cfg->path), "/music");
    strcpy_s(cfg->user, sizeof(cfg->user), "www");
    strcpy_s(cfg->pass, sizeof(cfg->pass), "www");
    cfg->ssl = 0;
    strcpy_s(cfg->vendor, sizeof(cfg->vendor), "other");
    cfg->headers[0] = '\0';
    cfg->no_check_cert = 0;

    /* 从 INI 读取 */
    GetPrivateProfileStringA(section, "host", cfg->host, cfg->host, sizeof(cfg->host), path);
    GetPrivateProfileStringA(section, "port", cfg->port, cfg->port, sizeof(cfg->port), path);
    GetPrivateProfileStringA(section, "path", cfg->path, cfg->path, sizeof(cfg->path), path);
    GetPrivateProfileStringA(section, "user", cfg->user, cfg->user, sizeof(cfg->user), path);
    GetPrivateProfileStringA(section, "pass", cfg->pass, cfg->pass, sizeof(cfg->pass), path);
    cfg->ssl = GetPrivateProfileIntA(section, "ssl", cfg->ssl, path);
    GetPrivateProfileStringA(section, "vendor", cfg->vendor, cfg->vendor, sizeof(cfg->vendor), path);
    GetPrivateProfileStringA(section, "headers", cfg->headers, cfg->headers, sizeof(cfg->headers), path);
    cfg->no_check_cert = GetPrivateProfileIntA(section, "no_check_cert", cfg->no_check_cert, path);
}

void SaveWebDavConfig(const char* section, const WebDavConfig* cfg) {
    const char* path = ConfigPath();
    char v[8];

    WritePrivateProfileStringA(section, "host", cfg->host, path);
    WritePrivateProfileStringA(section, "port", cfg->port, path);
    WritePrivateProfileStringA(section, "path", cfg->path, path);
    WritePrivateProfileStringA(section, "user", cfg->user, path);
    WritePrivateProfileStringA(section, "pass", cfg->pass, path);
    sprintf_s(v, sizeof(v), "%d", cfg->ssl);
    WritePrivateProfileStringA(section, "ssl", v, path);
    WritePrivateProfileStringA(section, "vendor", cfg->vendor, path);
    WritePrivateProfileStringA(section, "headers", cfg->headers, path);
    sprintf_s(v, sizeof(v), "%d", cfg->no_check_cert);
    WritePrivateProfileStringA(section, "no_check_cert", v, path);
}

/* ======================================================================
   SMB 专属配置 [conn_N]
   ====================================================================== */

void LoadSmbConfig(const char* section, SmbConfig* cfg) {
    const char* path = ConfigPath();

    strcpy_s(cfg->server, sizeof(cfg->server), "192.168.5.100");
    strcpy_s(cfg->port, sizeof(cfg->port), "445");
    cfg->share[0] = '\0';
    strcpy_s(cfg->user, sizeof(cfg->user), "guest");
    cfg->pass[0] = '\0';
    strcpy_s(cfg->domain, sizeof(cfg->domain), "WORKGROUP");
    cfg->spn[0] = '\0';
    cfg->use_kerberos = 0;
    strcpy_s(cfg->idle_timeout, sizeof(cfg->idle_timeout), "1m0s");
    cfg->hide_special_share = 1;
    cfg->case_insensitive = 1;

    GetPrivateProfileStringA(section, "server", cfg->server, cfg->server, sizeof(cfg->server), path);
    GetPrivateProfileStringA(section, "port", cfg->port, cfg->port, sizeof(cfg->port), path);
    GetPrivateProfileStringA(section, "share", cfg->share, cfg->share, sizeof(cfg->share), path);
    GetPrivateProfileStringA(section, "user", cfg->user, cfg->user, sizeof(cfg->user), path);
    GetPrivateProfileStringA(section, "pass", cfg->pass, cfg->pass, sizeof(cfg->pass), path);
    GetPrivateProfileStringA(section, "domain", cfg->domain, cfg->domain, sizeof(cfg->domain), path);
    GetPrivateProfileStringA(section, "spn", cfg->spn, cfg->spn, sizeof(cfg->spn), path);
    cfg->use_kerberos = GetPrivateProfileIntA(section, "use_kerberos", cfg->use_kerberos, path);
    GetPrivateProfileStringA(section, "idle_timeout", cfg->idle_timeout, cfg->idle_timeout, sizeof(cfg->idle_timeout), path);
    cfg->hide_special_share = GetPrivateProfileIntA(section, "hide_special_share", cfg->hide_special_share, path);
    cfg->case_insensitive = GetPrivateProfileIntA(section, "case_insensitive", cfg->case_insensitive, path);
}

void SaveSmbConfig(const char* section, const SmbConfig* cfg) {
    const char* path = ConfigPath();
    char v[8];

    WritePrivateProfileStringA(section, "server", cfg->server, path);
    WritePrivateProfileStringA(section, "port", cfg->port, path);
    WritePrivateProfileStringA(section, "share", cfg->share, path);
    WritePrivateProfileStringA(section, "user", cfg->user, path);
    WritePrivateProfileStringA(section, "pass", cfg->pass, path);
    WritePrivateProfileStringA(section, "domain", cfg->domain, path);
    WritePrivateProfileStringA(section, "spn", cfg->spn, path);
    sprintf_s(v, sizeof(v), "%d", cfg->use_kerberos);
    WritePrivateProfileStringA(section, "use_kerberos", v, path);
    WritePrivateProfileStringA(section, "idle_timeout", cfg->idle_timeout, path);
    sprintf_s(v, sizeof(v), "%d", cfg->hide_special_share);
    WritePrivateProfileStringA(section, "hide_special_share", v, path);
    sprintf_s(v, sizeof(v), "%d", cfg->case_insensitive);
    WritePrivateProfileStringA(section, "case_insensitive", v, path);
}

/* ======================================================================
   SFTP 专属配置 [conn_N]
   ====================================================================== */

void LoadSftpConfig(const char* section, SftpConfig* cfg) {
    const char* path = ConfigPath();

    strcpy_s(cfg->host, sizeof(cfg->host), "192.168.5.100");
    strcpy_s(cfg->port, sizeof(cfg->port), "22");
    strcpy_s(cfg->user, sizeof(cfg->user), "root");
    cfg->pass[0] = '\0';
    cfg->key_file[0] = '\0';
    cfg->key_file_pass[0] = '\0';
    cfg->shell_type[0] = '\0';
    strcpy_s(cfg->idle_timeout, sizeof(cfg->idle_timeout), "1m0s");
    cfg->use_insecure_cipher = 0;
    cfg->disable_hashcheck = 0;
    cfg->set_modtime = 0;
    cfg->skip_links = 0;

    GetPrivateProfileStringA(section, "host", cfg->host, cfg->host, sizeof(cfg->host), path);
    GetPrivateProfileStringA(section, "port", cfg->port, cfg->port, sizeof(cfg->port), path);
    GetPrivateProfileStringA(section, "user", cfg->user, cfg->user, sizeof(cfg->user), path);
    GetPrivateProfileStringA(section, "pass", cfg->pass, cfg->pass, sizeof(cfg->pass), path);
    GetPrivateProfileStringA(section, "key_file", cfg->key_file, cfg->key_file, sizeof(cfg->key_file), path);
    GetPrivateProfileStringA(section, "key_file_pass", cfg->key_file_pass, cfg->key_file_pass, sizeof(cfg->key_file_pass), path);
    GetPrivateProfileStringA(section, "shell_type", cfg->shell_type, cfg->shell_type, sizeof(cfg->shell_type), path);
    GetPrivateProfileStringA(section, "idle_timeout", cfg->idle_timeout, cfg->idle_timeout, sizeof(cfg->idle_timeout), path);
    cfg->use_insecure_cipher = GetPrivateProfileIntA(section, "use_insecure_cipher", cfg->use_insecure_cipher, path);
    cfg->disable_hashcheck = GetPrivateProfileIntA(section, "disable_hashcheck", cfg->disable_hashcheck, path);
    cfg->set_modtime = GetPrivateProfileIntA(section, "set_modtime", cfg->set_modtime, path);
    cfg->skip_links = GetPrivateProfileIntA(section, "skip_links", cfg->skip_links, path);
}

void SaveSftpConfig(const char* section, const SftpConfig* cfg) {
    const char* path = ConfigPath();
    char v[8];

    WritePrivateProfileStringA(section, "host", cfg->host, path);
    WritePrivateProfileStringA(section, "port", cfg->port, path);
    WritePrivateProfileStringA(section, "user", cfg->user, path);
    WritePrivateProfileStringA(section, "pass", cfg->pass, path);
    WritePrivateProfileStringA(section, "key_file", cfg->key_file, path);
    WritePrivateProfileStringA(section, "key_file_pass", cfg->key_file_pass, path);
    WritePrivateProfileStringA(section, "shell_type", cfg->shell_type, path);
    WritePrivateProfileStringA(section, "idle_timeout", cfg->idle_timeout, path);
    sprintf_s(v, sizeof(v), "%d", cfg->use_insecure_cipher);
    WritePrivateProfileStringA(section, "use_insecure_cipher", v, path);
    sprintf_s(v, sizeof(v), "%d", cfg->disable_hashcheck);
    WritePrivateProfileStringA(section, "disable_hashcheck", v, path);
    sprintf_s(v, sizeof(v), "%d", cfg->set_modtime);
    WritePrivateProfileStringA(section, "set_modtime", v, path);
    sprintf_s(v, sizeof(v), "%d", cfg->skip_links);
    WritePrivateProfileStringA(section, "skip_links", v, path);
}

/* ======================================================================
   FTP 专属配置 [conn_N]
   ====================================================================== */

void LoadFtpConfig(const char* section, FtpConfig* cfg) {
    const char* path = ConfigPath();

    strcpy_s(cfg->host, sizeof(cfg->host), "192.168.5.100");
    strcpy_s(cfg->port, sizeof(cfg->port), "21");
    strcpy_s(cfg->user, sizeof(cfg->user), "anonymous");
    cfg->pass[0] = '\0';
    cfg->tls = 0;
    cfg->explicit_tls = 0;
    cfg->no_check_certificate = 0;
    strcpy_s(cfg->idle_timeout, sizeof(cfg->idle_timeout), "1m0s");
    strcpy_s(cfg->concurrency, sizeof(cfg->concurrency), "0");

    GetPrivateProfileStringA(section, "host", cfg->host, cfg->host, sizeof(cfg->host), path);
    GetPrivateProfileStringA(section, "port", cfg->port, cfg->port, sizeof(cfg->port), path);
    GetPrivateProfileStringA(section, "user", cfg->user, cfg->user, sizeof(cfg->user), path);
    GetPrivateProfileStringA(section, "pass", cfg->pass, cfg->pass, sizeof(cfg->pass), path);
    cfg->tls = GetPrivateProfileIntA(section, "tls", cfg->tls, path);
    cfg->explicit_tls = GetPrivateProfileIntA(section, "explicit_tls", cfg->explicit_tls, path);
    cfg->no_check_certificate = GetPrivateProfileIntA(section, "no_check_certificate", cfg->no_check_certificate, path);
    GetPrivateProfileStringA(section, "idle_timeout", cfg->idle_timeout, cfg->idle_timeout, sizeof(cfg->idle_timeout), path);
    GetPrivateProfileStringA(section, "concurrency", cfg->concurrency, cfg->concurrency, sizeof(cfg->concurrency), path);
}

void SaveFtpConfig(const char* section, const FtpConfig* cfg) {
    const char* path = ConfigPath();
    char v[8];

    WritePrivateProfileStringA(section, "host", cfg->host, path);
    WritePrivateProfileStringA(section, "port", cfg->port, path);
    WritePrivateProfileStringA(section, "user", cfg->user, path);
    WritePrivateProfileStringA(section, "pass", cfg->pass, path);
    sprintf_s(v, sizeof(v), "%d", cfg->tls);
    WritePrivateProfileStringA(section, "tls", v, path);
    sprintf_s(v, sizeof(v), "%d", cfg->explicit_tls);
    WritePrivateProfileStringA(section, "explicit_tls", v, path);
    sprintf_s(v, sizeof(v), "%d", cfg->no_check_certificate);
    WritePrivateProfileStringA(section, "no_check_certificate", v, path);
    WritePrivateProfileStringA(section, "idle_timeout", cfg->idle_timeout, path);
    WritePrivateProfileStringA(section, "concurrency", cfg->concurrency, path);
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