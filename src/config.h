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
	// 高级设置
	char dir_cache_time[32];     // --dir-cache-time, 默认 "72h"
	char buffer_size[32];        // --buffer-size, 默认 "16M"
	int  transfers;              // --transfers, 默认 4
	char cache_dir[MAX_PATH];    // --cache-dir, 默认空(使用rclone临时目录)
	char vfs_cache_max_age[32];  // --vfs-cache-max-age, 默认 "24h"
	char vfs_read_chunk_size[32];      // --vfs-read-chunk-size, 默认 "128M"
	char vfs_read_chunk_size_limit[32]; // --vfs-read-chunk-size-limit, 默认 "off"
} AppConfig;

void LoadConfig(AppConfig* cfg);
void SaveConfig(const AppConfig* cfg);
void SetAppAutoStart(int enable);