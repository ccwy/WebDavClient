#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include "logger.h"

static FILE* g_LogFile = NULL;

void InitLogger() {
    AllocConsole();
    FILE* stream;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);

    char tempDir[MAX_PATH];
    GetTempPathA(sizeof(tempDir), tempDir);
    char logPath[MAX_PATH];
    sprintf_s(logPath, sizeof(logPath), "%sWebDavClient.log", tempDir);

    fopen_s(&g_LogFile, logPath, "w");
    LogMessage("INFO", "Logger initialized. Log path: %s", logPath);
}

void LogMessage(const char* level, const char* format, ...) {
    time_t now = time(NULL);
    struct tm t;
    localtime_s(&t, &now);

    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &t);

    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
    va_end(args);

    printf("[%s] [%s] %s\n", timeStr, level, buffer);
    fflush(stdout);

    if (g_LogFile) {
        fprintf(g_LogFile, "[%s] [%s] %s\n", timeStr, level, buffer);
        fflush(g_LogFile);
    }
}

void CloseLogger() {
    if (g_LogFile) {
        fclose(g_LogFile);
        g_LogFile = NULL;
    }
}