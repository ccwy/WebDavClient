#include "i18n.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>

#define MAX_STRINGS 100
#define MAX_KEY_LEN 64
#define MAX_VAL_LEN 256

typedef struct {
    char key[MAX_KEY_LEN];
    wchar_t value[MAX_VAL_LEN];
} Translation;

static Translation g_Translations[MAX_STRINGS];
static int g_TranslationCount = 0;
static wchar_t g_FallbackBuffer[MAX_VAL_LEN];

void InitI18n(const char* langCode) {
    g_TranslationCount = 0;
    
    char tempDir[MAX_PATH];
    GetTempPathA(sizeof(tempDir), tempDir);
    
    char langPath[MAX_PATH];
    sprintf_s(langPath, sizeof(langPath), "%sWebDavClientEnv\\lang\\%s.ini", tempDir, langCode);

    LogMessage("INFO", "Loading language file from extracted path: %s", langPath);

    FILE* fp = NULL;
    if (fopen_s(&fp, langPath, "r") != 0 || !fp) {
        LogMessage("WARN", "Language file not found (%s), UI will use raw keys.", langCode);
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp) && g_TranslationCount < MAX_STRINGS) {
        line[strcspn(line, "\r\n")] = 0;
        
        char* equals = strchr(line, '=');
        if (equals) {
            *equals = '\0';
            char* key = line;
            char* val = equals + 1;

            strncpy_s(g_Translations[g_TranslationCount].key, MAX_KEY_LEN, key, _TRUNCATE);
            MultiByteToWideChar(CP_UTF8, 0, val, -1, g_Translations[g_TranslationCount].value, MAX_VAL_LEN);
            g_TranslationCount++;
        }
    }
    fclose(fp);
    LogMessage("INFO", "Loaded %d translation entries.", g_TranslationCount);
}

const wchar_t* TR(const char* key) {
    for (int i = 0; i < g_TranslationCount; i++) {
        if (strcmp(g_Translations[i].key, key) == 0) {
            return g_Translations[i].value;
        }
    }
    MultiByteToWideChar(CP_ACP, 0, key, -1, g_FallbackBuffer, MAX_VAL_LEN);
    return g_FallbackBuffer;
}

void FreeI18n() {
    g_TranslationCount = 0;
}