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
    SC_HANDLE hSCM = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) return 0;
    
    SC_HANDLE hService = OpenServiceA(hSCM, "WinFsp.Device", SERVICE_QUERY_STATUS);
    if (!hService) {
        hService = OpenServiceA(hSCM, "WinFsp", SERVICE_QUERY_STATUS);
    }
    
    if (hService) {
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        return 1; 
    }
    CloseServiceHandle(hSCM);
    return 0;
}

int WaitUntilWinFspInstalled(HANDLE hProcess, DWORD maxTimeoutMs) {
    DWORD startTime = GetTickCount();
    LogMessage("INFO", "Waiting dynamically for WinFsp EXE installation to complete...");
    
    while (1) {
        if (CheckWinFspInstalled()) {
            LogMessage("INFO", "WinFsp service detected successfully during polling.");
            return 1; 
        }

        DWORD waitResult = WaitForSingleObject(hProcess, 1000);
        if (waitResult == WAIT_OBJECT_0) {
            Sleep(1000); 
            return CheckWinFspInstalled();
        }

        if (GetTickCount() - startTime > maxTimeoutMs) {
            LogMessage("WARN", "WinFsp installation timeout reached.");
            return CheckWinFspInstalled();
        }
    }
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

    int resRcloneId = 0, resFspId = 0;

    if (majorVer >= 10) {
        if (is64) { resRcloneId = IDR_WIN10_X64_RCLONE; resFspId = IDR_WIN10_X64_WINFSP; }
        else      { resRcloneId = IDR_WIN10_X86_RCLONE; resFspId = IDR_WIN10_X86_WINFSP; }
    } else {
        if (is64) { resRcloneId = IDR_WIN7_X64_RCLONE; resFspId = IDR_WIN7_X64_WINFSP; }
        else      { resRcloneId = IDR_WIN7_X86_RCLONE; resFspId = IDR_WIN7_X86_WINFSP; }
    }

    char tempDir[MAX_PATH], workDir[MAX_PATH];
    GetTempPathA(sizeof(tempDir), tempDir);
    sprintf_s(workDir, sizeof(workDir), "%sWebDavClientEnv", tempDir);
    CreateDirectoryA(workDir, NULL);

    char rcloneDest[MAX_PATH], fspDest[MAX_PATH];
    sprintf_s(rcloneDest, sizeof(rcloneDest), "%s\\rclone.exe", workDir);
    sprintf_s(fspDest, sizeof(fspDest), "%s\\winfsp.exe", workDir);

    if (!ExtractResourceToFile(resRcloneId, rcloneDest)) {
        LogMessage("ERROR", "Failed to extract rclone.exe from PE resources.");
        return 0;
    }

    if (!CheckWinFspInstalled()) {
        LogMessage("WARN", "WinFsp missing. Initiating EXE deployment process...");
        if (!ExtractResourceToFile(resFspId, fspDest)) {
            LogMessage("ERROR", "Failed to extract winfsp.exe from PE resources.");
            return 0;
        }

        char cmdLine[MAX_PATH * 2];
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = {0};

        // NSIS Silent Install uses /S (case sensitive)
        sprintf_s(cmdLine, sizeof(cmdLine), "\"%s\" /S", fspDest);
        LogMessage("INFO", "Executing silent install: %s", cmdLine);

        if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            int success = WaitUntilWinFspInstalled(pi.hProcess, 180000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            if (!success) {
                LogMessage("WARN", "Silent install failed or blocked. Attempting interactive EXE installer.");
                ZeroMemory(&pi, sizeof(pi));
                sprintf_s(cmdLine, sizeof(cmdLine), "\"%s\"", fspDest);
                if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                    while (!CheckWinFspInstalled()) {
                        if (WaitForSingleObject(pi.hProcess, 1000) == WAIT_OBJECT_0) break;
                    }
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                }
            }
        }

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