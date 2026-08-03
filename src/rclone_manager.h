#pragma once

int StartRcloneMount(const char* rcloneExePath, const char* webdav_url, const char* username, const char* password, char drive_letter);
void StopRcloneMount();