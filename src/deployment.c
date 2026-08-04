#include <windows.h>
#include <stdio.h>
#include "deployment.h"
#include "logger.h"
#include "i18n.h"
#include "../res/resource.h"

// 进度窗口全局句柄及控件
static HWND g_hProgressWnd = NULL;
static HWND g_hStatusText = NULL;
static HFONT g_hProgressFont = NULL;

static WCHAR g_currentStatus[512] = L"Initializing...";
static WCHAR g_windowTitle[128] = L"WebDAV Client Initialization";

#define WM_UPDATE_STATUS (WM_USER + 100)

static LRESULT CALLBACK ProgressWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hProgressFont = CreateFontW(
            -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei"
        );

        g_hStatusText = CreateWindowExW(
            0, L"STATIC", g_currentStatus,
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
            15, 20, 390, 50,
            hwnd, NULL, GetModuleHandle(NULL), NULL
        );

        if (g_hStatusText && g_hProgressFont) {
            SendMessageW(g_hStatusText, WM_SETFONT, (WPARAM)g_hProgressFont, TRUE);
        }
        break;
    }
    case WM_UPDATE_STATUS: {
        if (g_hStatusText && lParam) {
            SetWindowTextW(g_hStatusText, (const WCHAR*)lParam);
            InvalidateRect(g_hStatusText, NULL, TRUE);
            UpdateWindow(g_hStatusText);
        }
        break;
    }
    case WM_DESTROY: {
        if (g_hProgressFont) {
            DeleteObject(g_hProgressFont);
            g_hProgressFont = NULL;
        }
        break;
    }
    case WM_CLOSE:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void UpdateStatusW(const WCHAR* format, ...) {
    if (!format) return;

    WCHAR safeFmt[512] = { 0 };
    wcscpy_s(safeFmt, sizeof(safeFmt) / sizeof(WCHAR), format);

    for (size_t i = 0; safeFmt[i] != L'\0'; i++) {
        if (safeFmt[i] == L'%' && safeFmt[i + 1] == L'S') {
            safeFmt[i + 1] = L's';
        }
    }

    WCHAR wBuf[512] = { 0 };
    va_list args;
    va_start(args, format);
    vswprintf_s(wBuf, sizeof(wBuf) / sizeof(WCHAR), safeFmt, args);
    va_end(args);

    wcscpy_s(g_currentStatus, sizeof(g_currentStatus) / sizeof(WCHAR), wBuf);

    char ansiBuf[512];
    WideCharToMultiByte(CP_ACP, 0, wBuf, -1, ansiBuf, sizeof(ansiBuf), NULL, NULL);
    LogMessage("INFO", "%s", ansiBuf);

    if (g_hProgressWnd && g_hStatusText) {
        SendMessageW(g_hProgressWnd, WM_UPDATE_STATUS, 0, (LPARAM)wBuf);
    }
}

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

static void GetWindowsVersion(int* major, int* minor) {
    NTSTATUS(WINAPI * RtlGetVersion)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW rovi = { 0 };
    rovi.dwOSVersionInfoSize = sizeof(rovi);
    *(FARPROC*)&RtlGetVersion = GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
    if (RtlGetVersion) {
        RtlGetVersion(&rovi);
        *major = (int)rovi.dwMajorVersion;
        *minor = (int)rovi.dwMinorVersion;
    } else {
        *major = 6;
        *minor = 1;
    }
}

static int CheckVCRedistInstalled(int is64) {
    HKEY hKey;
    const char* subKeys[] = {
        "SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64",
        "SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x86",
        "SOFTWARE\\WOW6432Node\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64",
        "SOFTWARE\\WOW6432Node\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x86"
    };

    for (int i = 0; i < 4; i++) {
        REGSAM samDesired = KEY_READ | KEY_WOW64_64KEY;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subKeys[i], 0, samDesired, &hKey) == ERROR_SUCCESS) {
            DWORD installed = 0;
            DWORD size = sizeof(installed);
            if (RegQueryValueExA(hKey, "Installed", NULL, NULL, (LPBYTE)&installed, &size) == ERROR_SUCCESS && installed == 1) {
                RegCloseKey(hKey);
                return 1;
            }
            RegCloseKey(hKey);
        }
        samDesired = KEY_READ | KEY_WOW64_32KEY;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subKeys[i], 0, samDesired, &hKey) == ERROR_SUCCESS) {
            DWORD installed = 0;
            DWORD size = sizeof(installed);
            if (RegQueryValueExA(hKey, "Installed", NULL, NULL, (LPBYTE)&installed, &size) == ERROR_SUCCESS && installed == 1) {
                RegCloseKey(hKey);
                return 1;
            }
            RegCloseKey(hKey);
        }
    }
    return 0;
}

