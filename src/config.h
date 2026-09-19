#pragma once

#include "protocol.h"

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

void LoadCommonConfig(CommonConfig* cfg);
void SaveCommonConfig(const CommonConfig* cfg);
void SetAppAutoStart(int enable);