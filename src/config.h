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
	int debug_log;
	int auto_hide;
	int vfs_cache_mode; // 0=off, 1=minimal, 2=writes, 3=full
} AppConfig;

void LoadConfig(AppConfig* cfg);
void SaveConfig(const AppConfig* cfg);
void SetAppAutoStart(int enable);