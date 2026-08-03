#include "logger.h"
#include <windows.h>
#include <stdio.h>
#include <time.h>

static FILE* g_LogFile = NULL;

void InitLogger() {
    if (g_LogFile) return;

    // 获取程序当前所在的目录
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) {
        *lastSlash = '\0';
    }

    // 拼接日志文件路径到程序当前目录
    char logPath[MAX_PATH];
    sprintf_s(logPath, sizeof(logPath), "%s\\WebDavClient.log", workDir);

    // 以追加模式打开日志文件
    fopen_s(&g_LogFile, logPath, "a");
    if (g_LogFile) {
        LogMessage("INFO", "Logger initialized. Log path: %s", logPath);
    }
}

void LogMessage(const char* level, const char* format, ...) {
    // 如果未初始化，尝试在当前目录下自动初始化
    if (!g_LogFile) {
        InitLogger();
    }

    time_t now = time(NULL);
    struct tm tms;
    localtime_s(&tms, &now);

    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tms);

    // 格式化用户日志内容
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
    va_end(args);

    // 同时输出到控制台（如果存在）和日志文件
    char logLine[2048];
    int len = sprintf_s(logLine, sizeof(logLine), "[%s] [%s] %s\n", timeStr, level, message);

    if (g_LogFile) {
        fwrite(logLine, 1, len, g_LogFile);
        fflush(g_LogFile); // 实时写入，避免程序崩溃时日志丢失
    }

    // 同时打印到调试控制台
    OutputDebugStringA(logLine);
}

void CloseLogger() {
    if (g_LogFile) {
        LogMessage("INFO", "Logger shutting down.");
        fclose(g_LogFile);
        g_LogFile = NULL;
    }
}