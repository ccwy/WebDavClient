#include <windows.h>
#include <stdio.h>
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")
#include "deployment.h"
#include "logger.h"
#include "../res/resource.h"

// 进度窗口全局句柄及控件
static HWND g_hProgressWnd = NULL;
static HWND g_hStatusText = NULL;
static char g_currentStatus[512] = "正在初始化环境...";

// 自定义消息：用于后台线程更新提示文本
#define WM_UPDATE_STATUS (WM_USER + 100)

// 进度窗口过程函数
static LRESULT CALLBACK ProgressWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // 创建提示文本标签
        g_hStatusText = CreateWindowExA(
            0, "STATIC", g_currentStatus,
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
            20, 25, 340, 45,
            hwnd, NULL, GetModuleHandle(NULL), NULL
        );
        break;
    }
    case WM_UPDATE_STATUS: {
        if (g_hStatusText && lParam) {
            SetWindowTextA(g_hStatusText, (const char*)lParam);
        }
        break;
    }
    case WM_CLOSE:
        // 初始化期间不允许用户直接关闭窗口
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// 实时更新进度窗口文本的辅助函数
static void UpdateStatus(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsprintf_s(g_currentStatus, sizeof(g_currentStatus), format, args);
    va_end(args);

    LogMessage("INFO", "%s", g_currentStatus);
    if (g_hProgressWnd && g_hStatusText) {
        PostMessageA(g_hProgressWnd, WM_UPDATE_STATUS, 0, (LPARAM)g_currentStatus);
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

// 后台执行环境检查与在线下载逻辑的结构体与线程函数
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

    char langDir[MAX_PATH];
    sprintf_s(langDir, sizeof(langDir), "%s\\lang", workDir);
    CreateDirectoryA(langDir, NULL);

    char rcloneDest[MAX_PATH], msiDest[MAX_PATH], enDest[MAX_PATH], zhDest[MAX_PATH];
    sprintf_s(rcloneDest, sizeof(rcloneDest), "%s\\rclone.exe", workDir);
    sprintf_s(msiDest, sizeof(msiDest), "%s\\winfsp.msi", workDir);
    sprintf_s(enDest, sizeof(enDest), "%s\\en.ini", langDir);
    sprintf_s(zhDest, sizeof(zhDest), "%s\\zh.ini", langDir);

    // 1. 释放语言包
    UpdateStatus("正在加载语言包配置...");
    ExtractResourceToFile(IDR_LANG_EN, enDest);
    ExtractResourceToFile(IDR_LANG_ZH, zhDest);

    // 2. 在线下载 rclone.exe
    if (GetFileAttributesA(rcloneDest) == INVALID_FILE_ATTRIBUTES) {
        const char* folder = (majorVer >= 10) ? "win10" : "win7";
        const char* exeName = is64 ? "rclone_x64.exe" : "rclone_x86.exe";
        
        UpdateStatus("正在下载核心组件 %s...", exeName);
        char url[512], tempRclone[MAX_PATH];
        sprintf_s(url, sizeof(url), "https://raw.githubusercontent.com/ccwy/WebDavClient/onlin/%s/%s", folder, exeName);
        sprintf_s(tempRclone, sizeof(tempRclone), "%s\\%s", workDir, exeName);

        if (!DownloadFileOnline(url, tempRclone)) {
            UpdateStatus("错误: 下载 rclone 失败！");
            params->success = 0;
            PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (strcmp(tempRclone, rcloneDest) != 0) {
            MoveFileExA(tempRclone, rcloneDest, MOVEFILE_REPLACE_EXISTING);
        }
    }

    // 3. Win7 TLS 1.2 补丁下载
    if (majorVer == 6) {
        if (!CheckWin7TlsEnabled()) {
            UpdateStatus("检测到 Win7 未开启 TLS 1.2，正在下载安全补丁...");
            const char* msuName = is64 ? "windows6.1-kb3140245-x64.msu" : "windows6.1-kb3140245-x86.msu";
            
            char url[512], msuDest[MAX_PATH];
            sprintf_s(url, sizeof(url), "https://raw.githubusercontent.com/ccwy/WebDavClient/onlin/win7/%s", msuName);
            sprintf_s(msuDest, sizeof(msuDest), "%s\\%s", workDir, msuName);

            if (DownloadFileOnline(url, msuDest)) {
                UpdateStatus("正在调用系统安装 Win7 安全补丁...");
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

    // 4. WinFsp 驱动下载与安装
    if (!CheckWinFspInstalled()) {
        UpdateStatus("正在下载虚拟盘驱动组件 WinFsp...");
        const char* folder = (majorVer >= 10) ? "win10" : "win7";
        char msiUrl[512];
        sprintf_s(msiUrl, sizeof(msiUrl), "https://raw.githubusercontent.com/ccwy/WebDavClient/onlin/%s/winfsp.msi", folder);

        if (DownloadFileOnline(msiUrl, msiDest)) {
            UpdateStatus("正在安装 WinFsp 驱动程序，请按提示完成...");
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
            UpdateStatus("错误: 下载 WinFsp 驱动失败！");
            params->success = 0;
            PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
            return 0;
        }

        if (!CheckWinFspInstalled()) {
            UpdateStatus("错误: WinFsp 驱动验证失败！");
            params->success = 0;
            PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
            return 0;
        }
    }

    strcpy_s(params->outRclonePath, params->pathSize, rcloneDest);
    params->success = 1;

    // 完成后关闭进度窗口
    PostMessageA(g_hProgressWnd, WM_CLOSE, 0, 0);
    return 0;
}

// 主初始化入口：负责创建带UI的进度提示窗并驱动后台下载
int InitializeEnvironment(char* outRclonePath, size_t pathSize) {
    HINSTANCE hInstance = GetModuleHandle(NULL);
    
    // 注册临时进度窗口类
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = ProgressWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "DownloadProgressClass";
    wc.hCursor = LoadCursor(NULL, IDC_WAIT);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    // 计算屏幕居中坐标
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int dlgWidth = 400;
    int dlgHeight = 120;
    int dlgX = (screenWidth - dlgWidth) / 2;
    int dlgY = (screenHeight - dlgHeight) / 2;

    // 创建无边框/带标题栏的精美提示窗口
    g_hProgressWnd = CreateWindowExA(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        "DownloadProgressClass", "WebDAV 客户端初始化",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        dlgX, dlgY, dlgWidth, dlgHeight,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hProgressWnd) return 0;

    ShowWindow(g_hProgressWnd, SW_SHOW);
    UpdateWindow(g_hProgressWnd);

    // 启动后台工作线程
    InitParams params = { 0 };
    params.pathSize = pathSize;
    params.success = 0;

    HANDLE hThread = CreateThread(NULL, 0, InitWorkerThread, &params, 0, NULL);
    if (!hThread) {
        DestroyWindow(g_hProgressWnd);
        UnregisterClassA("DownloadProgressClass", hInstance);
        return 0;
    }

    // 专属消息循环：使进度窗口保持响应并随时刷新下载状态
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        if (msg.hwnd == g_hProgressWnd && msg.message == WM_CLOSE) {
            DestroyWindow(g_hProgressWnd);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 等待后台线程完全退出并回收资源
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    UnregisterClassA("DownloadProgressClass", hInstance);

    if (params.success) {
        strcpy_s(outRclonePath, pathSize, params.outRclonePath);
    }

    return params.success;
}