#pragma once

void InitLogger();
void CloseLogger();
void LogMessage(const char* level, const char* format, ...);
void SetDebugLogEnabled(int enable);