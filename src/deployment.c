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

// 多重健壮性验证：注册表 + 服务状态 + 文件路径
int CheckWinFspInstalled() {
    // 1. 检查注册表项
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WinFsp", 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS ||
        RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WinFsp", 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS) {
        char installDir[MAX_PATH];
        DWORD bufSize = sizeof(installDir);
        DWORD type = REG_SZ;
        if (RegQueryValueExA(hKey, "InstallDir", NULL, &type, (LPBYTE)installDir, &bufSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            if (strlen(installDir) > 0) {
                return 1; 
            }
        }
        RegCloseKey(hKey);
    }

    // 2. 检查 SCM 服务状态
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

    // 3. 直接检查核心 DLL 文件是否存在
    if (GetFileAttributesA("C:\\Program Files\\WinFsp\\bin\\winfsp-x64.dll") != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesA("C:\\Program Files (x86)\\WinFsp\\bin\\winfsp-x64.dll") != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesA("C:\\Program Files\\WinFsp\\bin\\winfsp-x86.dll") != INVALID_FILE_ATTRIBUTES) {
        return 1; 
    }

    return 0;
}

int ExtractResourceToFile(int resourceId, const char* outputPath) {
    HRSRC hRes = FindResourceA(NULL, MAKEINTRESOURCEA(resourceId), "BIN");
    if (!hRes) {
        LogMessage("ERROR", "Failed to find resource ID: %d", resourceId);
        return 0;
    }
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

    int resRcloneId = 0;
    int resMsiId = 0;

    if (majorVer >= 10) {
        resRcloneId = is64 ? IDR_WIN10_RCLONE_X64 : IDR_WIN10_RCLONE_X86;
        resMsiId = IDR_WIN10_WINFSP_MSI;
    } else {
        resRcloneId = is64 ? IDR_WIN7_RCLONE_X64 : IDR_WIN7_RCLONE_X86;
        resMsiId = IDR_WIN7_WINFSP_MSI;
    }

    char tempDir[MAX_PATH], workDir[MAX_PATH];
    GetTempPathA(sizeof(tempDir), tempDir);
    sprintf_s(workDir, sizeof(workDir), "%sWebDavClientEnv", tempDir);
    CreateDirectoryA(workDir, NULL);

    // 创建 lang 子目录
    char langDir[MAX_PATH];
    sprintf_s(langDir, sizeof(langDir), "%s\\lang", workDir);
    CreateDirectoryA(langDir, NULL);

    char rcloneDest[MAX_PATH], msiDest[MAX_PATH], enDest[MAX_PATH], zhDest[MAX_PATH];
    sprintf_s(rcloneDest, sizeof(rcloneDest), "%s\\rclone.exe", workDir);
    sprintf_s(msiDest, sizeof(msiDest), "%s\\winfsp.msi", workDir);
    sprintf_s(enDest, sizeof(enDest), "%s\\en.ini", langDir);
    sprintf_s(zhDest, sizeof(zhDest), "%s\\zh.ini", langDir);

    // 1. 释放 Rclone 核心程序
    if (!ExtractResourceToFile(resRcloneId, rcloneDest)) {
        LogMessage("ERROR", "Failed to extract rclone.exe from PE resources.");
        return 0;
    }

    // 2. 释放多语言 INI 文件
    ExtractResourceToFile(IDR_LANG_EN, enDest);
    ExtractResourceToFile(IDR_LANG_ZH, zhDest);
    LogMessage("INFO", "Language files extracted to temp environment successfully.");

    // 3. 检查 WinFsp，若未安装则直接弹出交互式安装界面并等待完成
    if (!CheckWinFspInstalled()) {
        LogMessage("WARN", "WinFsp missing. Launching interactive MSI installer...");
        if (!ExtractResourceToFile(resMsiId, msiDest)) {
            LogMessage("ERROR", "Failed to extract winfsp.msi from PE resources.");
            return 0;
        }

        char cmdLine[MAX_PATH * 2];
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };

        // 取消静默参数，直接以标准交互式界面运行
        sprintf_s(cmdLine, sizeof(cmdLine), "msiexec.exe /i \"%s\"", msiDest);
        LogMessage("INFO", "Executing interactive installer command: %s", cmdLine);

        if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            LogMessage("INFO", "Waiting for user to complete WinFsp installation in the GUI...");

            // 阻塞等待：死循环检测安装进程是否结束，并结合注册表/服务检测
            while (1) {
                DWORD waitResult = WaitForSingleObject(pi.hProcess, 1000);
                if (waitResult == WAIT_OBJECT_0) {
                    // 安装进程已退出
                    LogMessage("INFO", "WinFsp MSI installer process exited.");
                    break;
                }
                
                // 如果用户在中途已经提前完成了安装，也可以实时捕捉
                if (CheckWinFspInstalled()) {
                    LogMessage("INFO", "WinFsp installation successfully detected.");
                }
            }

            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            LogMessage("ERROR", "Failed to launch MSI installer process. Error code: %lu", GetLastError());
            return 0;
        }

        // 安装结束后进行最终验证
        if (!CheckWinFspInstalled()) {
            LogMessage("ERROR", "WinFsp verification failed post-installation.");
            return 0;
        }
        LogMessage("INFO", "WinFsp successfully installed and verified.");
    } else {
        LogMessage("INFO", "WinFsp is already installed on the system.");
    }

    strcpy_s(outRclonePath, pathSize, rcloneDest);
    return 1;
}