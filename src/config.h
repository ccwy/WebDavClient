#pragma once

#include "protocol.h"
#include "protocol_webdav.h"
#include "protocol_smb.h"
#include "protocol_sftp.h"
#include "protocol_ftp.h"

/* ======================================================================
   配置文件管理 — 分段 INI 格式
   格式:
     [global]
     auto_start=0
     debug_log=0
     auto_hide=0

     [conn_1]
     name=My WebDAV
     protocol=webdav
     drive=Z
     ...（通用字段 + 协议专属字段）

     [conn_2]
     name=My SMB
     protocol=smb
     ...
   ====================================================================== */

/* ---- 全局配置 ---- */
void LoadGlobalConfig(GlobalConfig* cfg);
void SaveGlobalConfig(const GlobalConfig* cfg);

/* ---- 连接列表 CRUD ---- */
void LoadAppConfig(AppConfig* cfg);
void SaveAppConfig(const AppConfig* cfg);

/* 获取指定连接的通用配置 */
void LoadConnectionConfig(ConnectionConfig* cfg, const char* section);
/* 保存指定连接的通用配置 */
void SaveConnectionConfig(const ConnectionConfig* cfg);

/* 添加新连接，返回新连接在数组中的索引，-1 表示失败 */
int AddConnection(AppConfig* cfg, const ConnectionConfig* conn);
/* 删除指定 ID 的连接，返回 0 成功，-1 未找到 */
int RemoveConnection(AppConfig* cfg, const char* id);
/* 按 ID 查找连接索引，返回 -1 未找到 */
int FindConnectionById(const AppConfig* cfg, const char* id);
/* 生成唯一连接 ID */
void GenerateConnectionId(const AppConfig* cfg, char* outId, size_t maxLen);

/* ---- 协议专属配置（按连接 section 读写） ---- */
void LoadWebDavConfig(const char* section, WebDavConfig* cfg);
void SaveWebDavConfig(const char* section, const WebDavConfig* cfg);

void LoadSmbConfig(const char* section, SmbConfig* cfg);
void SaveSmbConfig(const char* section, const SmbConfig* cfg);

void LoadSftpConfig(const char* section, SftpConfig* cfg);
void SaveSftpConfig(const char* section, const SftpConfig* cfg);

void LoadFtpConfig(const char* section, FtpConfig* cfg);
void SaveFtpConfig(const char* section, const FtpConfig* cfg);

/* ---- 开机自启 ---- */
void SetAppAutoStart(int enable);