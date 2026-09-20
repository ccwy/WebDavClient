#include "protocol_sftp.h"
#include "protocol.h"
#include "config.h"
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
   高级设置对话框控件 ID（SFTP 专属 + 通用 VFS）
   参数来源: https://rclone.org/sftp/
   ====================================================================== */
/* SFTP 专属控件 ID */
#define SFTP_IDC_ADV_EDIT_KEY_FILE_PASS    401
#define SFTP_IDC_ADV_COMBO_SHELL           402
#define SFTP_IDC_ADV_EDIT_IDLE_TIMEOUT     403
#define SFTP_IDC_ADV_CHECK_INSECURE_CIPHER 404
#define SFTP_IDC_ADV_CHECK_DISABLE_HASH    405
#define SFTP_IDC_ADV_CHECK_SET_MODTIME     406
#define SFTP_IDC_ADV_CHECK_SKIP_LINKS      407
#define SFTP_IDC_ADV_BTN_BACK              408
#define SFTP_IDC_ADV_BTN_SAVE              409
#define SFTP_IDC_ADV_BTN_RESET             410
/* 通用 VFS 控件 ID */
#define SFTP_IDC_ADV_COMBO_VFS             411
#define SFTP_IDC_ADV_EDIT_DCT              412
#define SFTP_IDC_ADV_EDIT_BS               413
#define SFTP_IDC_ADV_EDIT_TR               414
#define SFTP_IDC_ADV_EDIT_CD               415
#define SFTP_IDC_ADV_EDIT_CMA              416
#define SFTP_IDC_ADV_EDIT_RCS              417
#define SFTP_IDC_ADV_EDIT_RCSL             418
#define SFTP_IDC_ADV_EDIT_VOLNAME          419
#define SFTP_IDC_ADV_EDIT_VCMS             420
#define SFTP_IDC_ADV_BTN_BROWSE            421
#define SFTP_IDC_ADV_BTN_CLEAR_CACHE       422

/* ======================================================================
   内部辅助：创建带字体的控件
   ====================================================================== */
static HWND SftpCreateStyledExW(DWORD dwExStyle, LPCWSTR cls, LPCWSTR text,
                                 DWORD style, int x, int y, int w, int h,
                                 HWND parent, HMENU id, HFONT font) {
    HWND hw = CreateWindowExW(dwExStyle, cls, text, style, x, y, w, h, parent, id, NULL, NULL);
    if (hw && font) SendMessageW(hw, WM_SETFONT, (WPARAM)font, TRUE);
    return hw;
}

static HWND SftpCreateStyledExA(DWORD dwExStyle, LPCSTR cls, LPCSTR text,
                                 DWORD style, int x, int y, int w, int h,
                                 HWND parent, HMENU id, HFONT font) {
    HWND hw = CreateWindowExA(dwExStyle, cls, text, style, x, y, w, h, parent, id, NULL, NULL);
    if (hw && font) SendMessageW(hw, WM_SETFONT, (WPARAM)font, TRUE);
    return hw;
}

static HWND SftpCreateBoldLabel(LPCWSTR text, int x, int y, int w, int h,
                                 HWND parent, HFONT boldFont) {
    HWND hw = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                                 x, y, w, h, parent, NULL, NULL, NULL);
    if (hw && boldFont) SendMessageW(hw, WM_SETFONT, (WPARAM)boldFont, TRUE);
    return hw;
}

/* ======================================================================
   配置加载 / 保存（委托给 config.c 统一读写 config.ini，sftp_ 前缀键）
   ====================================================================== */
static void SftpLoadConfig(ProtocolHandler* self) {
    SftpData* d = (SftpData*)self->data;
    LoadSftpConfig(&d->cfg);
}

static void SftpSaveConfig(ProtocolHandler* self) {
    SftpData* d = (SftpData*)self->data;
    SaveSftpConfig(&d->cfg);
}

/* ======================================================================
   主页面 UI（5 个协议字段 + Drive 标签，与 SMB/WebDAV 布局对齐）
   ====================================================================== */
static void SftpCreateMainControls(ProtocolHandler* self, HWND hwnd,
                                    HFONT hFont, HFONT hBoldFont) {
    SftpData* d = (SftpData*)self->data;

    d->hMainLabels[0] = SftpCreateBoldLabel(TR("SFTP_STR_HOST"), 30, 70, 110, 28, hwnd, hBoldFont);
    d->hHostBox = SftpCreateStyledExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.host,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 70, 390, 28, hwnd, NULL, hFont);

    d->hMainLabels[1] = SftpCreateBoldLabel(TR("SFTP_STR_PORT"), 30, 115, 110, 28, hwnd, hBoldFont);
    d->hPortBox = SftpCreateStyledExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.port,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, 145, 115, 130, 28, hwnd, NULL, hFont);

    d->hMainLabels[2] = SftpCreateBoldLabel(TR("SFTP_STR_USER"), 30, 160, 110, 28, hwnd, hBoldFont);
    d->hUserBox = SftpCreateStyledExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.user,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 160, 390, 28, hwnd, NULL, hFont);

    d->hMainLabels[3] = SftpCreateBoldLabel(TR("SFTP_STR_PASS"), 30, 205, 110, 28, hwnd, hBoldFont);
    d->hPassBox = SftpCreateStyledExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.pass,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD, 145, 205, 390, 28, hwnd, NULL, hFont);

    d->hMainLabels[4] = SftpCreateBoldLabel(TR("SFTP_STR_KEY_FILE"), 30, 250, 110, 28, hwnd, hBoldFont);
    d->hKeyFileBox = SftpCreateStyledExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.key_file,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 250, 390, 28, hwnd, NULL, hFont);

    /* Drive 标签（与 main.c 的 hDriveBox 对齐，编辑框由 main.c 创建） */
    d->hMainLabels[5] = SftpCreateBoldLabel(TR("STR_DRIVE"), 30, 295, 110, 28, hwnd, hBoldFont);
}

static void SftpShowMainControls(ProtocolHandler* self, HWND hwnd) {
    SftpData* d = (SftpData*)self->data;
    int i;
    for (i = 0; i < 6; i++) ShowWindow(d->hMainLabels[i], SW_SHOW);
    ShowWindow(d->hHostBox, SW_SHOW);
    ShowWindow(d->hPortBox, SW_SHOW);
    ShowWindow(d->hUserBox, SW_SHOW);
    ShowWindow(d->hPassBox, SW_SHOW);
    ShowWindow(d->hKeyFileBox, SW_SHOW);
}

static void SftpHideMainControls(ProtocolHandler* self, HWND hwnd) {
    SftpData* d = (SftpData*)self->data;
    int i;
    for (i = 0; i < 6; i++) ShowWindow(d->hMainLabels[i], SW_HIDE);
    ShowWindow(d->hHostBox, SW_HIDE);
    ShowWindow(d->hPortBox, SW_HIDE);
    ShowWindow(d->hUserBox, SW_HIDE);
    ShowWindow(d->hPassBox, SW_HIDE);
    ShowWindow(d->hKeyFileBox, SW_HIDE);
}

