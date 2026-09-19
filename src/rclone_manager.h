#pragma once
#include "config.h"

int StartRcloneMount(const char* rclonePath, const char* url, const AppConfig* cfg);
void StopRcloneMount();