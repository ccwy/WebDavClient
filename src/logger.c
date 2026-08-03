#include "logger.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static FILE* g_LogFile = NULL;

void InitLogger() {
    if (g_LogFile) return;

    // 如果是通过命令行启动的，确保控制台能输出
    AllocConsole();
    FILE* dummy;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);

    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) {
        *lastSlash = '\0';
    }

    char logPath[MAX_PATH];
    sprintf_s(logPath, sizeof(logPath), "%s\\WebDavClient.log", workDir);

    fopen_s(&g_LogFile, logPath, "a");
    if (g_LogFile) {
        LogMessage("INFO", "Logger initialized. Log path: %s", logPath);
    }
}

void LogMessage(const char* level, const char* format, ...) {
    if (!g_LogFile) {
        InitLogger();
    }

    time_t now = time(NULL);
    struct tm tms;
    localtime_s(&tms, &now);

    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tms);

    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
    va_end(args);

    char logLine[2048];
    int len = sprintf_s(logLine, sizeof(logLine), "[%s] [%s] %s\n", timeStr, level, message);

    // 写入日志文件
    if (g_LogFile) {
        fwrite(logLine, 1, len, g_LogFile);
        fflush(g_LogFile);
    }

    // 同时实时打印到前端控制台
    printf("%s", logLine);
    fflush(stdout);

    OutputDebugStringA(logLine);
}

void CloseLogger() {
    if (g_LogFile) {
        LogMessage("INFO", "Logger shutting down.");
        fclose(g_LogFile);
        g_LogFile = NULL;
    }
}