/* ======================================================================
   高级设置页面 UI（17 行: 7行SFTP专属 + 10行通用VFS/Mount）
   Row 0-6: SFTP 专属参数
   Row 7-16: 通用 VFS/Mount 参数（与 SMB/WebDAV 高级设置布局一致）
   参数来源: https://rclone.org/sftp/
   ====================================================================== */
static void SftpCreateAdvControls(ProtocolHandler* self, HWND hwnd,
                                   HFONT hFont, HFONT hBoldFont, HFONT hDescFont) {
    SftpData* d = (SftpData*)self->data;
    CommonConfig* cc = d->commonCfg;
    int y;
    char transfersStr[16];
    const wchar_t* vfsDesc = NULL;
    int shellIdx = 0;

    d->hAdvDescFont = hDescFont;

    /* ====== SFTP 专属参数（Row 0-6） ====== */

    /* Row 0: Key File Pass (Edit) */
    y = 15;
    d->hAdvLabels[0] = CreateWindowExW(0, L"STATIC", TR("SFTP_STR_ADV_KEY_FILE_PASS"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[0], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[0] = CreateWindowExW(0, L"STATIC", L"--sftp-key-file-pass", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[0], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[0] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.key_file_pass, WS_CHILD | ES_AUTOHSCROLL | ES_PASSWORD, 195, y, 330, 28, hwnd, (HMENU)SFTP_IDC_ADV_EDIT_KEY_FILE_PASS, NULL, NULL);
    SendMessageW(d->hAdvEdits[0], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[0] = CreateWindowExW(0, L"STATIC", TR("SFTP_STR_ADV_HINT_KEY_FILE_PASS"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[0], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 1: Shell Type (ComboBox) */
    y = 105;
    d->hAdvLabels[1] = CreateWindowExW(0, L"STATIC", TR("SFTP_STR_ADV_SHELL_TYPE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[1], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[1] = CreateWindowExW(0, L"STATIC", L"--sftp-shell-type", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[1], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvComboShell = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 195, y, 330, 200, hwnd, (HMENU)SFTP_IDC_ADV_COMBO_SHELL, NULL, NULL);
    SendMessageW(d->hAdvComboShell, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(d->hAdvComboShell, CB_ADDSTRING, 0, (LPARAM)TR("SFTP_STR_SHELL_DEFAULT"));
    SendMessageW(d->hAdvComboShell, CB_ADDSTRING, 0, (LPARAM)TR("SFTP_STR_SHELL_UNIX"));
    SendMessageW(d->hAdvComboShell, CB_ADDSTRING, 0, (LPARAM)TR("SFTP_STR_SHELL_POWERSHELL"));
    SendMessageW(d->hAdvComboShell, CB_ADDSTRING, 0, (LPARAM)TR("SFTP_STR_SHELL_CMD"));
    /* 映射 shell_type 值到 ComboBox 索引 */
    if (strcmp(d->cfg.shell_type, "unix") == 0) shellIdx = 1;
    else if (strcmp(d->cfg.shell_type, "powershell") == 0) shellIdx = 2;
    else if (strcmp(d->cfg.shell_type, "cmd") == 0) shellIdx = 3;
    else shellIdx = 0;  /* 默认 "" */
    SendMessageW(d->hAdvComboShell, CB_SETCURSEL, shellIdx, 0);
    d->hAdvDescLabels[1] = CreateWindowExW(0, L"STATIC", TR("SFTP_STR_ADV_HINT_SHELL_TYPE"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[1], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 2: Idle Timeout (Edit) */
    y = 195;
    d->hAdvLabels[2] = CreateWindowExW(0, L"STATIC", TR("SFTP_STR_ADV_IDLE_TIMEOUT"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[2], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[2] = CreateWindowExW(0, L"STATIC", L"--sftp-idle-timeout", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[2], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[1] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.idle_timeout, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SFTP_IDC_ADV_EDIT_IDLE_TIMEOUT, NULL, NULL);
    SendMessageW(d->hAdvEdits[1], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[2] = CreateWindowExW(0, L"STATIC", TR("SFTP_STR_ADV_HINT_IDLE_TIMEOUT"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[2], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 3: Use Insecure Cipher (Checkbox) */
    y = 285;
    d->hAdvLabels[3] = CreateWindowExW(0, L"STATIC", TR("SFTP_STR_ADV_INSECURE_CIPHER"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[3], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[3] = CreateWindowExW(0, L"STATIC", L"--sftp-use-insecure-cipher", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[3], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvChecks[0] = CreateWindowExW(0, L"BUTTON", TR("SFTP_STR_ADV_ENABLE"), WS_CHILD | BS_AUTOCHECKBOX, 195, y + 3, 330, 25, hwnd, (HMENU)SFTP_IDC_ADV_CHECK_INSECURE_CIPHER, NULL, NULL);
    SendMessageW(d->hAdvChecks[0], WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(d->hAdvChecks[0], BM_SETCHECK, d->cfg.use_insecure_cipher ? BST_CHECKED : BST_UNCHECKED, 0);
    d->hAdvDescLabels[3] = CreateWindowExW(0, L"STATIC", TR("SFTP_STR_ADV_HINT_INSECURE_CIPHER"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[3], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 4: Disable Hashcheck (Checkbox) */
    y = 375;
    d->hAdvLabels[4] = CreateWindowExW(0, L"STATIC", TR("SFTP_STR_ADV_DISABLE_HASHCHECK"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[4], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[4] = CreateWindowExW(0, L"STATIC", L"--sftp-disable-hashcheck", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[4], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvChecks[1] = CreateWindowExW(0, L"BUTTON", TR("SFTP_STR_ADV_ENABLE"), WS_CHILD | BS_AUTOCHECKBOX, 195, y + 3, 330, 25, hwnd, (HMENU)SFTP_IDC_ADV_CHECK_DISABLE_HASH, NULL, NULL);
    SendMessageW(d->hAdvChecks[1], WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(d->hAdvChecks[1], BM_SETCHECK, d->cfg.disable_hashcheck ? BST_CHECKED : BST_UNCHECKED, 0);
    d->hAdvDescLabels[4] = CreateWindowExW(0, L"STATIC", TR("SFTP_STR_ADV_HINT_DISABLE_HASHCHECK"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[4], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 5: Set Modtime (Checkbox) */
    y = 465;
    d->hAdvLabels[5] = CreateWindowExW(0, L"STATIC", TR("SFTP_STR_ADV_SET_MODTIME"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[5], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[5] = CreateWindowExW(0, L"STATIC", L"--sftp-set-modtime", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[5], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvChecks[2] = CreateWindowExW(0, L"BUTTON", TR("SFTP_STR_ADV_ENABLE"), WS_CHILD | BS_AUTOCHECKBOX, 195, y + 3, 330, 25, hwnd, (HMENU)SFTP_IDC_ADV_CHECK_SET_MODTIME, NULL, NULL);
    SendMessageW(d->hAdvChecks[2], WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(d->hAdvChecks[2], BM_SETCHECK, d->cfg.set_modtime ? BST_CHECKED : BST_UNCHECKED, 0);
    d->hAdvDescLabels[5] = CreateWindowExW(0, L"STATIC", TR("SFTP_STR_ADV_HINT_SET_MODTIME"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[5], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 6: Skip Links (Checkbox) */
    y = 555;
    d->hAdvLabels[6] = CreateWindowExW(0, L"STATIC", TR("SFTP_STR_ADV_SKIP_LINKS"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[6], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[6] = CreateWindowExW(0, L"STATIC", L"--sftp-skip-links", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[6], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvChecks[3] = CreateWindowExW(0, L"BUTTON", TR("SFTP_STR_ADV_ENABLE"), WS_CHILD | BS_AUTOCHECKBOX, 195, y + 3, 330, 25, hwnd, (HMENU)SFTP_IDC_ADV_CHECK_SKIP_LINKS, NULL, NULL);
    SendMessageW(d->hAdvChecks[3], WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(d->hAdvChecks[3], BM_SETCHECK, d->cfg.skip_links ? BST_CHECKED : BST_UNCHECKED, 0);
    d->hAdvDescLabels[6] = CreateWindowExW(0, L"STATIC", TR("SFTP_STR_ADV_HINT_SKIP_LINKS"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[6], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* ====== 通用 VFS/Mount 参数（Row 7-16，使用 CommonConfig 数据，STR_ 前缀） ====== */

    /* Row 7: vfs-cache-mode ComboBox */
    y = 645;
    d->hAdvLabels[7] = CreateWindowExW(0, L"STATIC", TR("STR_VFS_CACHE_MODE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[7], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[7] = CreateWindowExW(0, L"STATIC", L"--vfs-cache-mode", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[7], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvComboVfs = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 195, y, 330, 200, hwnd, (HMENU)SFTP_IDC_ADV_COMBO_VFS, NULL, NULL);
    SendMessageW(d->hAdvComboVfs, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(d->hAdvComboVfs, CB_ADDSTRING, 0, (LPARAM)TR("STR_VFS_CACHE_OFF"));
    SendMessageW(d->hAdvComboVfs, CB_ADDSTRING, 0, (LPARAM)TR("STR_VFS_CACHE_MINIMAL"));
    SendMessageW(d->hAdvComboVfs, CB_ADDSTRING, 0, (LPARAM)TR("STR_VFS_CACHE_WRITES"));
    SendMessageW(d->hAdvComboVfs, CB_ADDSTRING, 0, (LPARAM)TR("STR_VFS_CACHE_FULL"));
    SendMessageW(d->hAdvComboVfs, CB_SETCURSEL, (WPARAM)cc->vfs_cache_mode, 0);
    switch (cc->vfs_cache_mode) {
        case 0: vfsDesc = TR("STR_VFS_TIP_OFF"); break;
        case 1: vfsDesc = TR("STR_VFS_TIP_MINIMAL"); break;
        case 2: vfsDesc = TR("STR_VFS_TIP_WRITES"); break;
        case 3: vfsDesc = TR("STR_VFS_TIP_FULL"); break;
        default: vfsDesc = TR("STR_VFS_TIP_WRITES"); break;
    }
    d->hAdvDescLabels[7] = CreateWindowExW(0, L"STATIC", vfsDesc, WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[7], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 8: dir-cache-time */
    y = 735;
    d->hAdvLabels[8] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_DIR_CACHE_TIME"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[8], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[8] = CreateWindowExW(0, L"STATIC", L"--dir-cache-time", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[8], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[2] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->dir_cache_time, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SFTP_IDC_ADV_EDIT_DCT, NULL, NULL);
    SendMessageW(d->hAdvEdits[2], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[8] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_DIR_CACHE_TIME"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[8], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 9: buffer-size */
    y = 825;
    d->hAdvLabels[9] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_BUFFER_SIZE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[9], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[9] = CreateWindowExW(0, L"STATIC", L"--buffer-size", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[9], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[3] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->buffer_size, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SFTP_IDC_ADV_EDIT_BS, NULL, NULL);
    SendMessageW(d->hAdvEdits[3], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[9] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_BUFFER_SIZE"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[9], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 10: transfers */
    y = 915;
    d->hAdvLabels[10] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_TRANSFERS"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[10], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[10] = CreateWindowExW(0, L"STATIC", L"--transfers", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[10], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    sprintf_s(transfersStr, sizeof(transfersStr), "%d", cc->transfers);
    d->hAdvEdits[4] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", transfersStr, WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 195, y, 330, 28, hwnd, (HMENU)SFTP_IDC_ADV_EDIT_TR, NULL, NULL);
    SendMessageW(d->hAdvEdits[4], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[10] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_TRANSFERS"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[10], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 11: cache-dir (narrower edit + browse button) */
    y = 1005;
    d->hAdvLabels[11] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_CACHE_DIR"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[11], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[11] = CreateWindowExW(0, L"STATIC", L"--cache-dir", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[11], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[5] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->cache_dir, WS_CHILD | ES_AUTOHSCROLL, 195, y, 260, 28, hwnd, (HMENU)SFTP_IDC_ADV_EDIT_CD, NULL, NULL);
    SendMessageW(d->hAdvEdits[5], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvBtnBrowse = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | BS_PUSHBUTTON, 465, y, 60, 25, hwnd, (HMENU)SFTP_IDC_ADV_BTN_BROWSE, NULL, NULL);
    SendMessageW(d->hAdvBtnBrowse, WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[11] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_CACHE_DIR"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[11], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 12: vfs-cache-max-age */
    y = 1095;
    d->hAdvLabels[12] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_CACHE_MAX_AGE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[12], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[12] = CreateWindowExW(0, L"STATIC", L"--vfs-cache-max-age", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[12], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[6] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->vfs_cache_max_age, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SFTP_IDC_ADV_EDIT_CMA, NULL, NULL);
    SendMessageW(d->hAdvEdits[6], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[12] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VFS_CACHE_MAX_AGE"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[12], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 13: vfs-read-chunk-size */
    y = 1185;
    d->hAdvLabels[13] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_READ_CHUNK"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[13], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[13] = CreateWindowExW(0, L"STATIC", L"--vfs-read-chunk-size", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[13], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[7] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->vfs_read_chunk_size, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SFTP_IDC_ADV_EDIT_RCS, NULL, NULL);
    SendMessageW(d->hAdvEdits[7], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[13] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VFS_READ_CHUNK"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[13], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 14: vfs-read-chunk-size-limit */
    y = 1275;
    d->hAdvLabels[14] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_READ_CHUNK_LIMIT"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[14], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[14] = CreateWindowExW(0, L"STATIC", L"--vfs-read-chunk-size-limit", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[14], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[8] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->vfs_read_chunk_size_limit, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SFTP_IDC_ADV_EDIT_RCSL, NULL, NULL);
    SendMessageW(d->hAdvEdits[8], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[14] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VFS_READ_CHUNK_LIMIT"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[14], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 15: volname */
    y = 1365;
    d->hAdvLabels[15] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VOLNAME"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[15], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[15] = CreateWindowExW(0, L"STATIC", L"--volname", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[15], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[9] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->volname, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SFTP_IDC_ADV_EDIT_VOLNAME, NULL, NULL);
    SendMessageW(d->hAdvEdits[9], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[15] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VOLNAME"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[15], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 16: vfs-cache-max-size */
    y = 1455;
    d->hAdvLabels[16] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_CACHE_MAX_SIZE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[16], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[16] = CreateWindowExW(0, L"STATIC", L"--vfs-cache-max-size", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[16], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[10] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->vfs_cache_max_size, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SFTP_IDC_ADV_EDIT_VCMS, NULL, NULL);
    SendMessageW(d->hAdvEdits[10], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[16] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VFS_CACHE_MAX_SIZE"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[16], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Bottom buttons */
    y = 1545;
    d->hAdvBtnBack = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_BACK"), WS_CHILD | BS_PUSHBUTTON, 20, y, 121, 32, hwnd, (HMENU)SFTP_IDC_ADV_BTN_BACK, NULL, NULL);
    SendMessageW(d->hAdvBtnBack, WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvBtnClearCache = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_CLEAR_CACHE"), WS_CHILD | BS_PUSHBUTTON, 148, y, 121, 32, hwnd, (HMENU)SFTP_IDC_ADV_BTN_CLEAR_CACHE, NULL, NULL);
    SendMessageW(d->hAdvBtnClearCache, WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvBtnSave = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_OK"), WS_CHILD | BS_PUSHBUTTON, 276, y, 121, 32, hwnd, (HMENU)SFTP_IDC_ADV_BTN_SAVE, NULL, NULL);
    SendMessageW(d->hAdvBtnSave, WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvBtnReset = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_RESET"), WS_CHILD | BS_PUSHBUTTON, 404, y, 121, 32, hwnd, (HMENU)SFTP_IDC_ADV_BTN_RESET, NULL, NULL);
    SendMessageW(d->hAdvBtnReset, WM_SETFONT, (WPARAM)hFont, TRUE);
}

static void SftpShowAdvControls(ProtocolHandler* self, HWND hwnd) {
    SftpData* d = (SftpData*)self->data;
    int i;
    for (i = 0; i < 17; i++) {
        ShowWindow(d->hAdvLabels[i], SW_SHOW);
        ShowWindow(d->hAdvParamLabels[i], SW_SHOW);
        ShowWindow(d->hAdvDescLabels[i], SW_SHOW);
    }
    for (i = 0; i < 11; i++) ShowWindow(d->hAdvEdits[i], SW_SHOW);
    for (i = 0; i < 4; i++) ShowWindow(d->hAdvChecks[i], SW_SHOW);
    ShowWindow(d->hAdvComboVfs, SW_SHOW);
    ShowWindow(d->hAdvComboShell, SW_SHOW);
    ShowWindow(d->hAdvBtnBrowse, SW_SHOW);
    ShowWindow(d->hAdvBtnBack, SW_SHOW);
    ShowWindow(d->hAdvBtnClearCache, SW_SHOW);
    ShowWindow(d->hAdvBtnSave, SW_SHOW);
    ShowWindow(d->hAdvBtnReset, SW_SHOW);
}

static void SftpHideAdvControls(ProtocolHandler* self, HWND hwnd) {
    SftpData* d = (SftpData*)self->data;
    int i;
    for (i = 0; i < 17; i++) {
        ShowWindow(d->hAdvLabels[i], SW_HIDE);
        ShowWindow(d->hAdvParamLabels[i], SW_HIDE);
        ShowWindow(d->hAdvDescLabels[i], SW_HIDE);
    }
    for (i = 0; i < 11; i++) ShowWindow(d->hAdvEdits[i], SW_HIDE);
    for (i = 0; i < 4; i++) ShowWindow(d->hAdvChecks[i], SW_HIDE);
    ShowWindow(d->hAdvComboVfs, SW_HIDE);
    ShowWindow(d->hAdvComboShell, SW_HIDE);
    ShowWindow(d->hAdvBtnBrowse, SW_HIDE);
    ShowWindow(d->hAdvBtnBack, SW_HIDE);
    ShowWindow(d->hAdvBtnClearCache, SW_HIDE);
    ShowWindow(d->hAdvBtnSave, SW_HIDE);
    ShowWindow(d->hAdvBtnReset, SW_HIDE);
}

static void SftpDestroyControls(ProtocolHandler* self, HWND hwnd) {
    SftpData* d = (SftpData*)self->data;
    int i;
    /* 销毁主页面控件 */
    for (i = 0; i < 6; i++) { if (d->hMainLabels[i]) { DestroyWindow(d->hMainLabels[i]); } d->hMainLabels[i] = NULL; }
    if (d->hHostBox)    { DestroyWindow(d->hHostBox);    d->hHostBox = NULL; }
    if (d->hPortBox)    { DestroyWindow(d->hPortBox);    d->hPortBox = NULL; }
    if (d->hUserBox)    { DestroyWindow(d->hUserBox);    d->hUserBox = NULL; }
    if (d->hPassBox)    { DestroyWindow(d->hPassBox);    d->hPassBox = NULL; }
    if (d->hKeyFileBox) { DestroyWindow(d->hKeyFileBox); d->hKeyFileBox = NULL; }
    /* 销毁高级设置控件 */
    for (i = 0; i < 17; i++) {
        if (d->hAdvLabels[i])      { DestroyWindow(d->hAdvLabels[i]);      d->hAdvLabels[i] = NULL; }
        if (d->hAdvParamLabels[i]) { DestroyWindow(d->hAdvParamLabels[i]); d->hAdvParamLabels[i] = NULL; }
        if (d->hAdvDescLabels[i])  { DestroyWindow(d->hAdvDescLabels[i]);  d->hAdvDescLabels[i] = NULL; }
    }
    for (i = 0; i < 11; i++) { if (d->hAdvEdits[i]) { DestroyWindow(d->hAdvEdits[i]); d->hAdvEdits[i] = NULL; } }
    for (i = 0; i < 4; i++) { if (d->hAdvChecks[i]) { DestroyWindow(d->hAdvChecks[i]); d->hAdvChecks[i] = NULL; } }
    if (d->hAdvComboVfs)      { DestroyWindow(d->hAdvComboVfs);      d->hAdvComboVfs = NULL; }
    if (d->hAdvComboShell)    { DestroyWindow(d->hAdvComboShell);    d->hAdvComboShell = NULL; }
    if (d->hAdvBtnBrowse)     { DestroyWindow(d->hAdvBtnBrowse);     d->hAdvBtnBrowse = NULL; }
    if (d->hAdvBtnBack)       { DestroyWindow(d->hAdvBtnBack);       d->hAdvBtnBack = NULL; }
    if (d->hAdvBtnClearCache) { DestroyWindow(d->hAdvBtnClearCache); d->hAdvBtnClearCache = NULL; }
    if (d->hAdvBtnSave)       { DestroyWindow(d->hAdvBtnSave);       d->hAdvBtnSave = NULL; }
    if (d->hAdvBtnReset)      { DestroyWindow(d->hAdvBtnReset);      d->hAdvBtnReset = NULL; }
}

static void SftpUpdateAdvPositions(ProtocolHandler* self, int scrollPos) {
    SftpData* d = (SftpData*)self->data;
    HDWP hdwp;
    int y;
    hdwp = BeginDeferWindowPos(76);  /* 17*4 + 4 checkboxes + comboVfs + comboShell + browse + 4 buttons = 76 */
    if (!hdwp) return;

    /* Row 0: Key File Pass */
    y = 15 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[0], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[0], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[0], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[0], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 1: Shell Type */
    y = 105 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[1], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[1], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvComboShell, NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[1], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 2: Idle Timeout */
    y = 195 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[2], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[2], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[1], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[2], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 3: Use Insecure Cipher */
    y = 285 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[3], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[3], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvChecks[0], NULL, 195, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[3], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 4: Disable Hashcheck */
    y = 375 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[4], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[4], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvChecks[1], NULL, 195, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[4], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 5: Set Modtime */
    y = 465 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[5], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[5], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvChecks[2], NULL, 195, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[5], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 6: Skip Links */
    y = 555 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[6], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[6], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvChecks[3], NULL, 195, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[6], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 7: vfs-cache-mode ComboBox */
    y = 645 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[7], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[7], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvComboVfs, NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[7], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 8: dir-cache-time */
    y = 735 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[8], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[8], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[2], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[8], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 9: buffer-size */
    y = 825 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[9], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[9], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[3], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[9], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 10: transfers */
    y = 915 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[10], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[10], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[4], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[10], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 11: cache-dir */
    y = 1005 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[11], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[11], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[5], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnBrowse, NULL, 465, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[11], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 12: vfs-cache-max-age */
    y = 1095 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[12], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[12], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[6], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[12], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 13: vfs-read-chunk-size */
    y = 1185 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[13], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[13], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[7], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[13], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 14: vfs-read-chunk-size-limit */
    y = 1275 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[14], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[14], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[8], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[14], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 15: volname */
    y = 1365 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[15], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[15], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[9], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[15], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 16: vfs-cache-max-size */
    y = 1455 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[16], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[16], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[10], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[16], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Bottom buttons */
    y = 1545 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnBack, NULL, 20, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnClearCache, NULL, 148, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnSave, NULL, 276, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnReset, NULL, 404, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    if (hdwp) EndDeferWindowPos(hdwp);
}

static int SftpGetAdvContentHeight(ProtocolHandler* self) {
    (void)self;
    return 1592;  /* 1545 + 32 + 15 */
}

static LRESULT SftpHandleCtlColor(ProtocolHandler* self, HWND hCtrl, HDC hdc) {
    SftpData* d = (SftpData*)self->data;
    int idx;
    for (idx = 0; idx < 17; idx++) {
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
static int SftpHandleCommand(ProtocolHandler* self, HWND hwnd,
                              WPARAM wParam, LPARAM lParam) {
    SftpData* d = (SftpData*)self->data;
    CommonConfig* cc = d->commonCfg;
    (void)lParam;

    if (LOWORD(wParam) == SFTP_IDC_ADV_BTN_SAVE) {
        char transfersBuf[16];
        int vfsSel;
        int shellSel;
        /* 从 UI 读取 SFTP 专属设置 */
        GetWindowTextA(d->hAdvEdits[0], d->cfg.key_file_pass, sizeof(d->cfg.key_file_pass));
        shellSel = (int)SendMessageW(d->hAdvComboShell, CB_GETCURSEL, 0, 0);
        switch (shellSel) {
            case 1: strcpy_s(d->cfg.shell_type, sizeof(d->cfg.shell_type), "unix"); break;
            case 2: strcpy_s(d->cfg.shell_type, sizeof(d->cfg.shell_type), "powershell"); break;
            case 3: strcpy_s(d->cfg.shell_type, sizeof(d->cfg.shell_type), "cmd"); break;
            default: d->cfg.shell_type[0] = '\0'; break;
        }
        GetWindowTextA(d->hAdvEdits[1], d->cfg.idle_timeout, sizeof(d->cfg.idle_timeout));
        d->cfg.use_insecure_cipher = (SendMessageW(d->hAdvChecks[0], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
        d->cfg.disable_hashcheck = (SendMessageW(d->hAdvChecks[1], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
        d->cfg.set_modtime = (SendMessageW(d->hAdvChecks[2], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
        d->cfg.skip_links = (SendMessageW(d->hAdvChecks[3], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
        /* 从 UI 读取通用 VFS 设置到 CommonConfig */
        GetWindowTextA(d->hAdvEdits[2], cc->dir_cache_time, sizeof(cc->dir_cache_time));
        GetWindowTextA(d->hAdvEdits[3], cc->buffer_size, sizeof(cc->buffer_size));
        memset(transfersBuf, 0, sizeof(transfersBuf));
        GetWindowTextA(d->hAdvEdits[4], transfersBuf, sizeof(transfersBuf));
        cc->transfers = atoi(transfersBuf);
        if (cc->transfers <= 0) cc->transfers = 4;
        GetWindowTextA(d->hAdvEdits[5], cc->cache_dir, sizeof(cc->cache_dir));
        GetWindowTextA(d->hAdvEdits[6], cc->vfs_cache_max_age, sizeof(cc->vfs_cache_max_age));
        GetWindowTextA(d->hAdvEdits[7], cc->vfs_read_chunk_size, sizeof(cc->vfs_read_chunk_size));
        GetWindowTextA(d->hAdvEdits[8], cc->vfs_read_chunk_size_limit, sizeof(cc->vfs_read_chunk_size_limit));
        GetWindowTextA(d->hAdvEdits[9], cc->volname, sizeof(cc->volname));
        GetWindowTextA(d->hAdvEdits[10], cc->vfs_cache_max_size, sizeof(cc->vfs_cache_max_size));
        vfsSel = (int)SendMessageW(d->hAdvComboVfs, CB_GETCURSEL, 0, 0);
        cc->vfs_cache_mode = (vfsSel != CB_ERR) ? vfsSel : 2;
        self->SaveConfig(self);
        SaveCommonConfig(cc);
        return 2;  /* 已处理：保存后请求 main.c 切换回主页面 */
    }
    else if (LOWORD(wParam) == SFTP_IDC_ADV_BTN_RESET) {
        /* 重置 SFTP 专属设置为默认值 */
        SetWindowTextA(d->hAdvEdits[0], "");
        SendMessageW(d->hAdvComboShell, CB_SETCURSEL, 0, 0);
        SetWindowTextA(d->hAdvEdits[1], "1m0s");
        SendMessageW(d->hAdvChecks[0], BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(d->hAdvChecks[1], BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(d->hAdvChecks[2], BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(d->hAdvChecks[3], BM_SETCHECK, BST_UNCHECKED, 0);
        /* 重置通用 VFS 设置为默认值 */
        SetWindowTextA(d->hAdvEdits[2], "24h");
        SetWindowTextA(d->hAdvEdits[3], "64M");
        SetWindowTextA(d->hAdvEdits[4], "4");
        SetWindowTextA(d->hAdvEdits[5], "");
        SetWindowTextA(d->hAdvEdits[6], "24h");
        SetWindowTextA(d->hAdvEdits[7], "128M");
        SetWindowTextA(d->hAdvEdits[8], "off");
        SetWindowTextA(d->hAdvEdits[9], "Network_Disk");
        SetWindowTextA(d->hAdvEdits[10], "15G");
        SendMessageW(d->hAdvComboVfs, CB_SETCURSEL, 3, 0);  /* full */
        SetWindowTextW(d->hAdvDescLabels[7], TR("STR_VFS_TIP_FULL"));
        return 1;
    }
    else if (LOWORD(wParam) == SFTP_IDC_ADV_BTN_CLEAR_CACHE) {
        char cacheDir[MAX_PATH];
        wchar_t wCacheDir[MAX_PATH];
        int isDefaultDir = 0;
        GetWindowTextA(d->hAdvEdits[5], cacheDir, sizeof(cacheDir));
        if (cacheDir[0] == '\0') {
            wchar_t localAppData[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
                swprintf_s(wCacheDir, MAX_PATH, L"%s\\rclone", localAppData);
                WideCharToMultiByte(CP_ACP, 0, wCacheDir, -1, cacheDir, MAX_PATH, NULL, NULL);
                isDefaultDir = 1;
            } else {
                MessageBoxW(hwnd, TR("MSG_CLEAR_CACHE_FAIL"), TR("MSG_ERROR"), MB_OK | MB_ICONERROR);
            }
        } else {
            MultiByteToWideChar(CP_ACP, 0, cacheDir, -1, wCacheDir, MAX_PATH);
        }
        if (cacheDir[0] != '\0') {
            int confirm = MessageBoxW(hwnd, TR("MSG_CLEAR_CACHE_CONFIRM"), TR("MSG_INFO"), MB_YESNO | MB_ICONQUESTION);
            if (confirm == IDYES) {
                if (GetFileAttributesW(wCacheDir) == INVALID_FILE_ATTRIBUTES) {
                    MessageBoxW(hwnd, TR("MSG_CLEAR_CACHE_EMPTY"), TR("MSG_INFO"), MB_OK | MB_ICONINFORMATION);
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
                        MessageBoxW(hwnd, TR("MSG_CLEAR_CACHE_OK"), TR("MSG_INFO"), MB_OK | MB_ICONINFORMATION);
                    } else {
                        MessageBoxW(hwnd, TR("MSG_CLEAR_CACHE_FAIL"), TR("MSG_ERROR"), MB_OK | MB_ICONERROR);
                    }
                }
            }
        }
        return 1;
    }
    else if (LOWORD(wParam) == SFTP_IDC_ADV_COMBO_VFS && HIWORD(wParam) == CBN_SELCHANGE) {
        int vfsSel = (int)SendMessageW(d->hAdvComboVfs, CB_GETCURSEL, 0, 0);
        if (vfsSel != CB_ERR) {
            const wchar_t* vfsDesc = NULL;
            switch (vfsSel) {
                case 0: vfsDesc = TR("STR_VFS_TIP_OFF"); break;
                case 1: vfsDesc = TR("STR_VFS_TIP_MINIMAL"); break;
                case 2: vfsDesc = TR("STR_VFS_TIP_WRITES"); break;
                case 3: vfsDesc = TR("STR_VFS_TIP_FULL"); break;
                default: vfsDesc = TR("STR_VFS_TIP_WRITES"); break;
            }
            SetWindowTextW(d->hAdvDescLabels[7], vfsDesc);
        }
        return 1;
    }
    else if (LOWORD(wParam) == SFTP_IDC_ADV_BTN_BROWSE) {
        BROWSEINFOW bi;
        LPITEMIDLIST pidl;
        wchar_t selectedPath[MAX_PATH];
        char ansiPath[MAX_PATH];
        memset(&bi, 0, sizeof(bi));
        bi.hwndOwner = hwnd;
        bi.lpszTitle = TR("STR_ADV_CACHE_DIR");
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        pidl = SHBrowseForFolderW(&bi);
        if (pidl) {
            if (SHGetPathFromIDListW(pidl, selectedPath)) {
                WideCharToMultiByte(CP_ACP, 0, selectedPath, -1, ansiPath, MAX_PATH, NULL, NULL);
                SetWindowTextA(d->hAdvEdits[5], ansiPath);
            }
            CoTaskMemFree(pidl);
        }
        return 1;
    }
    else if (LOWORD(wParam) == SFTP_IDC_ADV_BTN_BACK) {
        return 2;  /* 请求 main.c 切换回主页面 */
    }

    return 0;  /* 未处理 */
}

/* ======================================================================
   高级设置 UI ↔ 配置 同步
   ====================================================================== */
static void SftpSaveAdvSettingsFromUI(ProtocolHandler* self) {
    SftpData* d = (SftpData*)self->data;
    CommonConfig* cc = d->commonCfg;
    char transfersBuf[16];
    int vfsSel;
    int shellSel;
    /* SFTP 专属设置 → SftpConfig */
    GetWindowTextA(d->hAdvEdits[0], d->cfg.key_file_pass, sizeof(d->cfg.key_file_pass));
    shellSel = (int)SendMessageW(d->hAdvComboShell, CB_GETCURSEL, 0, 0);
    switch (shellSel) {
        case 1: strcpy_s(d->cfg.shell_type, sizeof(d->cfg.shell_type), "unix"); break;
        case 2: strcpy_s(d->cfg.shell_type, sizeof(d->cfg.shell_type), "powershell"); break;
        case 3: strcpy_s(d->cfg.shell_type, sizeof(d->cfg.shell_type), "cmd"); break;
        default: d->cfg.shell_type[0] = '\0'; break;
    }
    GetWindowTextA(d->hAdvEdits[1], d->cfg.idle_timeout, sizeof(d->cfg.idle_timeout));
    d->cfg.use_insecure_cipher = (SendMessageW(d->hAdvChecks[0], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    d->cfg.disable_hashcheck = (SendMessageW(d->hAdvChecks[1], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    d->cfg.set_modtime = (SendMessageW(d->hAdvChecks[2], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    d->cfg.skip_links = (SendMessageW(d->hAdvChecks[3], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    /* 通用 VFS 设置 → CommonConfig */
    GetWindowTextA(d->hAdvEdits[2], cc->dir_cache_time, sizeof(cc->dir_cache_time));
    GetWindowTextA(d->hAdvEdits[3], cc->buffer_size, sizeof(cc->buffer_size));
    memset(transfersBuf, 0, sizeof(transfersBuf));
    GetWindowTextA(d->hAdvEdits[4], transfersBuf, sizeof(transfersBuf));
    cc->transfers = atoi(transfersBuf);
    if (cc->transfers <= 0) cc->transfers = 4;
    GetWindowTextA(d->hAdvEdits[5], cc->cache_dir, sizeof(cc->cache_dir));
    GetWindowTextA(d->hAdvEdits[6], cc->vfs_cache_max_age, sizeof(cc->vfs_cache_max_age));
    GetWindowTextA(d->hAdvEdits[7], cc->vfs_read_chunk_size, sizeof(cc->vfs_read_chunk_size));
    GetWindowTextA(d->hAdvEdits[8], cc->vfs_read_chunk_size_limit, sizeof(cc->vfs_read_chunk_size_limit));
    GetWindowTextA(d->hAdvEdits[9], cc->volname, sizeof(cc->volname));
    GetWindowTextA(d->hAdvEdits[10], cc->vfs_cache_max_size, sizeof(cc->vfs_cache_max_size));
    vfsSel = (int)SendMessageW(d->hAdvComboVfs, CB_GETCURSEL, 0, 0);
    cc->vfs_cache_mode = (vfsSel != CB_ERR) ? vfsSel : 2;
}

static void SftpResetAdvSettings(ProtocolHandler* self) {
    SftpData* d = (SftpData*)self->data;
    /* 重置 SFTP 专属设置为默认值 */
    SetWindowTextA(d->hAdvEdits[0], "");
    SendMessageW(d->hAdvComboShell, CB_SETCURSEL, 0, 0);
    SetWindowTextA(d->hAdvEdits[1], "1m0s");
    SendMessageW(d->hAdvChecks[0], BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(d->hAdvChecks[1], BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(d->hAdvChecks[2], BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(d->hAdvChecks[3], BM_SETCHECK, BST_UNCHECKED, 0);
    /* 重置通用 VFS 设置为默认值 */
    SetWindowTextA(d->hAdvEdits[2], "24h");
    SetWindowTextA(d->hAdvEdits[3], "64M");
    SetWindowTextA(d->hAdvEdits[4], "4");
    SetWindowTextA(d->hAdvEdits[5], "");
    SetWindowTextA(d->hAdvEdits[6], "24h");
    SetWindowTextA(d->hAdvEdits[7], "128M");
    SetWindowTextA(d->hAdvEdits[8], "off");
    SetWindowTextA(d->hAdvEdits[9], "Network_Disk");
    SetWindowTextA(d->hAdvEdits[10], "15G");
    SendMessageW(d->hAdvComboVfs, CB_SETCURSEL, 3, 0);  /* full */
    SetWindowTextW(d->hAdvDescLabels[7], TR("STR_VFS_TIP_FULL"));
}

/* ======================================================================
   挂载执行
   rclone SFTP 命令格式: rclone mount :sftp: X: --sftp-host ... --sftp-user ...
   ====================================================================== */
static int SftpExecuteMount(ProtocolHandler* self, HWND hwnd,
                             const char* rclonePath, int isAuto) {
    SftpData* d = (SftpData*)self->data;
    CommonConfig* cc = d->commonCfg;

    /* 从 UI 读取主页面配置 */
    GetWindowTextA(d->hHostBox, d->cfg.host, sizeof(d->cfg.host));
    GetWindowTextA(d->hPortBox, d->cfg.port, sizeof(d->cfg.port));
    GetWindowTextA(d->hUserBox, d->cfg.user, sizeof(d->cfg.user));
    GetWindowTextA(d->hPassBox, d->cfg.pass, sizeof(d->cfg.pass));
    GetWindowTextA(d->hKeyFileBox, d->cfg.key_file, sizeof(d->cfg.key_file));

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
    SaveCommonConfig(cc);

    LogMessage("INFO", "SFTP mount action triggered with host=%s user=%s", d->cfg.host, d->cfg.user);

    /* 密码混淆 */
    char obscuredPass[256] = { 0 };
    RcloneObscurePassword(rclonePath, d->cfg.pass, obscuredPass, sizeof(obscuredPass));

    /* 密钥文件密码混淆 */
    char obscuredKeyPass[256] = { 0 };
    if (d->cfg.key_file_pass[0] != '\0') {
        RcloneObscurePassword(rclonePath, d->cfg.key_file_pass, obscuredKeyPass, sizeof(obscuredKeyPass));
    }

    /* 构建 SFTP 专属参数 */
    char sftpParams[2048] = { 0 };
    char tmpBuf[512];

    /* --sftp-port（非默认 22 时传递） */
    if (d->cfg.port[0] != '\0' && strcmp(d->cfg.port, "22") != 0) {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--sftp-port \"%s\" ", d->cfg.port);
        strcat_s(sftpParams, sizeof(sftpParams), tmpBuf);
    }

    /* --sftp-key-file（非空时传递） */
    if (d->cfg.key_file[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--sftp-key-file \"%s\" ", d->cfg.key_file);
        strcat_s(sftpParams, sizeof(sftpParams), tmpBuf);
    }

    /* --sftp-key-file-pass（非空时传递） */
    if (obscuredKeyPass[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--sftp-key-file-pass \"%s\" ", obscuredKeyPass);
        strcat_s(sftpParams, sizeof(sftpParams), tmpBuf);
    }

    /* --sftp-shell-type（非空时传递） */
    if (d->cfg.shell_type[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--sftp-shell-type \"%s\" ", d->cfg.shell_type);
        strcat_s(sftpParams, sizeof(sftpParams), tmpBuf);
    }

    /* --sftp-idle-timeout（非默认 1m0s 时传递） */
    if (d->cfg.idle_timeout[0] != '\0' && strcmp(d->cfg.idle_timeout, "1m0s") != 0) {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--sftp-idle-timeout \"%s\" ", d->cfg.idle_timeout);
        strcat_s(sftpParams, sizeof(sftpParams), tmpBuf);
    }

    /* --sftp-use-insecure-cipher（启用时传递） */
    if (d->cfg.use_insecure_cipher) {
        strcat_s(sftpParams, sizeof(sftpParams), "--sftp-use-insecure-cipher ");
    }

    /* --sftp-disable-hashcheck（启用时传递） */
    if (d->cfg.disable_hashcheck) {
        strcat_s(sftpParams, sizeof(sftpParams), "--sftp-disable-hashcheck ");
    }

    /* --sftp-set-modtime（启用时传递） */
    if (d->cfg.set_modtime) {
        strcat_s(sftpParams, sizeof(sftpParams), "--sftp-set-modtime ");
    }

    /* --sftp-skip-links（启用时传递） */
    if (d->cfg.skip_links) {
        strcat_s(sftpParams, sizeof(sftpParams), "--sftp-skip-links ");
    }

    /* 构建 VFS 通用参数 */
    char vfsParams[1024] = { 0 };
    const char* vfsModes[] = { "off", "minimal", "writes", "full" };
    int vfsMode = cc->vfs_cache_mode;
    if (vfsMode < 0 || vfsMode > 3) vfsMode = 2;

    sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-cache-mode %s ", vfsModes[vfsMode]);
    strcat_s(vfsParams, sizeof(vfsParams), tmpBuf);

    if (cc->dir_cache_time[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--dir-cache-time %s ", cc->dir_cache_time);
        strcat_s(vfsParams, sizeof(vfsParams), tmpBuf);
    }
    if (cc->buffer_size[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--buffer-size %s ", cc->buffer_size);
        strcat_s(vfsParams, sizeof(vfsParams), tmpBuf);
    }
    if (cc->transfers > 0) {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--transfers %d ", cc->transfers);
        strcat_s(vfsParams, sizeof(vfsParams), tmpBuf);
    }
    if (cc->cache_dir[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--cache-dir \"%s\" ", cc->cache_dir);
        strcat_s(vfsParams, sizeof(vfsParams), tmpBuf);
    }
    if (cc->vfs_cache_max_age[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-cache-max-age %s ", cc->vfs_cache_max_age);
        strcat_s(vfsParams, sizeof(vfsParams), tmpBuf);
    }
    if (cc->vfs_read_chunk_size[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-read-chunk-size %s ", cc->vfs_read_chunk_size);
        strcat_s(vfsParams, sizeof(vfsParams), tmpBuf);
    }
    if (cc->vfs_read_chunk_size_limit[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-read-chunk-size-limit %s ", cc->vfs_read_chunk_size_limit);
        strcat_s(vfsParams, sizeof(vfsParams), tmpBuf);
    }
    if (cc->vfs_cache_max_size[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-cache-max-size %s ", cc->vfs_cache_max_size);
        strcat_s(vfsParams, sizeof(vfsParams), tmpBuf);
    }
    if (cc->volname[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--volname \"%s\" ", cc->volname);
        strcat_s(vfsParams, sizeof(vfsParams), tmpBuf);
    }

    /* 构建完整挂载命令 */
    char cmd[4096];
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* slash = strrchr(workDir, '\\');
    if (slash) *slash = '\0';

    if (cc->debug_log) {
        char logPath[MAX_PATH];
        sprintf_s(logPath, sizeof(logPath), "%s\\rclone_error.log", workDir);
        sprintf_s(cmd, sizeof(cmd),
            "\"%s\" mount :sftp: %s: --sftp-host \"%s\" --sftp-user \"%s\" --sftp-pass \"%s\" "
            "%s"
            "%s"
            "--log-file \"%s\" -vv",
            rclonePath, cc->drive, d->cfg.host, d->cfg.user, obscuredPass,
            sftpParams, vfsParams, logPath
        );
        LogMessage("INFO", "Starting Rclone SFTP mount with debug logging enabled.");
    } else {
        sprintf_s(cmd, sizeof(cmd),
            "\"%s\" mount :sftp: %s: --sftp-host \"%s\" --sftp-user \"%s\" --sftp-pass \"%s\" "
            "%s"
            "%s",
            rclonePath, cc->drive, d->cfg.host, d->cfg.user, obscuredPass,
            sftpParams, vfsParams
        );
        LogMessage("INFO", "Starting Rclone SFTP mount with debug logging disabled.");
    }

    /* 调用 rclone_manager 执行挂载 */
    if (StartRcloneProcess(cmd, cc->drive)) {
        LogMessage("INFO", "SFTP mount started successfully on drive %s:", cc->drive);
        return 1;
    }

    LogMessage("ERROR", "SFTP mount failed to start. Check rclone_error.log for details.");
    if (!isAuto) MessageBoxW(hwnd, TR("MSG_MOUNT_FAIL"), TR("MSG_ERROR"), MB_OK | MB_ICONERROR);
    return 0;
}

/* ======================================================================
   销毁
   ====================================================================== */
static void SftpDestroy(ProtocolHandler* self) {
    SftpData* d = (SftpData*)self->data;
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
ProtocolHandler* CreateSftpHandler(CommonConfig* commonCfg) {
    SftpData* d = (SftpData*)calloc(1, sizeof(SftpData));
    if (!d) return NULL;
    d->commonCfg = commonCfg;

    /* 设置 SFTP 默认值 */
    strcpy_s(d->cfg.host, sizeof(d->cfg.host), "192.168.5.100");
    strcpy_s(d->cfg.port, sizeof(d->cfg.port), "22");
    strcpy_s(d->cfg.user, sizeof(d->cfg.user), "root");
    d->cfg.pass[0] = '\0';
    d->cfg.key_file[0] = '\0';
    d->cfg.key_file_pass[0] = '\0';
    d->cfg.shell_type[0] = '\0';
    strcpy_s(d->cfg.idle_timeout, sizeof(d->cfg.idle_timeout), "1m0s");
    d->cfg.use_insecure_cipher = 0;
    d->cfg.disable_hashcheck = 0;
    d->cfg.set_modtime = 0;
    d->cfg.skip_links = 0;

    ProtocolHandler* h = (ProtocolHandler*)calloc(1, sizeof(ProtocolHandler));
    if (!h) { free(d); return NULL; }

    h->name = "sftp";
    h->data = d;
    h->CreateMainControls = SftpCreateMainControls;
    h->ShowMainControls = SftpShowMainControls;
    h->HideMainControls = SftpHideMainControls;
    h->CreateAdvControls = SftpCreateAdvControls;
    h->ShowAdvControls = SftpShowAdvControls;
    h->HideAdvControls = SftpHideAdvControls;
    h->DestroyControls = SftpDestroyControls;
    h->UpdateAdvPositions = SftpUpdateAdvPositions;
    h->GetAdvContentHeight = SftpGetAdvContentHeight;
    h->HandleCtlColor = SftpHandleCtlColor;
    h->HandleCommand = SftpHandleCommand;
    h->LoadConfig = SftpLoadConfig;
    h->SaveConfig = SftpSaveConfig;
    h->SaveAdvSettingsFromUI = SftpSaveAdvSettingsFromUI;
    h->ResetAdvSettings = SftpResetAdvSettings;
    h->ExecuteMount = SftpExecuteMount;
    h->Destroy = SftpDestroy;

    return h;
}