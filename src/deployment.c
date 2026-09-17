#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <shellapi.h>
#include "deployment.h"
#include "logger.h"
#include "i18n.h"
#include "../res/resource.h"

// 使用 RtlGetVersion 获取真实系统版本（不受兼容性清单影响）
static BOOL GetRealOSVersion(DWORD* major, DWORD* minor) {
    HMODULE hNtDll = GetModuleHandleA("ntdll.dll");
    if (!hNtDll) return FALSE;

    typedef LONG(WINAPI* RtlGetVersionPtr)(OSVERSIONINFOEXW*);
    RtlGetVersionPtr RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hNtDll, "RtlGetVersion");
    if (!RtlGetVersion) return FALSE;

    OSVERSIONINFOEXW osvi = { 0 };
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);
    if (RtlGetVersion(&osvi) != 0) return FALSE;

    *major = osvi.dwMajorVersion;
    *minor = osvi.dwMinorVersion;
    return TRUE;
}

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

static int Is64BitSystem() {
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

// KB4474419 检测逻辑：匹配组件服务注册表中的包名及 WMI HotFix
#ifdef TARGET_WIN7
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
                _stricmp(keyName, "Package_for_KB4474419~31bf3856ad364e35~x86~~6.1.1.3") == 0 ||
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

    // 备用检测：WMI Win32_QuickFixEngineering
    // 如果 CBS 枚举遗漏（如补丁被月度汇总取代），通过注册表 HotFix 键检测
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\HotFix\\KB4474419",
        0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return 1;
    }
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\HotFix\\KB4474419",
        0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return 1;
    }

    return 0;
}

#endif // TARGET_WIN7