static int CheckKB4474419Installed(int is64) {
    HKEY hKey;
    const char* packagesKey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\Packages";
    REGSAM samDesired = KEY_READ | (is64 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY);
    
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, packagesKey, 0, samDesired, &hKey) == ERROR_SUCCESS) {
        DWORD index = 0;
        char keyName[512];
        DWORD nameSize = sizeof(keyName);
        while (RegEnumKeyExA(hKey, index, keyName, &nameSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            if (_stricmp(keyName, "Package_for_KB4474419~31bf3856ad364e35~amd64~~6.1.1.3") == 0 ||
                strstr(keyName, "KB4474419") != NULL) {
                RegCloseKey(hKey);
                return 1;
            }
            index++;
            nameSize = sizeof(keyName);
            memset(keyName, 0, sizeof(keyName));
        }
        RegCloseKey(hKey);
    }
    return 0;
}

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

// 增强资源提取函数：加入详细日志，防止资源找不到时静默跳过
int ExtractResourceToFile(int resourceId, const char* outputPath) {
    HRSRC hRes = FindResourceA(NULL, MAKEINTRESOURCEA(resourceId), "BIN");
    if (!hRes) {
        LogMessage("ERROR", "FindResourceA failed for ID %d, GetLastError=%lu. (Did you run WIN10 build on Win7?)", resourceId, GetLastError());
        return 0;
    }
    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData) {
        LogMessage("ERROR", "LoadResource failed for ID %d", resourceId);
        return 0;
    }
    LPVOID pData = LockResource(hData);
    DWORD dwSize = SizeofResource(NULL, hRes);
    if (!pData || dwSize == 0) {
        LogMessage("ERROR", "LockResource or size zero for ID %d", resourceId);
        return 0;
    }

    HANDLE hFile = CreateFileA(outputPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        LogMessage("ERROR", "CreateFileA failed for %s, Error=%lu", outputPath, GetLastError());
        return 0;
    }

    DWORD dwWritten = 0;
    WriteFile(hFile, pData, dwSize, &dwWritten, NULL);
    CloseHandle(hFile);
    
    if (dwWritten != dwSize) {
        LogMessage("ERROR", "WriteFile incomplete for %s: written %lu of %lu", outputPath, dwWritten, dwSize);
        return 0;
    }
    return 1;
}

typedef struct {
    char outRclonePath[MAX_PATH];
    size_t pathSize;
    int success;
} InitParams;

// 后台工作线程：加入强制安装调试
static DWORD WINAPI InitWorkerThread(LPVOID lpParam) {
    InitParams* params = (InitParams*)lpParam;
    int majorVer = 6, minorVer = 1;
    GetWindowsVersion(&majorVer, &minorVer);
    int is64 = Is64BitSystem();

    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    // 【第 1 步】VC++ 2015-2022
    if (!CheckVCRedistInstalled(is64)) {
        UpdateStatusW(L"%ls", TR("STR_INIT_VC_INSTALL"));
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
        }
    }

