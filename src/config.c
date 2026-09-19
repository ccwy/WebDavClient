#include "config.h"
#include "logger.h"
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>

void LoadCommonConfig(CommonConfig* cfg) {
    strcpy_s(cfg->drive, sizeof(cfg->drive), "Z");
    strcpy_s(cfg->protocol, sizeof(cfg->protocol), "webdav");
    strcpy_s(cfg->volname, sizeof(cfg->volname), "Network_Disk");
    cfg->auto_start = 0;
    cfg->debug_log = 0;
    cfg->auto_hide = 0;

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

        if (strcmp(key, "drive") == 0)          strcpy_s(cfg->drive, sizeof(cfg->drive), val);
        else if (strcmp(key, "protocol") == 0)    strcpy_s(cfg->protocol, sizeof(cfg->protocol), val);
        else if (strcmp(key, "volname") == 0)      strcpy_s(cfg->volname, sizeof(cfg->volname), val);
        else if (strcmp(key, "auto_start") == 0) cfg->auto_start = atoi(val);
        else if (strcmp(key, "debug_log") == 0)  cfg->debug_log = atoi(val);
        else if (strcmp(key, "auto_hide") == 0)  cfg->auto_hide = atoi(val);
    }
    fclose(fp);
}

void SaveCommonConfig(const CommonConfig* cfg) {
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    char iniPath[MAX_PATH];
    sprintf_s(iniPath, sizeof(iniPath), "%s\\config.ini", workDir);

    FILE* fp = NULL;
    if (fopen_s(&fp, iniPath, "w") != 0 || !fp) return;

    fprintf(fp, "drive=%s\n", cfg->drive);
    fprintf(fp, "protocol=%s\n", cfg->protocol);
    fprintf(fp, "volname=%s\n", cfg->volname);
    fprintf(fp, "auto_start=%d\n", cfg->auto_start);
    fprintf(fp, "debug_log=%d\n", cfg->debug_log);
    fprintf(fp, "auto_hide=%d\n", cfg->auto_hide);

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