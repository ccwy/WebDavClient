#include <windows.h>
#include <stdio.h>
#include <urlmon.h>
#include <wininet.h>
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "wininet.lib")
#include "deployment.h"
#include "logger.h"
#include "i18n.h"
#include "../res/resource.h"

// 进度窗口全局句柄及控制变量
static HWND g_hProgressWnd = NULL;
static HWND g_hStatusText = NULL;
static HFONT g_hProgressFont = NULL;
static volatile int g_cancelRequested = 0; // 取消下载标志

static WCHAR g_currentStatus[512] = L"Initializing...";
static WCHAR g_windowTitle[128] = L"WebDAV Client Initialization";

#define WM_UPDATE_STATUS (WM_USER + 100)

// 前置声明
void UpdateStatusW(const WCHAR* format, ...);

// ----------------------------------------------------------------------
// 1. 实现 IBindStatusCallback 接口，支持随时响应取消操作
// ----------------------------------------------------------------------
typedef struct {
    IBindStatusCallbackVtbl* lpVtbl;
    const wchar_t* statusFmt;
    const wchar_t* extraParam;
} DownloadProgressCallback;

static HRESULT STDMETHODCALLTYPE Callback_QueryInterface(IBindStatusCallback* This, REFIID riid, void** ppvObject) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IBindStatusCallback)) {
        *ppvObject = This;
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE Callback_AddRef(IBindStatusCallback* This) { return 1; }
static ULONG STDMETHODCALLTYPE Callback_Release(IBindStatusCallback* This) { return 1; }
static HRESULT STDMETHODCALLTYPE Callback_OnStartBinding(IBindStatusCallback* This, DWORD dwReserved, IBinding* pib) { return S_OK; }
static HRESULT STDMETHODCALLTYPE Callback_GetPriority(IBindStatusCallback* This, LONG* pnPriority) { return S_OK; }
static HRESULT STDMETHODCALLTYPE Callback_OnLowResource(IBindStatusCallback* This, DWORD reserved) { return S_OK; }

static HRESULT STDMETHODCALLTYPE Callback_OnProgress(IBindStatusCallback* This, ULONG ulProgress, ULONG ulProgressMax, ULONG ulStatusCode, LPCWSTR szStatusText) {
    if (g_cancelRequested) {
        return E_ABORT;
    }

    DownloadProgressCallback* pCB = (DownloadProgressCallback*)This;
    if (ulProgressMax > 0 && pCB->statusFmt) {
        int percent = (int)(((double)ulProgress / (double)ulProgressMax) * 100.0);
        if (percent > 100) percent = 100;

        WCHAR combinedFmt[512] = { 0 };
        if (pCB->extraParam && pCB->extraParam[0] != L'\0') {
            swprintf_s(combinedFmt, 512, L"%ls (%d%%)", pCB->statusFmt, percent);
            UpdateStatusW(combinedFmt, pCB->extraParam);
        } else {
            swprintf_s(combinedFmt, 512, L"%ls (%d%%)", pCB->statusFmt, percent);
            UpdateStatusW(L"%ls", combinedFmt);
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Callback_OnStopBinding(IBindStatusCallback* This, HRESULT hresult, LPCWSTR szError) { return S_OK; }
static HRESULT STDMETHODCALLTYPE Callback_GetBindInfo(IBindStatusCallback* This, DWORD* grfBINDF, BINDINFO* pbindinfo) { return S_OK; }
static HRESULT STDMETHODCALLTYPE Callback_OnDataAvailable(IBindStatusCallback* This, DWORD grfBSC, DWORD dwSize, FORMATETC* pformatetc, STGMEDIUM* pstgmed) { return S_OK; }
static HRESULT STDMETHODCALLTYPE Callback_OnObjectAvailable(IBindStatusCallback* This, REFIID riid, IUnknown* punk) { return S_OK; }

static IBindStatusCallbackVtbl g_CallbackVtbl = {
    Callback_QueryInterface,
    Callback_AddRef,
    Callback_Release,
    Callback_OnStartBinding,
    Callback_GetPriority,
    Callback_OnLowResource,
    Callback_OnProgress,
    Callback_OnStopBinding,
    Callback_GetBindInfo,
    Callback_OnDataAvailable,
    Callback_OnObjectAvailable
};

// ----------------------------------------------------------------------
// 2. Win32 窗口过程函数
// ----------------------------------------------------------------------
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
    case WM_CLOSE: {
        g_cancelRequested = 1;
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY: {
        if (g_hProgressFont) {
            DeleteObject(g_hProgressFont);
            g_hProgressFont = NULL;
        }
        break;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// 状态更新辅助函数
void UpdateStatusW(const WCHAR* format, ...) {
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
        PostMessageW(g_hProgressWnd, WM_UPDATE_STATUS, 0, (LPARAM)wBuf);
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

// 基础下载函数（带百分比实时回调）
int DownloadFileOnlineWithProgress(const char* url, const char* outputPath, const wchar_t* statusFmt, const wchar_t* extraParam) {
    DeleteUrlCacheEntryA(url);
    DeleteFileA(outputPath);

    DownloadProgressCallback cb;
    cb.lpVtbl = &g_CallbackVtbl;
    cb.statusFmt = statusFmt;
    cb.extraParam = extraParam;

    if (g_cancelRequested) return 0;

    if (extraParam && extraParam[0] != L'\0') {
        WCHAR combinedFmt[512] = { 0 };
        swprintf_s(combinedFmt, 512, L"%ls (0%%)", statusFmt);
        UpdateStatusW(combinedFmt, extraParam);
    } else {
        WCHAR combinedFmt[512] = { 0 };
        swprintf_s(combinedFmt, 512, L"%ls (0%%)", statusFmt);
        UpdateStatusW(L"%ls", combinedFmt);
    }

    HRESULT hr = URLDownloadToFileA(NULL, url, outputPath, 0, (IBindStatusCallback*)&cb);
    
    if (g_cancelRequested || hr != S_OK) {
        DeleteFileA(outputPath);
        return 0;
    }
    return 1;
}

// 【新增】：多加速线路自动切换容灾下载函数
int DownloadWithFallbacks(const char* rawPath, const char* outputPath, const wchar_t* statusFmt, const wchar_t* extraParam) {
    // 定义多个加速备用前缀
    const char* proxies[] = {
        "https://github.192286.xyz/",
        "https://hub.glowp.xyz/",
		"https://ghfast.top/",
		"https://g.blfrp.cn/"
    };
    int numProxies = 2;

    for (int i = 0; i < numProxies; i++) {
        if (g_cancelRequested) return 0;

        char fullUrl[512];
        sprintf_s(fullUrl, sizeof(fullUrl), "%s%s", proxies[i], rawPath);

        LogMessage("INFO", "Trying download with acceleration line %d: %s", i + 1, fullUrl);

        if (DownloadFileOnlineWithProgress(fullUrl, outputPath, statusFmt, extraParam)) {
            return 1; // 下载成功
        }

        LogMessage("WARN", "Download failed or slow on line %d, switching to next acceleration line...", i + 1);
    }

    return 0; // 所有线路均失败
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

    // 1. 在线下载 rclone.exe（启用多线路自动切换）
    if (!g_cancelRequested && GetFileAttributesA(rcloneDest) == INVALID_FILE_ATTRIBUTES) {
        const char* folder = (majorVer >= 10) ? "win10" : "win7";
        const char* exeName = is64 ? "rclone_x64.exe" : "rclone_x86.exe";
        
        wchar_t wExeName[128] = { 0 };
        MultiByteToWideChar(CP_UTF8, 0, exeName, -1, wExeName, 128);

        UpdateStatusW(TR("STR_INIT_DOWNLOADING_RCLONE"), wExeName);

        char rawPath[512], tempRclone[MAX_PATH];
        sprintf_s(rawPath, sizeof(rawPath), "https://raw.githubusercontent.com/ccwy/WebDavClient/onlin/%s/%s", folder, exeName);
        sprintf_s(tempRclone, sizeof(tempRclone), "%s\\%s", workDir, exeName);

        if (!DownloadWithFallbacks(rawPath, tempRclone, TR("STR_INIT_DOWNLOADING_RCLONE"), wExeName)) {
            params->success = 0;
            PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (!g_cancelRequested && strcmp(tempRclone, rcloneDest) != 0) {
            MoveFileExA(tempRclone, rcloneDest, MOVEFILE_REPLACE_EXISTING);
        }
    }

    // 2. Win7 TLS 1.2 补丁
    if (!g_cancelRequested && majorVer == 6) {
        if (!CheckWin7TlsEnabled()) {
            UpdateStatusW(L"%ls", TR("STR_INIT_INSTALLING_PATCH"));
            const char* msuName = is64 ? "windows6.1-kb3140245-x64.msu" : "windows6.1-kb3140245-x86.msu";
            int patchResId = is64 ? IDR_PATCH_X64 : IDR_PATCH_X86;
            
            char msuDest[MAX_PATH];
            sprintf_s(msuDest, sizeof(msuDest), "%s\\%s", workDir, msuName);

            if (ExtractResourceToFile(patchResId, msuDest)) {
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

    // 3. WinFsp 驱动下载与安装（启用多线路自动切换）
    if (!g_cancelRequested && !CheckWinFspInstalled()) {
        const char* folder = (majorVer >= 10) ? "win10" : "win7";
        char rawMsiPath[512];
        sprintf_s(rawMsiPath, sizeof(rawMsiPath), "https://raw.githubusercontent.com/ccwy/WebDavClient/onlin/%s/winfsp.msi", folder);

        if (DownloadWithFallbacks(rawMsiPath, msiDest, TR("STR_INIT_DOWNLOADING_WINFSP"), NULL)) {
            UpdateStatusW(L"%ls", TR("STR_INIT_INSTALLING_WINFSP"));
            char cmdLine[MAX_PATH * 2];
            sprintf_s(cmdLine, sizeof(cmdLine), "msiexec.exe /i \"%s\"", msiDest);
            STARTUPINFOA si = { sizeof(si) };
            PROCESS_INFORMATION pi = { 0 };
            if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                while (!g_cancelRequested) {
                    if (WaitForSingleObject(pi.hProcess, 1000) == WAIT_OBJECT_0) break;
                    if (CheckWinFspInstalled()) break;
                }
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
            DeleteFileA(msiDest);
        } else {
            params->success = 0;
            PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
            return 0;
        }

        if (!g_cancelRequested && !CheckWinFspInstalled()) {
            params->success = 0;
            PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
            return 0;
        }
    }

    if (g_cancelRequested) {
        params->success = 0;
    } else {
        strcpy_s(params->outRclonePath, params->pathSize, rcloneDest);
        params->success = 1;
    }

    PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
    return 0;
}

int InitializeEnvironment(char* outRclonePath, size_t pathSize) {
    HINSTANCE hInstance = GetModuleHandle(NULL);
    g_cancelRequested = 0;

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

    char rcloneDest[MAX_PATH];
    sprintf_s(rcloneDest, sizeof(rcloneDest), "%s\\rclone.exe", workDir);
    int rcloneExists = (GetFileAttributesA(rcloneDest) != INVALID_FILE_ATTRIBUTES);
    int winfspInstalled = CheckWinFspInstalled();

    if (rcloneExists && winfspInstalled) {
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

    if (g_cancelRequested || !params.success) {
        return 0;
    }

    if (params.success) {
        strcpy_s(outRclonePath, pathSize, params.outRclonePath);
    }

    return params.success;
}