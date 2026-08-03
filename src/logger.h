#pragma once

void InitLogger();
void LogMessage(const char* level, const char* format, ...);
void CloseLogger();