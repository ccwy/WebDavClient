#pragma once

#include "protocol.h"
#include "protocol_webdav.h"
#include "protocol_smb.h"

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

/* 通用配置（config.ini 无前缀键） */
void LoadCommonConfig(CommonConfig* cfg);
void SaveCommonConfig(const CommonConfig* cfg);

/* WebDAV 专属配置（config.ini wd_ 前缀键） */
void LoadWebDavConfig(WebDavConfig* cfg);
void SaveWebDavConfig(const WebDavConfig* cfg);

/* SMB 专属配置（config.ini smb_ 前缀键） */
void LoadSmbConfig(SmbConfig* cfg);
void SaveSmbConfig(const SmbConfig* cfg);

void SetAppAutoStart(int enable);