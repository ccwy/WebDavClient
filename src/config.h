#pragma once

typedef struct {
    char host[512];
    char port[32];
    char path[256];
    char user[256];
    char pass[256];
    char drive[16];
    int ssl;
    int auto_start;
} AppConfig;

void LoadConfig(AppConfig* cfg);
void SaveConfig(const AppConfig* cfg);
void SetAppAutoStart(int enable);