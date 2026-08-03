#pragma once

int StartRcloneMount(const char* rclonePath, const char* url, const char* user, const char* pass, const char* driveLetter, int debug_log, const char* vfs_cache_mode);
void StopRcloneMount();