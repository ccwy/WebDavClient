#include "protocol_smb.h"
#include "protocol.h"
#include "logger.h"
#include "i18n.h"
#include "rclone_manager.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ======================================================================
   高级设置对话框控件 ID（SMB 专属）
   ====================================================================== */
#define SMB_IDC_ADV_EDIT_DCT       301
#define SMB_IDC_ADV_EDIT_BS        302
#define SMB_IDC_ADV_EDIT_TR        303
#define SMB_IDC_ADV_EDIT_CD        304
#define SMB_IDC_ADV_EDIT_RCS       305
#define SMB_IDC_ADV_EDIT_RCSL      306
#define SMB_IDC_ADV_BTN_BROWSE     307
#define SMB_IDC_ADV_BTN_OK         308
#define SMB_IDC_ADV_BTN_CANCEL     309
#define SMB_IDC_ADV_BTN_RESET      310
#define SMB_IDC_ADV_EDIT_VOLNAME   311
#define SMB_IDC_ADV_COMBO_VFS      312
#define SMB_IDC_ADV_EDIT_VCMS      313
#define SMB_IDC_ADV_BTN_BACK       314
#define SMB_IDC_ADV_BTN_SAVE       315
#define SMB_IDC_ADV_BTN_CLEAR_CACHE 316

/* ======================================================================
   内部辅助：创建带字体的控件
   ====================================================================== */
static HWND SmbCreateStyledExW(DWORD dwExStyle, LPCWSTR cls, LPCWSTR text,
                                DWORD style, int x, int y, int w, int h,
                                HWND parent, HMENU id, HFONT font) {
    HWND hw = CreateWindowExW(dwExStyle, cls, text, style, x, y, w, h, parent, id, NULL, NULL);
    if (hw && font) SendMessageW(hw, WM_SETFONT, (WPARAM)font, TRUE);
    return hw;
}

static HWND SmbCreateStyledExA(DWORD dwExStyle, LPCSTR cls, LPCSTR text,
                                DWORD style, int x, int y, int w, int h,
                                HWND parent, HMENU id, HFONT font) {
    HWND hw = CreateWindowExA(dwExStyle, cls, text, style, x, y, w, h, parent, id, NULL, NULL);
    if (hw && font) SendMessageW(hw, WM_SETFONT, (WPARAM)font, TRUE);
    return hw;
}

static HWND SmbCreateBoldLabel(LPCWSTR text, int x, int y, int w, int h,
                                HWND parent, HFONT boldFont) {
    HWND hw = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                                x, y, w, h, parent, NULL, NULL, NULL);
    if (hw && boldFont) SendMessageW(hw, WM_SETFONT, (WPARAM)boldFont, TRUE);
    return hw;
}

/* ======================================================================
   配置默认值 / 加载 / 保存
   ====================================================================== */
static void SmbSetDefaults(SmbConfig* c) {
    strcpy_s(c->server, sizeof(c->server), "192.168.5.100");
    strcpy_s(c->port, sizeof(c->port), "445");
    strcpy_s(c->share, sizeof(c->share), "share");
    strcpy_s(c->user, sizeof(c->user), "guest");
    strcpy_s(c->pass, sizeof(c->pass), "");
    c->vfs_cache_mode = 3;  /* full */
    strcpy_s(c->dir_cache_time, sizeof(c->dir_cache_time), "24h");
    strcpy_s(c->buffer_size, sizeof(c->buffer_size), "64M");
    c->transfers = 4;
    c->cache_dir[0] = '\0';
    strcpy_s(c->vfs_cache_max_age, sizeof(c->vfs_cache_max_age), "24h");
    strcpy_s(c->vfs_read_chunk_size, sizeof(c->vfs_read_chunk_size), "128M");
    strcpy_s(c->vfs_read_chunk_size_limit, sizeof(c->vfs_read_chunk_size_limit), "off");
    strcpy_s(c->vfs_cache_max_size, sizeof(c->vfs_cache_max_size), "15G");
}

static void SmbLoadConfig(ProtocolHandler* self) {
    SmbData* d = (SmbData*)self->data;
    SmbSetDefaults(&d->cfg);

    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* slash = strrchr(workDir, '\\');
    if (slash) *slash = '\0';

    char iniPath[MAX_PATH];
    sprintf_s(iniPath, sizeof(iniPath), "%s\\config_smb.ini", workDir);

    FILE* fp = NULL;
    if (fopen_s(&fp, iniPath, "r") != 0 || !fp) return;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;

        if (strcmp(key, "server") == 0)                   strcpy_s(d->cfg.server, sizeof(d->cfg.server), val);
        else if (strcmp(key, "port") == 0)                strcpy_s(d->cfg.port, sizeof(d->cfg.port), val);
        else if (strcmp(key, "share") == 0)               strcpy_s(d->cfg.share, sizeof(d->cfg.share), val);
        else if (strcmp(key, "user") == 0)                strcpy_s(d->cfg.user, sizeof(d->cfg.user), val);
        else if (strcmp(key, "pass") == 0)                strcpy_s(d->cfg.pass, sizeof(d->cfg.pass), val);
        else if (strcmp(key, "vfs_cache_mode") == 0)      d->cfg.vfs_cache_mode = atoi(val);
        else if (strcmp(key, "dir_cache_time") == 0)      strcpy_s(d->cfg.dir_cache_time, sizeof(d->cfg.dir_cache_time), val);
        else if (strcmp(key, "buffer_size") == 0)         strcpy_s(d->cfg.buffer_size, sizeof(d->cfg.buffer_size), val);
        else if (strcmp(key, "transfers") == 0)           d->cfg.transfers = atoi(val);
        else if (strcmp(key, "cache_dir") == 0)           strcpy_s(d->cfg.cache_dir, sizeof(d->cfg.cache_dir), val);
        else if (strcmp(key, "vfs_cache_max_age") == 0)   strcpy_s(d->cfg.vfs_cache_max_age, sizeof(d->cfg.vfs_cache_max_age), val);
        else if (strcmp(key, "vfs_read_chunk_size") == 0) strcpy_s(d->cfg.vfs_read_chunk_size, sizeof(d->cfg.vfs_read_chunk_size), val);
        else if (strcmp(key, "vfs_read_chunk_size_limit") == 0) strcpy_s(d->cfg.vfs_read_chunk_size_limit, sizeof(d->cfg.vfs_read_chunk_size_limit), val);
        else if (strcmp(key, "vfs_cache_max_size") == 0)  strcpy_s(d->cfg.vfs_cache_max_size, sizeof(d->cfg.vfs_cache_max_size), val);
    }
    fclose(fp);
}

