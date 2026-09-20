#pragma once

int TestRcloneConnection(const char* testCmd);
int StartRcloneProcess(const char* fullCmdLine, const char* driveLetter);
void StopRcloneMount(void);
int RcloneObscurePassword(const char* rclonePath, const char* plainPass, char* outObscured, size_t maxLen);