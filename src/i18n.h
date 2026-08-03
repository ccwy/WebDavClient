#pragma once
#include <windows.h>

void InitI18n(const char* langCode);
const wchar_t* TR(const char* key);
void FreeI18n();