static int CheckWinFspInstalled() {
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

static int ExtractResourceToFile(int resourceId, const char* outputPath) {
    HRSRC hRes = FindResourceA(NULL, MAKEINTRESOURCEA(resourceId), "BIN");
    if (!hRes) {
        LogMessage("ERROR", "FindResourceA failed for ID %d, GetLastError=%lu.", resourceId, GetLastError());
        return 0;
    }
    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData) {
        LogMessage("ERROR", "LoadResource failed for ID %d, GetLastError=%lu.", resourceId, GetLastError());
        return 0;
    }
    LPVOID pData = LockResource(hData);
    DWORD dwSize = SizeofResource(NULL, hRes);
    if (!pData || dwSize == 0) {
        LogMessage("ERROR", "LockResource/SizeofResource failed for ID %d, pData=%p, dwSize=%lu.", resourceId, pData, dwSize);
        return 0;
    }

    HANDLE hFile = CreateFileA(outputPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return 0;

    DWORD dwWritten = 0;
    WriteFile(hFile, pData, dwSize, &dwWritten, NULL);
    CloseHandle(hFile);
    return (dwWritten == dwSize);
}

// 纯交互式提权执行安装（不使用静默参数，由标准安装向导引导）
static int RunElevatedProcess(const char* file, const char* parameters) {
    SHELLEXECUTEINFOA sei = { sizeof(sei) };
    sei.lpVerb = "runas";
    sei.lpFile = file;
    sei.lpParameters = parameters;
    sei.nShow = SW_NORMAL;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    LogMessage("INFO", "Executing elevated interactive process: file=%s, params=%s", file, parameters ? parameters : "NULL");

    if (ShellExecuteExA(&sei)) {
        if (sei.hProcess) {
            WaitForSingleObject(sei.hProcess, INFINITE);
            CloseHandle(sei.hProcess);
            LogMessage("INFO", "Elevated process finished successfully.");
            return 1;
        }
    } else {
        DWORD err = GetLastError();
        LogMessage("ERROR", "ShellExecuteExA failed to launch elevated process. Error=%lu", err);
    }
    return 0;
}

typedef struct {
    char outRclonePath[MAX_PATH];
    size_t pathSize;
    int success;
} InitParams;

static DWORD WINAPI InitWorkerThread(LPVOID lpParam) {
    InitParams* params = (InitParams*)lpParam;
    int is64 = Is64BitSystem();

    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

#ifdef TARGET_WIN7
    // ==================================================================
    // 【第 1 步】Win7 专属：KB4474419 (SHA-2 签名) 交互式安装
    // ==================================================================
    if (!CheckKB4474419Installed(is64)) {
        UpdateStatusW(L"%ls", TR("STR_INIT_PATCH_KB4474419"));
        char msuDest[MAX_PATH];
        sprintf_s(msuDest, sizeof(msuDest), "%s\\kb4474419.msu", workDir);

        if (ExtractResourceToFile(IDR_KB4474419, msuDest)) {
            char paramsStr[MAX_PATH + 32];
            sprintf_s(paramsStr, sizeof(paramsStr), "\"%s\"", msuDest);
            RunElevatedProcess("wusa.exe", paramsStr);
            DeleteFileA(msuDest);
        }

        // 验证补丁是否真正安装成功（用户可能取消 UAC 或安装失败）
        if (!CheckKB4474419Installed(is64)) {
            UpdateStatusW(L"%ls", TR("STR_INIT_ERR_KB4474419"));
            LogMessage("ERROR", "KB4474419 installation failed or was cancelled by user.");
            MessageBoxW(g_hProgressWnd, TR("STR_INIT_ERR_KB4474419"), TR("STR_INIT_TITLE"), MB_OK | MB_ICONERROR);
            params->success = 0;
            PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
            return 0;
        }


    }
#endif

    // ==================================================================
    // 【第 2 步】WinFsp 驱动交互式安装
    // ==================================================================
    if (!CheckWinFspInstalled()) {
        UpdateStatusW(L"%ls", TR("STR_INIT_WINFSP_INSTALL"));
        char msiDest[MAX_PATH];
        sprintf_s(msiDest, sizeof(msiDest), "%s\\winfsp.msi", workDir);

        if (!ExtractResourceToFile(IDR_WINFSP_MSI, msiDest)) {
            UpdateStatusW(L"%ls", TR("STR_INIT_ERR_WINFSP"));
            MessageBoxW(g_hProgressWnd, TR("STR_INIT_ERR_WINFSP"), TR("STR_INIT_TITLE"), MB_OK | MB_ICONERROR);
            params->success = 0;
            PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
            return 0;
        }

        char paramsStr[MAX_PATH + 32];
        sprintf_s(paramsStr, sizeof(paramsStr), "/i \"%s\"", msiDest);
        RunElevatedProcess("msiexec.exe", paramsStr);
        DeleteFileA(msiDest);

        if (!CheckWinFspInstalled()) {
            UpdateStatusW(L"%ls", TR("STR_INIT_ERR_WINFSP"));
            MessageBoxW(g_hProgressWnd, TR("STR_INIT_ERR_WINFSP"), TR("STR_INIT_TITLE"), MB_OK | MB_ICONERROR);
            params->success = 0;
            PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
            return 0;
        }
    }

    // ==================================================================
    // 【第 3 步】释放 Rclone 主程序
    // ==================================================================
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

    // 系统版本与架构兼容性检测（i18n 已初始化，在资源释放之前阻断）
    {
        int is64 = Is64BitSystem();
        const wchar_t* sysArch = is64 ? L"64" : L"32";
        const wchar_t* buildArch = L"32";
#if defined(_WIN64)
        buildArch = L"64";
#endif
        const wchar_t* errMsg = NULL;
        static wchar_t errBuf[512];

        DWORD major = 0, minor = 0;
        if (GetRealOSVersion(&major, &minor)) {
            BOOL isWin7OrLater = (major > 6 || (major == 6 && minor >= 1));
            BOOL isWin10OrLater = (major >= 10);

            // 构建架构与系统架构不匹配
            BOOL archMismatch = FALSE;
#if defined(_WIN64)
            if (!is64) archMismatch = TRUE;
#else
            if (is64) archMismatch = TRUE;
#endif

            if (archMismatch) {
                swprintf_s(errBuf, 512, TR("MSG_SYS_MISMATCH"), buildArch, sysArch);
                errMsg = errBuf;
            }
#ifdef TARGET_WIN7
            // WIN7版本运行在Win10+系统
            else if (isWin10OrLater) {
                swprintf_s(errBuf, 512, TR("MSG_WIN7_ON_WIN10"), sysArch, sysArch);
                errMsg = errBuf;
            }
            // WIN7版本运行在不支持的系统（低于Win7）
            else if (!isWin7OrLater) {
                swprintf_s(errBuf, 512, TR("MSG_UNSUPPORTED_OS"), sysArch);
                errMsg = errBuf;
            }
#elif defined(TARGET_WIN10)
            // WIN10版本运行在Win7/8系统
            else if (!isWin10OrLater && isWin7OrLater) {
                swprintf_s(errBuf, 512, TR("MSG_WIN10_ON_WIN7"), sysArch, sysArch);
                errMsg = errBuf;
            }
            // WIN10版本运行在不支持的系统（低于Win7）
            else if (!isWin10OrLater) {
                swprintf_s(errBuf, 512, TR("MSG_UNSUPPORTED_OS"), sysArch);
                errMsg = errBuf;
            }
#endif
        }

        if (errMsg) {
            MessageBoxW(NULL, errMsg, TR("MSG_ERROR"), MB_OK | MB_ICONERROR);
            LogMessage("ERROR", "System check failed: is64=%d", is64);
            return 0;
        }
    }

#ifdef TARGET_WIN7
    int is64 = Is64BitSystem();
    int kb4474Installed = CheckKB4474419Installed(is64);
#else
    int kb4474Installed = 1;
#endif
    int winfspInstalled = CheckWinFspInstalled();
    
    char rcloneDest[MAX_PATH];
    sprintf_s(rcloneDest, sizeof(rcloneDest), "%s\\rclone.exe", workDir);
    int rcloneExists = (GetFileAttributesA(rcloneDest) != INVALID_FILE_ATTRIBUTES);

    if (kb4474Installed && winfspInstalled && rcloneExists) {
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
        WS_EX_DLGMODALFRAME,
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