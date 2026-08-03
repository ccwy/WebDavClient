#include <windows.h>
#include <stdio.h>
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")
#include "deployment.h"
#include "logger.h"
#include "../res/resource.h"

// 声明项目原有的国际化函数
extern void InitI18n(const char* langCode);
extern const char* TR(const char* key);

// 进度窗口全局句柄及控件
static HWND g_hProgressWnd = NULL;
static HWND g_hStatusText = NULL;
static WCHAR g_currentStatus[512] = L"Loading...";
static WCHAR g_windowTitle[128] = L"WebDAV Client Initialization";

#define WM_UPDATE_STATUS (WM_USER + 100)

// 将 UTF-8 转换为 WCHAR 宽字符，防止乱码
static void Utf8ToWide(const char* utf8Str, WCHAR* wideStr, int maxLen) {
    if (!utf8Str || utf8Str[0] == '\0') return;
    MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, wideStr, maxLen);
}

// 进度窗口过程函数
static LRESULT CALLBACK ProgressWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hStatusText = CreateWindowExW(
            0, L"STATIC", g_currentStatus,
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
            20, 25, 360, 45,
            hwnd, NULL, GetModuleHandle(NULL), NULL
        );
        break;
    }
    case WM_UPDATE_STATUS: {
        if (g_hStatusText && lParam) {
            SetWindowTextW(g_hStatusText, (const WCHAR*)lParam);
        }
        break;
    }
    case WM_CLOSE:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// 实时更新支持多语言和格式化的状态提示
