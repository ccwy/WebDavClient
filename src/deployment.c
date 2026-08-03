#include <windows.h>
#include <stdio.h>
#include "deployment.h"
#include "logger.h"
#include "../res/resource.h"

int Is64BitSystem() {
    BOOL bIsWow64 = FALSE;
    typedef BOOL(WINAPI* LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);
    LPFN_ISWOW64PROCESS fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress(
        GetModuleHandleA("kernel32.dll"), "IsWow64Process");
    if (fnIsWow64Process) fnIsWow64Process(GetCurrentProcess(), &bIsWow64);
#if defined(_WIN64)
    return 1;
#else
    return bIsWow64;
#endif
}

int GetWindowsMajorVersion() {
    NTSTATUS(WINAPI * RtlGetVersion)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW rovi = { 0 };
    rovi.dwOSVersionInfoSize = sizeof(rovi);
    *(FARPROC*)&RtlGetVersion = GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
    if (RtlGetVersion) {
        RtlGetVersion(&rovi);
        return (int)rovi.dwMajorVersion;
    }
    return 6;
}

int CheckWinFspInstalled() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WinFsp", 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS ||
        RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WinFsp", 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS) {
        char installDir[MAX_PATH];
        DWORD bufSize = sizeof(installDir);
        DWORD type = REG_SZ;
        if (RegQueryValueExA(hKey, "InstallDir", NULL, &type, (LPBYTE)installDir, &bufSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            if (strlen(installDir) > 0) return 1; 
        }
        RegCloseKey(hKey);
    }

    SC_HANDLE hSCM = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (hSCM) {
        SC_HANDLE hService = OpenServiceA(hSCM, "WinFsp", SERVICE_QUERY_STATUS);
        if (hService) {
            CloseServiceHandle(hService);
            CloseServiceHandle(hSCM);
            return 1; 
        }
        CloseServiceHandle(hSCM);
    }

    if (GetFileAttributesA("C:\\Program Files\\WinFsp\\bin\\winfsp-x64.dll") != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesA("C:\\Program Files (x86)\\WinFsp\\bin\\winfsp-x64.dll") != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesA("C:\\Program Files\\WinFsp\\bin\\winfsp-x86.dll") != INVALID_FILE_ATTRIBUTES) {
        return 1; 
    }

    return 0;
}

int ExtractResourceToFile(int resourceId, const char* outputPath) {
    HRSRC hRes = FindResourceA(NULL, MAKEINTRESOURCEA(resourceId), "BIN");
    if (!hRes) return 0;
    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData) return 0;
    LPVOID pData = LockResource(hData);
    DWORD dwSize = SizeofResource(NULL, hRes);
    if (!pData || dwSize == 0) return 0;

    HANDLE hFile = CreateFileA(outputPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return 0;

    DWORD dwWritten = 0;
    WriteFile(hFile, pData, dwSize, &dwWritten, NULL);
    CloseHandle(hFile);
    return (dwWritten == dwSize);
}

int InitializeEnvironment(char* outRclonePath, size_t pathSize) {
    int majorVer = GetWindowsMajorVersion();
    int is64 = Is64BitSystem();
    LogMessage("INFO", "OS check: Windows Major=%d, x64=%d", majorVer, is64);

    int resRcloneId = 0, resMsiId = 0;
    if (majorVer >= 10) {
        resRcloneId = is64 ? IDR_WIN10_RCLONE_X64 : IDR_WIN10_RCLONE_X86;
        resMsiId = IDR_WIN10_WINFSP_MSI;
    } else {
        resRcloneId = is64 ? IDR_WIN7_RCLONE_X64 : IDR_WIN7_RCLONE_X86;
        resMsiId = IDR_WIN7_WINFSP_MSI;
    }

    // 获取程序当前所在的目录作为工作目录
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    // 创建 lang 目录
    char langDir[MAX_PATH];
    sprintf_s(langDir, sizeof(langDir), "%s\\lang", workDir);
    CreateDirectoryA(langDir, NULL);

    char rcloneDest[MAX_PATH], msiDest[MAX_PATH], enDest[MAX_PATH], zhDest[MAX_PATH];
    sprintf_s(rcloneDest, sizeof(rcloneDest), "%s\\rclone.exe", workDir);
    sprintf_s(msiDest, sizeof(msiDest), "%s\\winfsp.msi", workDir);
    sprintf_s(enDest, sizeof(enDest), "%s\\en.ini", langDir);
    sprintf_s(zhDest, sizeof(zhDest), "%s\\zh.ini", langDir);

    // 1. 释放文件到当前目录
    if (!ExtractResourceToFile(resRcloneId, rcloneDest)) {
        LogMessage("ERROR", "Failed to extract rclone.exe.");
        return 0;
    }
    ExtractResourceToFile(IDR_LANG_EN, enDest);
    ExtractResourceToFile(IDR_LANG_ZH, zhDest);

    // 2. 检查并安装 WinFsp
    if (!CheckWinFspInstalled()) {
        LogMessage("WARN", "WinFsp missing. Launching interactive MSI installer...");
        if (!ExtractResourceToFile(resMsiId, msiDest)) {
            LogMessage("ERROR", "Failed to extract winfsp.msi.");
            return 0;
        }

        char cmdLine[MAX_PATH * 2];
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };

        sprintf_s(cmdLine, sizeof(cmdLine), "msiexec.exe /i \"%s\"", msiDest);
        if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            while (1) {
                if (WaitForSingleObject(pi.hProcess, 1000) == WAIT_OBJECT_0) break;
                if (CheckWinFspInstalled()) break;
            }
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }

        if (!CheckWinFspInstalled()) {
            LogMessage("ERROR", "WinFsp verification failed post-installation.");
            return 0;
        }
        LogMessage("INFO", "WinFsp successfully installed and verified.");
    } else {
        LogMessage("INFO", "WinFsp is already installed.");
    }

    strcpy_s(outRclonePath, pathSize, rcloneDest);
    return 1;
}