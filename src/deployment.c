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

// 检查 VC++ 2015-2022 运行库是否已安装
static int CheckVCRedistInstalled(int is64) {
    HKEY hKey;
    const char* subKey = is64 ? 
        "SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64" : 
        "SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x86";
    
    REGSAM samDesired = KEY_READ | (is64 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subKey, 0, samDesired, &hKey) == ERROR_SUCCESS) {
        DWORD installed = 0;
        DWORD size = sizeof(installed);
        if (RegQueryValueExA(hKey, "Installed", NULL, NULL, (LPBYTE)&installed, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (installed == 1);
        }
        RegCloseKey(hKey);
    }
    return 0;
}

// 检查 Windows 7 下是否安装了 KB4474419 补丁
static int CheckKB4474419Installed(int is64) {
    HKEY hKey;
    const char* packagesKey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\Packages";
    REGSAM samDesired = KEY_READ | (is64 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY);
    
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, packagesKey, 0, samDesired, &hKey) == ERROR_SUCCESS) {
        DWORD index = 0;
        char keyName[512];
        DWORD nameSize = sizeof(keyName);
        while (RegEnumKeyExA(hKey, index, keyName, &nameSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            if (strstr(keyName, "KB4474419") != NULL) {
                RegCloseKey(hKey);
                return 1;
            }
            index++;
            nameSize = sizeof(keyName);
        }
        RegCloseKey(hKey);
    }
    return 0;
}