static void UpdateStatus(const char* format, ...) {
    char utf8Buf[512];
    va_list args;
    va_start(args, format);
    vsnprintf_s(utf8Buf, sizeof(utf8Buf), _TRUNCATE, format, args);
    va_end(args);

    WCHAR wBuf[512] = { 0 };
    Utf8ToWide(utf8Buf, wBuf, 512);
    if (wBuf[0] != L'\0') {
        wcscpy_s(g_currentStatus, sizeof(g_currentStatus) / sizeof(WCHAR), wBuf);
    }

    LogMessage("INFO", "%s", utf8Buf);
    if (g_hProgressWnd && g_hStatusText) {
        PostMessageW(g_hProgressWnd, WM_UPDATE_STATUS, 0, (LPARAM)g_currentStatus);
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

int DownloadFileOnline(const char* url, const char* outputPath) {
    HRESULT hr = URLDownloadToFileA(NULL, url, outputPath, 0, NULL);
    return (hr == S_OK);
}

typedef struct {
    char outRclonePath[MAX_PATH];
    size_t pathSize;
    int success;
} InitParams;

static DWORD WINAPI InitWorkerThread(LPVOID lpParam) {
    InitParams* params = (InitParams*)lpParam;
    int majorVer = GetWindowsMajorVersion();
    int is64 = Is64BitSystem();

    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    char rcloneDest[MAX_PATH], msiDest[MAX_PATH];
    sprintf_s(rcloneDest, sizeof(rcloneDest), "%s\\rclone.exe", workDir);
    sprintf_s(msiDest, sizeof(msiDest), "%s\\winfsp.msi", workDir);

    // 1. 在线下载 rclone.exe
    if (GetFileAttributesA(rcloneDest) == INVALID_FILE_ATTRIBUTES) {
        const char* folder = (majorVer >= 10) ? "win10" : "win7";
        const char* exeName = is64 ? "rclone_x64.exe" : "rclone_x86.exe";
        
        const char* trMsg = TR("Init.DownloadingRclone");
        UpdateStatus((trMsg && trMsg[0] != '\0') ? trMsg : "Downloading core component: %s", exeName);

        char url[512], tempRclone[MAX_PATH];
        sprintf_s(url, sizeof(url), "https://raw.githubusercontent.com/ccwy/WebDavClient/onlin/%s/%s", folder, exeName);
        sprintf_s(tempRclone, sizeof(tempRclone), "%s\\%s", workDir, exeName);

        if (!DownloadFileOnline(url, tempRclone)) {
            const char* errTr = TR("Init.ErrorDownloadRclone");
            UpdateStatus("%s", (errTr && errTr[0] != '\0') ? errTr : "Error: Failed to download rclone executable!");
            params->success = 0;
            PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (strcmp(tempRclone, rcloneDest) != 0) {
            MoveFileExA(tempRclone, rcloneDest, MOVEFILE_REPLACE_EXISTING);
        }
    }

    // 2. Win7 TLS 1.2 补丁下载
    if (majorVer == 6) {
        if (!CheckWin7TlsEnabled()) {
            const char* tlsTr = TR("Init.MissingTls");
            UpdateStatus("%s", (tlsTr && tlsTr[0] != '\0') ? tlsTr : "Win7 TLS 1.2 missing, downloading security patch...");
            const char* msuName = is64 ? "windows6.1-kb3140245-x64.msu" : "windows6.1-kb3140245-x86.msu";
            
            char url[512], msuDest[MAX_PATH];
            sprintf_s(url, sizeof(url), "https://raw.githubusercontent.com/ccwy/WebDavClient/onlin/win7/%s", msuName);
            sprintf_s(msuDest, sizeof(msuDest), "%s\\%s", workDir, msuName);

            if (DownloadFileOnline(url, msuDest)) {
                const char* patchTr = TR("Init.InstallingPatch");
                UpdateStatus("%s", (patchTr && patchTr[0] != '\0') ? patchTr : "Installing Windows 7 security patch...");
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
            }
        }
    }

    // 3. WinFsp 驱动下载与安装
    if (!CheckWinFspInstalled()) {
        const char* winfspTr = TR("Init.DownloadingWinFsp");
        UpdateStatus("%s", (winfspTr && winfspTr[0] != '\0') ? winfspTr : "Downloading virtual drive driver WinFsp...");
        const char* folder = (majorVer >= 10) ? "win10" : "win7";
        char msiUrl[512];
        sprintf_s(msiUrl, sizeof(msiUrl), "https://raw.githubusercontent.com/ccwy/WebDavClient/onlin/%s/winfsp.msi", folder);

        if (DownloadFileOnline(msiUrl, msiDest)) {
            const char* instTr = TR("Init.InstallingWinFsp");
            UpdateStatus("%s", (instTr && instTr[0] != '\0') ? instTr : "Installing WinFsp driver, please follow instructions...");
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
        } else {
            const char* errMsi = TR("Init.ErrorDownloadWinFsp");
            UpdateStatus("%s", (errMsi && errMsi[0] != '\0') ? errMsi : "Error: Failed to download WinFsp driver!");
            params->success = 0;
            PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
            return 0;
        }

        if (!CheckWinFspInstalled()) {
            const char* errVer = TR("Init.ErrorVerifyWinFsp");
            UpdateStatus("%s", (errVer && errVer[0] != '\0') ? errVer : "Error: WinFsp verification failed!");
            params->success = 0;
            PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
            return 0;
        }
    }

    strcpy_s(params->outRclonePath, params->pathSize, rcloneDest);
    params->success = 1;

    PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
    return 0;
}

int InitializeEnvironment(char* outRclonePath, size_t pathSize) {
    HINSTANCE hInstance = GetModuleHandle(NULL);

    // 获取程序所在目录
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    // 【关键修复】将当前工作目录切换到程序所在目录，确保 i18n 能正确读取 lang 文件夹下的 .ini
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

    // 获取多语言文本，若未命中则提供安全的默认兜底字符串
    const char* titleTr = TR("Init.Title");
    const char* loadTr = TR("Init.LoadingLang");

    Utf8ToWide((titleTr && titleTr[0] != '\0') ? titleTr : "WebDAV Client Initialization", g_windowTitle, 128);
    Utf8ToWide((loadTr && loadTr[0] != '\0') ? loadTr : "Loading language packages and configuration...", g_currentStatus, 512);
    
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