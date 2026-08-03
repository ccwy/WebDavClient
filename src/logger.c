#include "logger.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static FILE* g_LogFile = NULL;
static int g_DebugEnabled = 0;

void InitLogger() {
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* lastSlash = strrchr(workDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    char logPath[MAX_PATH];
    sprintf_s(logPath, sizeof(logPath), "%s\\client_debug.log", workDir);
    fopen_s(&g_LogFile, logPath, "a");
}

void CloseLogger() {
    if (g_LogFile) {
        fclose(g_LogFile);
        g_LogFile = NULL;
    }
}

// 实时控制日志记录开关
void SetDebugLogEnabled(int enable) {
    g_DebugEnabled = enable;
}

void LogMessage(const char* level, const char* format, ...) {
    // 如果未启用调试日志，则不写入文件
    if (!g_DebugEnabled || !g_LogFile) return;

    time_t now = time(NULL);
    struct tm tms;
    localtime_s(&tms, &now);

    fprintf(g_LogFile, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] ",
        tms.tm_year + 1900, tms.tm_mon + 1, tms.tm_mday,
        tms.tm_hour, tms.tm_min, tms.tm_sec, level);

    va_list args;
    va_start(args, format);
    vfprintf(g_LogFile, format, args);
    va_end(args);

    fprintf(g_LogFile, "\n");
    fflush(g_LogFile);
}