static void SmbSaveConfig(ProtocolHandler* self) {
    SmbData* d = (SmbData*)self->data;

    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* slash = strrchr(workDir, '\\');
    if (slash) *slash = '\0';

    char iniPath[MAX_PATH];
    sprintf_s(iniPath, sizeof(iniPath), "%s\\config_smb.ini", workDir);

    FILE* fp = NULL;
    if (fopen_s(&fp, iniPath, "w") != 0 || !fp) return;

    fprintf(fp, "server=%s\n", d->cfg.server);
    fprintf(fp, "port=%s\n", d->cfg.port);
    fprintf(fp, "share=%s\n", d->cfg.share);
    fprintf(fp, "user=%s\n", d->cfg.user);
    fprintf(fp, "pass=%s\n", d->cfg.pass);
    fprintf(fp, "vfs_cache_mode=%d\n", d->cfg.vfs_cache_mode);
    fprintf(fp, "dir_cache_time=%s\n", d->cfg.dir_cache_time);
    fprintf(fp, "buffer_size=%s\n", d->cfg.buffer_size);
    fprintf(fp, "transfers=%d\n", d->cfg.transfers);
    fprintf(fp, "cache_dir=%s\n", d->cfg.cache_dir);
    fprintf(fp, "vfs_cache_max_age=%s\n", d->cfg.vfs_cache_max_age);
    fprintf(fp, "vfs_read_chunk_size=%s\n", d->cfg.vfs_read_chunk_size);
    fprintf(fp, "vfs_read_chunk_size_limit=%s\n", d->cfg.vfs_read_chunk_size_limit);
    fprintf(fp, "vfs_cache_max_size=%s\n", d->cfg.vfs_cache_max_size);

    fclose(fp);
}

/* ======================================================================
   主页面 UI
   ====================================================================== */
static void SmbCreateMainControls(ProtocolHandler* self, HWND hwnd,
                                   HFONT hFont, HFONT hBoldFont) {
    SmbData* d = (SmbData*)self->data;

    d->hMainLabels[0] = SmbCreateBoldLabel(TR("SMB_STR_SERVER"), 30, 70, 110, 28, hwnd, hBoldFont);
    d->hServerBox = SmbCreateStyledExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.server,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 70, 390, 28, hwnd, NULL, hFont);

    d->hMainLabels[1] = SmbCreateBoldLabel(TR("SMB_STR_PORT"), 30, 115, 110, 28, hwnd, hBoldFont);
    d->hPortBox = SmbCreateStyledExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.port,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, 145, 115, 130, 28, hwnd, NULL, hFont);

    d->hMainLabels[2] = SmbCreateBoldLabel(TR("SMB_STR_SHARE"), 30, 160, 110, 28, hwnd, hBoldFont);
    d->hShareBox = SmbCreateStyledExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.share,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 160, 390, 28, hwnd, NULL, hFont);

    d->hMainLabels[3] = SmbCreateBoldLabel(TR("SMB_STR_USER"), 30, 205, 110, 28, hwnd, hBoldFont);
    d->hUserBox = SmbCreateStyledExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.user,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 205, 390, 28, hwnd, NULL, hFont);

    d->hMainLabels[4] = SmbCreateBoldLabel(TR("SMB_STR_PASS"), 30, 250, 110, 28, hwnd, hBoldFont);
    d->hPassBox = SmbCreateStyledExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.pass,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD, 145, 250, 390, 28, hwnd, NULL, hFont);
}

static void SmbShowMainControls(ProtocolHandler* self, HWND hwnd) {
    SmbData* d = (SmbData*)self->data;
    int i;
    for (i = 0; i < 5; i++) ShowWindow(d->hMainLabels[i], SW_SHOW);
    ShowWindow(d->hServerBox, SW_SHOW);
    ShowWindow(d->hPortBox, SW_SHOW);
    ShowWindow(d->hShareBox, SW_SHOW);
    ShowWindow(d->hUserBox, SW_SHOW);
    ShowWindow(d->hPassBox, SW_SHOW);
}

static void SmbHideMainControls(ProtocolHandler* self, HWND hwnd) {
    SmbData* d = (SmbData*)self->data;
    int i;
    for (i = 0; i < 5; i++) ShowWindow(d->hMainLabels[i], SW_HIDE);
    ShowWindow(d->hServerBox, SW_HIDE);
    ShowWindow(d->hPortBox, SW_HIDE);
    ShowWindow(d->hShareBox, SW_HIDE);
    ShowWindow(d->hUserBox, SW_HIDE);
    ShowWindow(d->hPassBox, SW_HIDE);
}

