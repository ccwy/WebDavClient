#pragma once

typedef struct {
    char host[256];
    char port[32];
    char path[128];
    char user[128];
    char pass[128];
    char drive[8];
    int ssl;
    int auto_start;
} AppConfig;

void LoadConfig(AppConfig* cfg);
void SaveConfig(const AppConfig* cfg);
void SetAppAutoStart(int enable);