#ifdef TARGET_WIN7
    // ==================================================================
    // 【第 2 步】Win7 专属：KB3140245 (强制弹出交互安装以供测试)
    // ==================================================================
    LogMessage("INFO", "Checking Win7 TLS 1.2 status... Result=%d", CheckWin7TlsEnabled());
    
    // 如果你想强制每次都弹出安装，可以把 !CheckWin7TlsEnabled() 改为 1 (即强制执行)
    if (!CheckWin7TlsEnabled()) {
        UpdateStatusW(L"%ls", TR("STR_INIT_MISSING_TLS"));
        char msuDest[MAX_PATH];
        sprintf_s(msuDest, sizeof(msuDest), "%s\\kb3140245.msu", workDir);
        
        LogMessage("INFO", "Extracting KB3140245 to: %s", msuDest);
        if (ExtractResourceToFile(IDR_KB3140245, msuDest)) {
            char cmdLine[MAX_PATH * 2];
            sprintf_s(cmdLine, sizeof(cmdLine), "wusa.exe \"%s\"", msuDest);
            LogMessage("INFO", "Launching command: %s", cmdLine);
            
            STARTUPINFOA si = { sizeof(si) };
            PROCESS_INFORMATION pi = { 0 };

            if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                LogMessage("INFO", "KB3140245 process launched successfully. Waiting for user interaction...");
                WaitForSingleObject(pi.hProcess, INFINITE);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                LogMessage("INFO", "KB3140245 installation window closed.");
            } else {
                LogMessage("ERROR", "Failed to launch KB3140245 process. Error=%lu", GetLastError());
            }
            DeleteFileA(msuDest);
        } else {
            LogMessage("ERROR", "Failed to extract KB3140245.msu from resources!");
        }
    }

    // ==================================================================
    // 【第 3 步】Win7 专属：KB4474419 (强制弹出交互安装以供测试)
    // ==================================================================
    LogMessage("INFO", "Checking Win7 KB4474419 status... Result=%d", CheckKB4474419Installed(is64));

    if (!CheckKB4474419Installed(is64)) {
        UpdateStatusW(L"%ls", TR("STR_INIT_PATCH_KB4474419"));
        char msuDest[MAX_PATH];
        sprintf_s(msuDest, sizeof(msuDest), "%s\\kb4474419.msu", workDir);

        LogMessage("INFO", "Extracting KB4474419 to: %s", msuDest);
        if (ExtractResourceToFile(IDR_KB4474419, msuDest)) {
            char cmdLine[MAX_PATH * 2];
            sprintf_s(cmdLine, sizeof(cmdLine), "wusa.exe \"%s\"", msuDest);
            LogMessage("INFO", "Launching command: %s", cmdLine);

            STARTUPINFOA si = { sizeof(si) };
            PROCESS_INFORMATION pi = { 0 };

            if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                LogMessage("INFO", "KB4474419 process launched successfully. Waiting for user interaction...");
                WaitForSingleObject(pi.hProcess, INFINITE);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                LogMessage("INFO", "KB4474419 installation window closed.");
            } else {
                LogMessage("ERROR", "Failed to launch KB4474419 process. Error=%lu", GetLastError());
            }
            DeleteFileA(msuDest);
        } else {
            LogMessage("ERROR", "Failed to extract KB4474419.msu from resources!");
        }
    }
#endif

    // 【第 4 步】WinFsp 驱动
    if (!CheckWinFspInstalled()) {
        UpdateStatusW(L"%ls", TR("STR_INIT_WINFSP_INSTALL"));
        char msiDest[MAX_PATH];
        sprintf_s(msiDest, sizeof(msiDest), "%s\\winfsp.msi", workDir);

        if (!ExtractResourceToFile(IDR_WINFSP_MSI, msiDest)) {
            UpdateStatusW(L"%ls", TR("STR_INIT_ERR_WINFSP"));
            params->success = 0;
            PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
            return 0;
        }

        char cmdLine[MAX_PATH * 2];
        sprintf_s(cmdLine, sizeof(cmdLine), "msiexec.exe /i \"%s\"", msiDest);
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };

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
            UpdateStatusW(L"%ls", TR("STR_INIT_ERR_WINFSP"));
            params->success = 0;
            PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
            return 0;
        }
    }

    // 【第 5 步】释放 Rclone 主程序
    UpdateStatusW(L"%ls", TR("STR_INIT_EXTRACT_RCLONE"));
    char rcloneDest[MAX_PATH];
    sprintf_s(rcloneDest, sizeof(rcloneDest), "%s\\rclone.exe", workDir);

    if (!ExtractResourceToFile(IDR_RCLONE, rcloneDest)) {
        params->success = 0;
        PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
        return 0;
    }

    strcpy_s(params->outRclonePath, params->pathSize, rcloneDest);
    params->success = 1;

    PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
    return 0;
}