/* ======================================================================
   高级设置页面 UI
   ====================================================================== */
static void SmbCreateAdvControls(ProtocolHandler* self, HWND hwnd,
                                  HFONT hFont, HFONT hBoldFont, HFONT hDescFont) {
    SmbData* d = (SmbData*)self->data;
    int y;
    char transfersStr[16];
    const wchar_t* vfsDesc = NULL;

    d->hAdvDescFont = hDescFont;

    /* Row 0: vfs-cache-mode ComboBox */
    y = 15;
    d->hAdvLabels[0] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_VFS_CACHE_MODE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[0], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[0] = CreateWindowExW(0, L"STATIC", L"--vfs-cache-mode", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[0], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvComboVfs = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 195, y, 330, 200, hwnd, (HMENU)SMB_IDC_ADV_COMBO_VFS, NULL, NULL);
    SendMessageW(d->hAdvComboVfs, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(d->hAdvComboVfs, CB_ADDSTRING, 0, (LPARAM)TR("WD_STR_VFS_CACHE_OFF"));
    SendMessageW(d->hAdvComboVfs, CB_ADDSTRING, 0, (LPARAM)TR("WD_STR_VFS_CACHE_MINIMAL"));
    SendMessageW(d->hAdvComboVfs, CB_ADDSTRING, 0, (LPARAM)TR("WD_STR_VFS_CACHE_WRITES"));
    SendMessageW(d->hAdvComboVfs, CB_ADDSTRING, 0, (LPARAM)TR("WD_STR_VFS_CACHE_FULL"));
    SendMessageW(d->hAdvComboVfs, CB_SETCURSEL, (WPARAM)d->cfg.vfs_cache_mode, 0);
    switch (d->cfg.vfs_cache_mode) {
        case 0: vfsDesc = TR("WD_STR_VFS_TIP_OFF"); break;
        case 1: vfsDesc = TR("WD_STR_VFS_TIP_MINIMAL"); break;
        case 2: vfsDesc = TR("WD_STR_VFS_TIP_WRITES"); break;
        case 3: vfsDesc = TR("WD_STR_VFS_TIP_FULL"); break;
        default: vfsDesc = TR("WD_STR_VFS_TIP_WRITES"); break;
    }
    d->hAdvDescLabels[0] = CreateWindowExW(0, L"STATIC", vfsDesc, WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[0], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 1: dir-cache-time */
    y = 105;
    d->hAdvLabels[1] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_DIR_CACHE_TIME"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[1], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[1] = CreateWindowExW(0, L"STATIC", L"--dir-cache-time", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[1], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[0] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.dir_cache_time, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_DCT, NULL, NULL);
    SendMessageW(d->hAdvEdits[0], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[1] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_HINT_DIR_CACHE_TIME"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[1], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 2: buffer-size */
    y = 195;
    d->hAdvLabels[2] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_BUFFER_SIZE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[2], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[2] = CreateWindowExW(0, L"STATIC", L"--buffer-size", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[2], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[1] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.buffer_size, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_BS, NULL, NULL);
    SendMessageW(d->hAdvEdits[1], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[2] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_HINT_BUFFER_SIZE"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[2], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 3: transfers */
    y = 285;
    d->hAdvLabels[3] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_TRANSFERS"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[3], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[3] = CreateWindowExW(0, L"STATIC", L"--transfers", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[3], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    sprintf_s(transfersStr, sizeof(transfersStr), "%d", d->cfg.transfers);
    d->hAdvEdits[2] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", transfersStr, WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_TR, NULL, NULL);
    SendMessageW(d->hAdvEdits[2], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[3] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_HINT_TRANSFERS"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[3], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 4: cache-dir (narrower edit + browse button) */
    y = 375;
    d->hAdvLabels[4] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_CACHE_DIR"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[4], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[4] = CreateWindowExW(0, L"STATIC", L"--cache-dir", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[4], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[3] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.cache_dir, WS_CHILD | ES_AUTOHSCROLL, 195, y, 260, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_CD, NULL, NULL);
    SendMessageW(d->hAdvEdits[3], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvBtnBrowse = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | BS_PUSHBUTTON, 465, y, 60, 25, hwnd, (HMENU)SMB_IDC_ADV_BTN_BROWSE, NULL, NULL);
    SendMessageW(d->hAdvBtnBrowse, WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[4] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_HINT_CACHE_DIR"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[4], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 5: vfs-cache-max-age */
    y = 465;
    d->hAdvLabels[5] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_VFS_CACHE_MAX_AGE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[5], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[5] = CreateWindowExW(0, L"STATIC", L"--vfs-cache-max-age", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[5], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[4] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.vfs_cache_max_age, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_CMA, NULL, NULL);
    SendMessageW(d->hAdvEdits[4], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[5] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_HINT_VFS_CACHE_MAX_AGE"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[5], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 6: vfs-read-chunk-size */
    y = 555;
    d->hAdvLabels[6] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_VFS_READ_CHUNK"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[6], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[6] = CreateWindowExW(0, L"STATIC", L"--vfs-read-chunk-size", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[6], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[5] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.vfs_read_chunk_size, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_RCS, NULL, NULL);
    SendMessageW(d->hAdvEdits[5], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[6] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_HINT_VFS_READ_CHUNK"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[6], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 7: vfs-read-chunk-size-limit */
    y = 645;
    d->hAdvLabels[7] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_VFS_READ_CHUNK_LIMIT"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[7], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[7] = CreateWindowExW(0, L"STATIC", L"--vfs-read-chunk-size-limit", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[7], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[6] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.vfs_read_chunk_size_limit, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_RCSL, NULL, NULL);
    SendMessageW(d->hAdvEdits[6], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[7] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_HINT_VFS_READ_CHUNK_LIMIT"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[7], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 8: volname */
    y = 735;
    d->hAdvLabels[8] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VOLNAME"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[8], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[8] = CreateWindowExW(0, L"STATIC", L"--volname", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[8], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[8] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->commonCfg->volname, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_VOLNAME, NULL, NULL);
    SendMessageW(d->hAdvEdits[8], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[8] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VOLNAME"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[8], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 9: vfs-cache-max-size */
    y = 825;
    d->hAdvLabels[9] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_VFS_CACHE_MAX_SIZE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[9], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[9] = CreateWindowExW(0, L"STATIC", L"--vfs-cache-max-size", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[9], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[7] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.vfs_cache_max_size, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_VCMS, NULL, NULL);
    SendMessageW(d->hAdvEdits[7], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[9] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_HINT_VFS_CACHE_MAX_SIZE"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[9], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Bottom buttons */
    y = 920;
    d->hAdvBtnBack = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_BACK"), WS_CHILD | BS_PUSHBUTTON, 20, y, 121, 32, hwnd, (HMENU)SMB_IDC_ADV_BTN_BACK, NULL, NULL);
    SendMessageW(d->hAdvBtnBack, WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvBtnClearCache = CreateWindowExW(0, L"BUTTON", TR("SMB_STR_ADV_CLEAR_CACHE"), WS_CHILD | BS_PUSHBUTTON, 148, y, 121, 32, hwnd, (HMENU)SMB_IDC_ADV_BTN_CLEAR_CACHE, NULL, NULL);
    SendMessageW(d->hAdvBtnClearCache, WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvBtnSave = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_OK"), WS_CHILD | BS_PUSHBUTTON, 276, y, 121, 32, hwnd, (HMENU)SMB_IDC_ADV_BTN_SAVE, NULL, NULL);
    SendMessageW(d->hAdvBtnSave, WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvBtnReset = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_RESET"), WS_CHILD | BS_PUSHBUTTON, 404, y, 121, 32, hwnd, (HMENU)SMB_IDC_ADV_BTN_RESET, NULL, NULL);
    SendMessageW(d->hAdvBtnReset, WM_SETFONT, (WPARAM)hFont, TRUE);
}

static void SmbShowAdvControls(ProtocolHandler* self, HWND hwnd) {
    SmbData* d = (SmbData*)self->data;
    int i;
    for (i = 0; i < 10; i++) {
        ShowWindow(d->hAdvLabels[i], SW_SHOW);
        ShowWindow(d->hAdvParamLabels[i], SW_SHOW);
        ShowWindow(d->hAdvDescLabels[i], SW_SHOW);
    }
    for (i = 0; i < 9; i++) ShowWindow(d->hAdvEdits[i], SW_SHOW);
    ShowWindow(d->hAdvComboVfs, SW_SHOW);
    ShowWindow(d->hAdvBtnBrowse, SW_SHOW);
    ShowWindow(d->hAdvBtnBack, SW_SHOW);
    ShowWindow(d->hAdvBtnClearCache, SW_SHOW);
    ShowWindow(d->hAdvBtnSave, SW_SHOW);
    ShowWindow(d->hAdvBtnReset, SW_SHOW);
}

static void SmbHideAdvControls(ProtocolHandler* self, HWND hwnd) {
    SmbData* d = (SmbData*)self->data;
    int i;
    for (i = 0; i < 10; i++) {
        ShowWindow(d->hAdvLabels[i], SW_HIDE);
        ShowWindow(d->hAdvParamLabels[i], SW_HIDE);
        ShowWindow(d->hAdvDescLabels[i], SW_HIDE);
    }
    for (i = 0; i < 9; i++) ShowWindow(d->hAdvEdits[i], SW_HIDE);
    ShowWindow(d->hAdvComboVfs, SW_HIDE);
    ShowWindow(d->hAdvBtnBrowse, SW_HIDE);
    ShowWindow(d->hAdvBtnBack, SW_HIDE);
    ShowWindow(d->hAdvBtnClearCache, SW_HIDE);
    ShowWindow(d->hAdvBtnSave, SW_HIDE);
    ShowWindow(d->hAdvBtnReset, SW_HIDE);
}

static void SmbDestroyControls(ProtocolHandler* self, HWND hwnd) {
    SmbData* d = (SmbData*)self->data;
    int i;
    /* 销毁主页面控件 */
    for (i = 0; i < 5; i++) { if (d->hMainLabels[i]) DestroyWindow(d->hMainLabels[i]); d->hMainLabels[i] = NULL; }
    if (d->hServerBox) { DestroyWindow(d->hServerBox); d->hServerBox = NULL; }
    if (d->hPortBox)   { DestroyWindow(d->hPortBox);   d->hPortBox = NULL; }
    if (d->hShareBox)  { DestroyWindow(d->hShareBox);  d->hShareBox = NULL; }
    if (d->hUserBox)   { DestroyWindow(d->hUserBox);   d->hUserBox = NULL; }
    if (d->hPassBox)   { DestroyWindow(d->hPassBox);   d->hPassBox = NULL; }
    /* 销毁高级设置控件 */
    for (i = 0; i < 10; i++) {
        if (d->hAdvLabels[i])      { DestroyWindow(d->hAdvLabels[i]);      d->hAdvLabels[i] = NULL; }
        if (d->hAdvParamLabels[i]) { DestroyWindow(d->hAdvParamLabels[i]); d->hAdvParamLabels[i] = NULL; }
        if (d->hAdvDescLabels[i])  { DestroyWindow(d->hAdvDescLabels[i]);  d->hAdvDescLabels[i] = NULL; }
    }
    for (i = 0; i < 9; i++) { if (d->hAdvEdits[i]) { DestroyWindow(d->hAdvEdits[i]); d->hAdvEdits[i] = NULL; } }
    if (d->hAdvComboVfs)      { DestroyWindow(d->hAdvComboVfs);      d->hAdvComboVfs = NULL; }
    if (d->hAdvBtnBrowse)     { DestroyWindow(d->hAdvBtnBrowse);     d->hAdvBtnBrowse = NULL; }
    if (d->hAdvBtnBack)       { DestroyWindow(d->hAdvBtnBack);       d->hAdvBtnBack = NULL; }
    if (d->hAdvBtnClearCache) { DestroyWindow(d->hAdvBtnClearCache); d->hAdvBtnClearCache = NULL; }
    if (d->hAdvBtnSave)       { DestroyWindow(d->hAdvBtnSave);       d->hAdvBtnSave = NULL; }
    if (d->hAdvBtnReset)      { DestroyWindow(d->hAdvBtnReset);      d->hAdvBtnReset = NULL; }
}

static void SmbUpdateAdvPositions(ProtocolHandler* self, int scrollPos) {
    SmbData* d = (SmbData*)self->data;
    HDWP hdwp;
    int y;
    hdwp = BeginDeferWindowPos(46);
    if (!hdwp) return;

    /* Row 0: VFS ComboBox */
    y = 15 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[0], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[0], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvComboVfs, NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[0], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 1: dir-cache-time */
    y = 105 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[1], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[1], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[0], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[1], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 2: buffer-size */
    y = 195 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[2], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[2], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[1], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[2], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 3: transfers */
    y = 285 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[3], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[3], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[2], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[3], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 4: cache-dir */
    y = 375 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[4], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[4], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[3], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnBrowse, NULL, 465, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[4], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 5: vfs-cache-max-age */
    y = 465 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[5], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[5], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[4], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[5], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 6: vfs-read-chunk-size */
    y = 555 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[6], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[6], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[5], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[6], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 7: vfs-read-chunk-size-limit */
    y = 645 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[7], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[7], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[6], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[7], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 8: volname */
    y = 735 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[8], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[8], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[8], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[8], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 9: vfs-cache-max-size */
    y = 825 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[9], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[9], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[7], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[9], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Bottom buttons */
    y = 920 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnBack, NULL, 20, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnClearCache, NULL, 148, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnSave, NULL, 276, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnReset, NULL, 404, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    if (hdwp) EndDeferWindowPos(hdwp);
}

static int SmbGetAdvContentHeight(ProtocolHandler* self) {
    (void)self;
    return 967;  /* 15 + 90*10 + 15 + 32 + 15 */
}

static LRESULT SmbHandleCtlColor(ProtocolHandler* self, HWND hCtrl, HDC hdc) {
    SmbData* d = (SmbData*)self->data;
    int idx;
    for (idx = 0; idx < 10; idx++) {
        if (hCtrl == d->hAdvDescLabels[idx] || hCtrl == d->hAdvParamLabels[idx]) {
            SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
    }
    return -1;  /* 未处理 */
}

/* ======================================================================
   命令处理
   ====================================================================== */
static int SmbHandleCommand(ProtocolHandler* self, HWND hwnd,
                             WPARAM wParam, LPARAM lParam) {
    SmbData* d = (SmbData*)self->data;
    (void)lParam;

    if (LOWORD(wParam) == SMB_IDC_ADV_BTN_SAVE) {
        char transfersBuf[16];
        int vfsSel;
        GetWindowTextA(d->hAdvEdits[0], d->cfg.dir_cache_time, sizeof(d->cfg.dir_cache_time));
        GetWindowTextA(d->hAdvEdits[1], d->cfg.buffer_size, sizeof(d->cfg.buffer_size));
        memset(transfersBuf, 0, sizeof(transfersBuf));
        GetWindowTextA(d->hAdvEdits[2], transfersBuf, sizeof(transfersBuf));
        d->cfg.transfers = atoi(transfersBuf);
        if (d->cfg.transfers <= 0) d->cfg.transfers = 4;
        GetWindowTextA(d->hAdvEdits[3], d->cfg.cache_dir, sizeof(d->cfg.cache_dir));
        GetWindowTextA(d->hAdvEdits[4], d->cfg.vfs_cache_max_age, sizeof(d->cfg.vfs_cache_max_age));
        GetWindowTextA(d->hAdvEdits[5], d->cfg.vfs_read_chunk_size, sizeof(d->cfg.vfs_read_chunk_size));
        GetWindowTextA(d->hAdvEdits[6], d->cfg.vfs_read_chunk_size_limit, sizeof(d->cfg.vfs_read_chunk_size_limit));
        GetWindowTextA(d->hAdvEdits[7], d->cfg.vfs_cache_max_size, sizeof(d->cfg.vfs_cache_max_size));
        GetWindowTextA(d->hAdvEdits[8], d->commonCfg->volname, sizeof(d->commonCfg->volname));
        vfsSel = (int)SendMessageW(d->hAdvComboVfs, CB_GETCURSEL, 0, 0);
        d->cfg.vfs_cache_mode = (vfsSel != CB_ERR) ? vfsSel : 2;
        self->SaveConfig(self);
        return 2;  /* 已处理：保存后请求 main.c 切换回主页面 */
    }
    else if (LOWORD(wParam) == SMB_IDC_ADV_BTN_RESET) {
        SetWindowTextA(d->hAdvEdits[0], "72h");
        SetWindowTextA(d->hAdvEdits[1], "16M");
        SetWindowTextA(d->hAdvEdits[2], "4");
        SetWindowTextA(d->hAdvEdits[3], "");
        SetWindowTextA(d->hAdvEdits[4], "24h");
        SetWindowTextA(d->hAdvEdits[5], "128M");
        SetWindowTextA(d->hAdvEdits[6], "off");
        SetWindowTextA(d->hAdvEdits[7], "5G");
        SetWindowTextA(d->hAdvEdits[8], "Network_Disk");
        SendMessageW(d->hAdvComboVfs, CB_SETCURSEL, 2, 0);
        SetWindowTextW(d->hAdvDescLabels[0], TR("WD_STR_VFS_TIP_WRITES"));
        return 1;
    }
    else if (LOWORD(wParam) == SMB_IDC_ADV_BTN_CLEAR_CACHE) {
        char cacheDir[MAX_PATH];
        wchar_t wCacheDir[MAX_PATH];
        int isDefaultDir = 0;
        GetWindowTextA(d->hAdvEdits[3], cacheDir, sizeof(cacheDir));
        if (cacheDir[0] == '\0') {
            wchar_t localAppData[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
                swprintf_s(wCacheDir, MAX_PATH, L"%s\\rclone", localAppData);
                WideCharToMultiByte(CP_ACP, 0, wCacheDir, -1, cacheDir, MAX_PATH, NULL, NULL);
                isDefaultDir = 1;
            } else {
                MessageBoxW(hwnd, TR("SMB_MSG_CLEAR_CACHE_FAIL"), TR("MSG_ERROR"), MB_OK | MB_ICONERROR);
            }
        } else {
            MultiByteToWideChar(CP_ACP, 0, cacheDir, -1, wCacheDir, MAX_PATH);
        }
        if (cacheDir[0] != '\0') {
            int confirm = MessageBoxW(hwnd, TR("SMB_MSG_CLEAR_CACHE_CONFIRM"), TR("MSG_INFO"), MB_YESNO | MB_ICONQUESTION);
            if (confirm == IDYES) {
                if (GetFileAttributesW(wCacheDir) == INVALID_FILE_ATTRIBUTES) {
                    MessageBoxW(hwnd, TR("SMB_MSG_CLEAR_CACHE_EMPTY"), TR("MSG_INFO"), MB_OK | MB_ICONINFORMATION);
                } else {
                    SHFILEOPSTRUCTW fos;
                    wchar_t fromBuf[MAX_PATH + 2];
                    memset(fromBuf, 0, sizeof(fromBuf));
                    wcscpy_s(fromBuf, MAX_PATH + 1, wCacheDir);
                    memset(&fos, 0, sizeof(fos));
                    fos.hwnd = hwnd;
                    fos.wFunc = FO_DELETE;
                    fos.pFrom = fromBuf;
                    fos.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
                    if (SHFileOperationW(&fos) == 0 && !fos.fAnyOperationsAborted) {
                        CreateDirectoryW(wCacheDir, NULL);
                        MessageBoxW(hwnd, TR("SMB_MSG_CLEAR_CACHE_OK"), TR("MSG_INFO"), MB_OK | MB_ICONINFORMATION);
                    } else {
                        MessageBoxW(hwnd, TR("SMB_MSG_CLEAR_CACHE_FAIL"), TR("MSG_ERROR"), MB_OK | MB_ICONERROR);
                    }
                }
            }
        }
        return 1;
    }
    else if (LOWORD(wParam) == SMB_IDC_ADV_COMBO_VFS && HIWORD(wParam) == CBN_SELCHANGE) {
        int vfsSel = (int)SendMessageW(d->hAdvComboVfs, CB_GETCURSEL, 0, 0);
        if (vfsSel != CB_ERR) {
            const wchar_t* vfsDesc = NULL;
            switch (vfsSel) {
                case 0: vfsDesc = TR("WD_STR_VFS_TIP_OFF"); break;
                case 1: vfsDesc = TR("WD_STR_VFS_TIP_MINIMAL"); break;
                case 2: vfsDesc = TR("WD_STR_VFS_TIP_WRITES"); break;
                case 3: vfsDesc = TR("WD_STR_VFS_TIP_FULL"); break;
                default: vfsDesc = TR("WD_STR_VFS_TIP_WRITES"); break;
            }
            SetWindowTextW(d->hAdvDescLabels[0], vfsDesc);
        }
        return 1;
    }
    else if (LOWORD(wParam) == SMB_IDC_ADV_BTN_BROWSE) {
        BROWSEINFOW bi;
        LPITEMIDLIST pidl;
        wchar_t selectedPath[MAX_PATH];
        char ansiPath[MAX_PATH];
        memset(&bi, 0, sizeof(bi));
        bi.hwndOwner = hwnd;
        bi.lpszTitle = TR("SMB_STR_ADV_CACHE_DIR");
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        pidl = SHBrowseForFolderW(&bi);
        if (pidl) {
            if (SHGetPathFromIDListW(pidl, selectedPath)) {
                WideCharToMultiByte(CP_ACP, 0, selectedPath, -1, ansiPath, MAX_PATH, NULL, NULL);
                SetWindowTextA(d->hAdvEdits[3], ansiPath);
            }
            CoTaskMemFree(pidl);
        }
        return 1;
    }
    else if (LOWORD(wParam) == SMB_IDC_ADV_BTN_BACK) {
        return 2;  /* 请求 main.c 切换回主页面 */
    }

    return 0;  /* 未处理 */
}

/* ======================================================================
   高级设置 UI ↔ 配置 同步
   ====================================================================== */
static void SmbSaveAdvSettingsFromUI(ProtocolHandler* self) {
    SmbData* d = (SmbData*)self->data;
    char transfersBuf[16];
    int vfsSel;
    GetWindowTextA(d->hAdvEdits[0], d->cfg.dir_cache_time, sizeof(d->cfg.dir_cache_time));
    GetWindowTextA(d->hAdvEdits[1], d->cfg.buffer_size, sizeof(d->cfg.buffer_size));
    memset(transfersBuf, 0, sizeof(transfersBuf));
    GetWindowTextA(d->hAdvEdits[2], transfersBuf, sizeof(transfersBuf));
    d->cfg.transfers = atoi(transfersBuf);
    if (d->cfg.transfers <= 0) d->cfg.transfers = 4;
    GetWindowTextA(d->hAdvEdits[3], d->cfg.cache_dir, sizeof(d->cfg.cache_dir));
    GetWindowTextA(d->hAdvEdits[4], d->cfg.vfs_cache_max_age, sizeof(d->cfg.vfs_cache_max_age));
    GetWindowTextA(d->hAdvEdits[5], d->cfg.vfs_read_chunk_size, sizeof(d->cfg.vfs_read_chunk_size));
    GetWindowTextA(d->hAdvEdits[6], d->cfg.vfs_read_chunk_size_limit, sizeof(d->cfg.vfs_read_chunk_size_limit));
    GetWindowTextA(d->hAdvEdits[7], d->cfg.vfs_cache_max_size, sizeof(d->cfg.vfs_cache_max_size));
    GetWindowTextA(d->hAdvEdits[8], d->commonCfg->volname, sizeof(d->commonCfg->volname));
    vfsSel = (int)SendMessageW(d->hAdvComboVfs, CB_GETCURSEL, 0, 0);
    d->cfg.vfs_cache_mode = (vfsSel != CB_ERR) ? vfsSel : 2;
}

static void SmbResetAdvSettings(ProtocolHandler* self) {
    SmbData* d = (SmbData*)self->data;
    SetWindowTextA(d->hAdvEdits[0], "72h");
    SetWindowTextA(d->hAdvEdits[1], "16M");
    SetWindowTextA(d->hAdvEdits[2], "4");
    SetWindowTextA(d->hAdvEdits[3], "");
    SetWindowTextA(d->hAdvEdits[4], "24h");
    SetWindowTextA(d->hAdvEdits[5], "128M");
    SetWindowTextA(d->hAdvEdits[6], "off");
    SetWindowTextA(d->hAdvEdits[7], "5G");
    SetWindowTextA(d->hAdvEdits[8], "Network_Disk");
    SendMessageW(d->hAdvComboVfs, CB_SETCURSEL, 2, 0);
    SetWindowTextW(d->hAdvDescLabels[0], TR("WD_STR_VFS_TIP_WRITES"));
}

/* ======================================================================
   挂载执行
   ====================================================================== */
static const char* SmbGetVfsCacheModeStr(int mode) {
    switch (mode) {
        case 0: return "off";
        case 1: return "minimal";
        case 2: return "writes";
        case 3: return "full";
        default: return "writes";
    }
}

static int SmbExecuteMount(ProtocolHandler* self, HWND hwnd,
                            const char* rclonePath, int isAuto) {
    SmbData* d = (SmbData*)self->data;
    CommonConfig* cc = d->commonCfg;

    /* 从 UI 读取主页面配置 */
    GetWindowTextA(d->hServerBox, d->cfg.server, sizeof(d->cfg.server));
    GetWindowTextA(d->hPortBox, d->cfg.port, sizeof(d->cfg.port));
    GetWindowTextA(d->hShareBox, d->cfg.share, sizeof(d->cfg.share));
    GetWindowTextA(d->hUserBox, d->cfg.user, sizeof(d->cfg.user));
    GetWindowTextA(d->hPassBox, d->cfg.pass, sizeof(d->cfg.pass));

    /* 验证盘符 */
    if (strlen(cc->drive) != 1 || cc->drive[0] < 'A' || cc->drive[0] > 'Z') {
        LogMessage("ERROR", "Invalid drive letter: '%s'. Must be a single uppercase letter (A-Z).", cc->drive);
        if (!isAuto) MessageBoxW(hwnd, TR("MSG_INVALID_DRIVE"), TR("MSG_ERROR"), MB_OK | MB_ICONWARNING);
        return 0;
    }

    DWORD logicalDrives = GetLogicalDrives();
    int driveIndex = (int)(toupper((unsigned char)cc->drive[0]) - 'A');
    if (!isAuto && (logicalDrives & (1 << driveIndex)) != 0) {
        LogMessage("WARN", "Drive letter %c: is already in use on the system.", cc->drive[0]);
        MessageBoxW(hwnd, TR("MSG_DRIVE_IN_USE"), TR("MSG_ERROR"), MB_OK | MB_ICONWARNING);
        return 0;
    }

    self->SaveConfig(self);

    LogMessage("INFO", "SMB mount action triggered with server=%s share=%s", d->cfg.server, d->cfg.share);

    /* 密码混淆 */
    char obscuredPass[256] = { 0 };
    RcloneObscurePassword(rclonePath, d->cfg.pass, obscuredPass, sizeof(obscuredPass));

    /* 构建高级参数字符串 */
    char advParams[1024] = { 0 };
    char tmpBuf[256];
    const char* cacheMode = SmbGetVfsCacheModeStr(d->cfg.vfs_cache_mode);

    /* SMB 端口（非默认 445 时传递） */
    if (d->cfg.port[0] != '\0' && strcmp(d->cfg.port, "445") != 0) {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--smb-port %s ", d->cfg.port);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }

    if (d->cfg.dir_cache_time[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--dir-cache-time %s ", d->cfg.dir_cache_time);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    if (d->cfg.buffer_size[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--buffer-size %s ", d->cfg.buffer_size);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    if (d->cfg.transfers > 0) {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--transfers %d ", d->cfg.transfers);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    if (d->cfg.cache_dir[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--cache-dir \"%s\" ", d->cfg.cache_dir);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    if (d->cfg.vfs_cache_max_age[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-cache-max-age %s ", d->cfg.vfs_cache_max_age);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    if (d->cfg.vfs_read_chunk_size[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-read-chunk-size %s ", d->cfg.vfs_read_chunk_size);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    if (d->cfg.vfs_read_chunk_size_limit[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-read-chunk-size-limit %s ", d->cfg.vfs_read_chunk_size_limit);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    if (cc->volname[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--volname \"%s\" ", cc->volname);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    if (d->cfg.vfs_cache_max_size[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-cache-max-size %s ", d->cfg.vfs_cache_max_size);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }

    /* 构建完整 rclone 命令行 */
    char cmd[4096];
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* slash = strrchr(workDir, '\\');
    if (slash) *slash = '\0';

    if (cc->debug_log) {
        char logPath[MAX_PATH];
        sprintf_s(logPath, sizeof(logPath), "%s\\rclone_error.log", workDir);
        sprintf_s(cmd, sizeof(cmd),
            "\"%s\" mount :smb: %s: --smb-host \"%s\" --smb-share \"%s\" --smb-user \"%s\" --smb-pass \"%s\" "
            "--vfs-cache-mode %s "
            "%s"
            "--no-check-certificate "
            "--log-file \"%s\" -vv",
            rclonePath, cc->drive, d->cfg.server, d->cfg.share, d->cfg.user, obscuredPass, cacheMode, advParams, logPath
        );
        LogMessage("INFO", "Starting Rclone SMB mount with vfs-cache-mode=%s and debug logging enabled.", cacheMode);
    } else {
        sprintf_s(cmd, sizeof(cmd),
            "\"%s\" mount :smb: %s: --smb-host \"%s\" --smb-share \"%s\" --smb-user \"%s\" --smb-pass \"%s\" "
            "--vfs-cache-mode %s "
            "%s"
            "--no-check-certificate ",
            rclonePath, cc->drive, d->cfg.server, d->cfg.share, d->cfg.user, obscuredPass, cacheMode, advParams
        );
        LogMessage("INFO", "Starting Rclone SMB mount with vfs-cache-mode=%s and debug logging disabled.", cacheMode);
    }

    /* 调用 rclone_manager 执行挂载 */
    if (StartRcloneProcess(cmd, cc->drive)) {
        return 1;
    }
    return 0;
}

/* ======================================================================
   清理
   ====================================================================== */
static void SmbDestroy(ProtocolHandler* self) {
    SmbData* d = (SmbData*)self->data;
    if (d) {
        if (d->hAdvDescFont) {
            DeleteObject(d->hAdvDescFont);
            d->hAdvDescFont = NULL;
        }
        free(d);
    }
    self->data = NULL;
}

/* ======================================================================
   工厂函数
   ====================================================================== */
ProtocolHandler* CreateSmbHandler(CommonConfig* commonCfg) {
    SmbData* d = (SmbData*)calloc(1, sizeof(SmbData));
    if (!d) return NULL;

    d->commonCfg = commonCfg;

    ProtocolHandler* h = (ProtocolHandler*)calloc(1, sizeof(ProtocolHandler));
    if (!h) { free(d); return NULL; }

    h->name    = "smb";
    h->data    = d;

    h->CreateMainControls  = SmbCreateMainControls;
    h->ShowMainControls    = SmbShowMainControls;
    h->HideMainControls    = SmbHideMainControls;
    h->CreateAdvControls   = SmbCreateAdvControls;
    h->ShowAdvControls     = SmbShowAdvControls;
    h->HideAdvControls     = SmbHideAdvControls;
    h->DestroyControls     = SmbDestroyControls;
    h->UpdateAdvPositions  = SmbUpdateAdvPositions;
    h->GetAdvContentHeight = SmbGetAdvContentHeight;
    h->HandleCtlColor      = SmbHandleCtlColor;
    h->HandleCommand       = SmbHandleCommand;
    h->LoadConfig          = SmbLoadConfig;
    h->SaveConfig          = SmbSaveConfig;
    h->SaveAdvSettingsFromUI = SmbSaveAdvSettingsFromUI;
    h->ResetAdvSettings    = SmbResetAdvSettings;
    h->ExecuteMount        = SmbExecuteMount;
    h->Destroy             = SmbDestroy;

    return h;
}