// 检查 Windows 7 是否已经开启了 TLS 1.2 客户端协议
static int CheckWin7TlsEnabled() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
        "SYSTEM\\CurrentControlSet\\Control\\SecurityProviders\\SCHANNEL\\Protocols\\TLS 1.2\\Client", 
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD enabled = 0;
        DWORD size = sizeof(enabled);
        if (RegQueryValueExA(hKey, "Enabled", NULL, NULL, (LPBYTE)&enabled, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            if (enabled == 1) return 1;
        }
        RegCloseKey(hKey);
    }
    return 0;
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

    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    // ------------------------------------------------------------------
    // 步骤 1：优先检测并交互式安装 VC++ 2015-2022
    // ------------------------------------------------------------------
    if (!CheckVCRedistInstalled(is64)) {
        LogMessage("WARN", "Visual C++ 2015-2022 Redistributable missing. Launching interactive installer...");
        char vcDest[MAX_PATH];
        sprintf_s(vcDest, sizeof(vcDest), "%s\\vc_redist.exe", workDir);

        if (ExtractResourceToFile(IDR_VC_2015_2022, vcDest)) {
            char cmdLine[MAX_PATH * 2];
            sprintf_s(cmdLine, sizeof(cmdLine), "\"%s\"", vcDest);

            STARTUPINFOA si = { sizeof(si) };
            PROCESS_INFORMATION pi = { 0 };

            if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, INFINITE);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
            DeleteFileA(vcDest);
            LogMessage("INFO", "Visual C++ 2015-2022 installation completed.");
        } else {
            LogMessage("ERROR", "Failed to extract Visual C++ installer.");
        }
    } else {
        LogMessage("INFO", "Visual C++ 2015-2022 is already installed.");
    }

    // ------------------------------------------------------------------
    // 步骤 2：针对 Windows 7 (MajorVersion == 6) 检测并安装 KB4474419
    // ------------------------------------------------------------------
    if (majorVer == 6) {
        if (!CheckKB4474419Installed(is64)) {
            LogMessage("WARN", "Windows 7 KB4474419 patch missing. Launching interactive installer...");
            char msuDest[MAX_PATH];
            sprintf_s(msuDest, sizeof(msuDest), "%s\\kb4474419.msu", workDir);

            if (ExtractResourceToFile(IDR_WIN7_KB4474419, msuDest)) {
                char cmdLine[MAX_PATH * 2];
                sprintf_s(cmdLine, sizeof(cmdLine), "wusa.exe \"%s\"", msuDest);

                STARTUPINFOA si = { sizeof(si) };
                PROCESS_INFORMATION pi = { 0 };

                if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                    WaitForSingleObject(pi.hProcess, INFINITE);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                }
                DeleteFileA(msuDest);
                LogMessage("INFO", "KB4474419 installation completed.");
            } else {
                LogMessage("ERROR", "Failed to extract KB4474419 patch.");
            }
        } else {
            LogMessage("INFO", "Windows 7 KB4474419 patch is already installed.");
        }

        // ------------------------------------------------------------------
        // 步骤 3：针对 Windows 7 检测并安装 KB3140245
        // ------------------------------------------------------------------
        if (!CheckWin7TlsEnabled()) {
            LogMessage("WARN", "Windows 7 TLS 1.2 support missing. Launching interactive KB3140245 patch installer...");
            
            char msuDest[MAX_PATH];
            sprintf_s(msuDest, sizeof(msuDest), "%s\\kb3140245.msu", workDir);
            
            if (ExtractResourceToFile(IDR_WIN7_KB3140245, msuDest)) {
                char cmdLine[MAX_PATH * 2];
                sprintf_s(cmdLine, sizeof(cmdLine), "wusa.exe \"%s\"", msuDest);

                STARTUPINFOA si = { sizeof(si) };
                PROCESS_INFORMATION pi = { 0 };

                if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                    WaitForSingleObject(pi.hProcess, INFINITE);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                }
                DeleteFileA(msuDest);
                LogMessage("INFO", "KB3140245 installation process completed.");
            } else {
                LogMessage("ERROR", "Failed to extract KB3140245 patch.");
            }
        } else {
            LogMessage("INFO", "Windows 7 TLS 1.2 is already enabled.");
        }
    }

    // ------------------------------------------------------------------
    // 步骤 4：检测并安装 WinFsp
    // ------------------------------------------------------------------
    int resMsiId = (majorVer >= 10) ? IDR_WIN10_WINFSP_MSI : IDR_WIN7_WINFSP_MSI;
    if (!CheckWinFspInstalled()) {
        LogMessage("WARN", "WinFsp missing. Launching interactive MSI installer...");
        char msiDest[MAX_PATH];
        sprintf_s(msiDest, sizeof(msiDest), "%s\\winfsp.msi", workDir);

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

        DeleteFileA(msiDest);

        if (!CheckWinFspInstalled()) {
            LogMessage("ERROR", "WinFsp verification failed post-installation.");
            return 0;
        }
        LogMessage("INFO", "WinFsp successfully installed and verified.");
    } else {
        LogMessage("INFO", "WinFsp is already installed.");
    }

    // ------------------------------------------------------------------
    // 步骤 5：释放 Rclone 与语言包文件
    // ------------------------------------------------------------------
    int resRcloneId = (majorVer >= 10) ? IDR_WIN10_RCLONE : IDR_WIN7_RCLONE;

    char langDir[MAX_PATH];
    sprintf_s(langDir, sizeof(langDir), "%s\\lang", workDir);
    CreateDirectoryA(langDir, NULL);

    char rcloneDest[MAX_PATH], enDest[MAX_PATH], zhDest[MAX_PATH];
    sprintf_s(rcloneDest, sizeof(rcloneDest), "%s\\rclone.exe", workDir);
    sprintf_s(enDest, sizeof(enDest), "%s\\en.ini", langDir);
    sprintf_s(zhDest, sizeof(zhDest), "%s\\zh.ini", langDir);

    if (!ExtractResourceToFile(resRcloneId, rcloneDest)) {
        LogMessage("ERROR", "Failed to extract rclone.exe.");
        return 0;
    }
    ExtractResourceToFile(IDR_LANG_EN, enDest);
    ExtractResourceToFile(IDR_LANG_ZH, zhDest);

    strcpy_s(outRclonePath, pathSize, rcloneDest);
    return 1;
}