int InitializeEnvironment(char* outRclonePath, size_t pathSize) {
    HINSTANCE hInstance = GetModuleHandle(NULL);
    int majorVer = 6, minorVer = 1;
    GetWindowsVersion(&majorVer, &minorVer);
    int is64 = Is64BitSystem();

    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';
    SetCurrentDirectoryA(workDir);

    char langDir[MAX_PATH];
    sprintf_s(langDir, sizeof(langDir), "%s\\lang", workDir);
    CreateDirectoryA(langDir, NULL);

    char enDest[MAX_PATH], zhDest[MAX_PATH];
    sprintf_s(enDest, sizeof(enDest), "%s\\en.ini", langDir);
    sprintf_s(zhDest, sizeof(zhDest), "%s\\zh.ini", langDir);

    ExtractResourceToFile(IDR_LANG_EN, enDest);
    ExtractResourceToFile(IDR_LANG_ZH, zhDest);

    LANGID langId = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(langId) == LANG_CHINESE) {
        InitI18n("zh");
    } else {
        InitI18n("en");
    }

    int vcInstalled = CheckVCRedistInstalled(is64);
#ifdef TARGET_WIN7
    int kb3140Installed = CheckWin7TlsEnabled();
    int kb4474Installed = CheckKB4474419Installed(is64);
#else
    int kb3140Installed = 1;
    int kb4474Installed = 1;
#endif
    int winfspInstalled = CheckWinFspInstalled();
    
    char rcloneDest[MAX_PATH];
    sprintf_s(rcloneDest, sizeof(rcloneDest), "%s\\rclone.exe", workDir);
    int rcloneExists = (GetFileAttributesA(rcloneDest) != INVALID_FILE_ATTRIBUTES);

    if (vcInstalled && kb3140Installed && kb4474Installed && winfspInstalled && rcloneExists) {
        LogMessage("INFO", "All environment dependencies are ready. Skipping initialization progress window.");
        strcpy_s(outRclonePath, pathSize, rcloneDest);
        return 1;
    }

    const wchar_t* wTitle = TR("STR_INIT_TITLE");
    const wchar_t* wLoading = TR("STR_INIT_LOADING");

    if (wTitle && wTitle[0] != L'\0') {
        wcscpy_s(g_windowTitle, sizeof(g_windowTitle) / sizeof(WCHAR), wTitle);
    }
    if (wLoading && wLoading[0] != L'\0') {
        wcscpy_s(g_currentStatus, sizeof(g_currentStatus) / sizeof(WCHAR), wLoading);
    }

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = ProgressWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DownloadProgressClassW";
    wc.hCursor = LoadCursor(NULL, IDC_WAIT);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int dlgWidth = 420;
    int dlgHeight = 130;
    int dlgX = (screenWidth - dlgWidth) / 2;
    int dlgY = (screenHeight - dlgHeight) / 2;

    g_hProgressWnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"DownloadProgressClassW", g_windowTitle,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        dlgX, dlgY, dlgWidth, dlgHeight,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hProgressWnd) return 0;

    ShowWindow(g_hProgressWnd, SW_SHOW);
    UpdateWindow(g_hProgressWnd);

    InitParams params = { 0 };
    params.pathSize = pathSize;
    params.success = 0;

    HANDLE hThread = CreateThread(NULL, 0, InitWorkerThread, &params, 0, NULL);
    if (!hThread) {
        DestroyWindow(g_hProgressWnd);
        UnregisterClassW(L"DownloadProgressClassW", hInstance);
        return 0;
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.hwnd == g_hProgressWnd && msg.message == WM_CLOSE) {
            DestroyWindow(g_hProgressWnd);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    UnregisterClassW(L"DownloadProgressClassW", hInstance);

    if (params.success) {
        strcpy_s(outRclonePath, pathSize, params.outRclonePath);
    }

    return params.success;
}