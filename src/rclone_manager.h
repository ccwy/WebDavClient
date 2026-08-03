#pragma once

int StartRcloneMount(const char* rclonePath, const char* url, const char* user, const char* pass, const char* driveLetter, int debug_log);
void StopRcloneMount();