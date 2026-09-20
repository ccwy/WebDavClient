#include "protocol_smb.h"
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
   高级设置对话框控件 ID（SMB 专属 + 通用 VFS）
   参数来源: https://rclone.org/smb/
   ====================================================================== */
/* SMB 专属控件 ID */
#define SMB_IDC_ADV_EDIT_DOMAIN        301
#define SMB_IDC_ADV_EDIT_SPN           302
#define SMB_IDC_ADV_CHECK_KERBEROS     303
#define SMB_IDC_ADV_EDIT_IDLE_TIMEOUT  304
#define SMB_IDC_ADV_CHECK_HIDE_SHARE   305
#define SMB_IDC_ADV_CHECK_CASE_INSENS  306
#define SMB_IDC_ADV_BTN_BACK           308
#define SMB_IDC_ADV_BTN_SAVE           309
#define SMB_IDC_ADV_BTN_RESET          310
/* 通用 VFS 控件 ID */
#define SMB_IDC_ADV_COMBO_VFS          311
#define SMB_IDC_ADV_EDIT_DCT           312
#define SMB_IDC_ADV_EDIT_BS            313
#define SMB_IDC_ADV_EDIT_TR            314
#define SMB_IDC_ADV_EDIT_CD            315
#define SMB_IDC_ADV_EDIT_CMA           316
#define SMB_IDC_ADV_EDIT_RCS           317
#define SMB_IDC_ADV_EDIT_RCSL          318
#define SMB_IDC_ADV_EDIT_VOLNAME       319
#define SMB_IDC_ADV_EDIT_VCMS          320
#define SMB_IDC_ADV_BTN_BROWSE         321
#define SMB_IDC_ADV_BTN_CLEAR_CACHE    322

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
   配置加载 / 保存（委托给 config.c 统一读写 config.ini，smb_ 前缀键）
   ====================================================================== */
static void SmbLoadConfig(ProtocolHandler* self) {
    SmbData* d = (SmbData*)self->data;
    LoadSmbConfig(&d->cfg);
}

static void SmbSaveConfig(ProtocolHandler* self) {
    SmbData* d = (SmbData*)self->data;
    SaveSmbConfig(&d->cfg);
}

/* ======================================================================
   主页面 UI（5 个协议字段 + Drive 标签，与 WebDAV 布局对齐）
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

    /* Drive 标签（与 main.c 的 hDriveBox 对齐，编辑框由 main.c 创建） */
    d->hMainLabels[5] = SmbCreateBoldLabel(TR("STR_DRIVE"), 30, 295, 110, 28, hwnd, hBoldFont);
}

static void SmbShowMainControls(ProtocolHandler* self, HWND hwnd) {
    SmbData* d = (SmbData*)self->data;
    int i;
    for (i = 0; i < 6; i++) ShowWindow(d->hMainLabels[i], SW_SHOW);
    ShowWindow(d->hServerBox, SW_SHOW);
    ShowWindow(d->hPortBox, SW_SHOW);
    ShowWindow(d->hShareBox, SW_SHOW);
    ShowWindow(d->hUserBox, SW_SHOW);
    ShowWindow(d->hPassBox, SW_SHOW);
}

static void SmbHideMainControls(ProtocolHandler* self, HWND hwnd) {
    SmbData* d = (SmbData*)self->data;
    int i;
    for (i = 0; i < 6; i++) ShowWindow(d->hMainLabels[i], SW_HIDE);
    ShowWindow(d->hServerBox, SW_HIDE);
    ShowWindow(d->hPortBox, SW_HIDE);
    ShowWindow(d->hShareBox, SW_HIDE);
    ShowWindow(d->hUserBox, SW_HIDE);
    ShowWindow(d->hPassBox, SW_HIDE);
}

/* ======================================================================
   高级设置页面 UI（16 行: 6行SMB专属 + 10行通用VFS/Mount）
   Row 0-5: SMB 专属参数
   Row 6-15: 通用 VFS/Mount 参数（与 WebDAV 高级设置布局一致）
   参数来源: https://rclone.org/smb/
   ====================================================================== */
static void SmbCreateAdvControls(ProtocolHandler* self, HWND hwnd,
                                  HFONT hFont, HFONT hBoldFont, HFONT hDescFont) {
    SmbData* d = (SmbData*)self->data;
    CommonConfig* cc = d->commonCfg;
    int y;
    char transfersStr[16];
    const wchar_t* vfsDesc = NULL;

    d->hAdvDescFont = hDescFont;

    /* ====== SMB 专属参数（Row 0-5） ====== */

    /* Row 0: Domain (Edit) */
    y = 15;
    d->hAdvLabels[0] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_DOMAIN"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[0], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[0] = CreateWindowExW(0, L"STATIC", L"--smb-domain", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[0], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[0] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.domain, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_DOMAIN, NULL, NULL);
    SendMessageW(d->hAdvEdits[0], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[0] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_HINT_DOMAIN"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[0], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 1: SPN (Edit) */
    y = 105;
    d->hAdvLabels[1] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_SPN"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[1], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[1] = CreateWindowExW(0, L"STATIC", L"--smb-spn", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[1], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[1] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.spn, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_SPN, NULL, NULL);
    SendMessageW(d->hAdvEdits[1], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[1] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_HINT_SPN"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[1], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 2: Use Kerberos (Checkbox) */
    y = 195;
    d->hAdvLabels[2] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_USE_KERBEROS"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[2], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[2] = CreateWindowExW(0, L"STATIC", L"--smb-use-kerberos", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[2], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvChecks[0] = CreateWindowExW(0, L"BUTTON", TR("SMB_STR_ADV_ENABLE"), WS_CHILD | BS_AUTOCHECKBOX, 195, y + 3, 330, 25, hwnd, (HMENU)SMB_IDC_ADV_CHECK_KERBEROS, NULL, NULL);
    SendMessageW(d->hAdvChecks[0], WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(d->hAdvChecks[0], BM_SETCHECK, d->cfg.use_kerberos ? BST_CHECKED : BST_UNCHECKED, 0);
    d->hAdvDescLabels[2] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_HINT_USE_KERBEROS"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[2], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 3: Idle Timeout (Edit) */
    y = 285;
    d->hAdvLabels[3] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_IDLE_TIMEOUT"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[3], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[3] = CreateWindowExW(0, L"STATIC", L"--smb-idle-timeout", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[3], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[2] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.idle_timeout, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_IDLE_TIMEOUT, NULL, NULL);
    SendMessageW(d->hAdvEdits[2], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[3] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_HINT_IDLE_TIMEOUT"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[3], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 4: Hide Special Share (Checkbox) */
    y = 375;
    d->hAdvLabels[4] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_HIDE_SPECIAL_SHARE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[4], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[4] = CreateWindowExW(0, L"STATIC", L"--smb-hide-special-share", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[4], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvChecks[1] = CreateWindowExW(0, L"BUTTON", TR("SMB_STR_ADV_ENABLE"), WS_CHILD | BS_AUTOCHECKBOX, 195, y + 3, 330, 25, hwnd, (HMENU)SMB_IDC_ADV_CHECK_HIDE_SHARE, NULL, NULL);
    SendMessageW(d->hAdvChecks[1], WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(d->hAdvChecks[1], BM_SETCHECK, d->cfg.hide_special_share ? BST_CHECKED : BST_UNCHECKED, 0);
    d->hAdvDescLabels[4] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_HINT_HIDE_SPECIAL_SHARE"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[4], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 5: Case Insensitive (Checkbox) */
    y = 465;
    d->hAdvLabels[5] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_CASE_INSENSITIVE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[5], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[5] = CreateWindowExW(0, L"STATIC", L"--smb-case-insensitive", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[5], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvChecks[2] = CreateWindowExW(0, L"BUTTON", TR("SMB_STR_ADV_ENABLE"), WS_CHILD | BS_AUTOCHECKBOX, 195, y + 3, 330, 25, hwnd, (HMENU)SMB_IDC_ADV_CHECK_CASE_INSENS, NULL, NULL);
    SendMessageW(d->hAdvChecks[2], WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(d->hAdvChecks[2], BM_SETCHECK, d->cfg.case_insensitive ? BST_CHECKED : BST_UNCHECKED, 0);
    d->hAdvDescLabels[5] = CreateWindowExW(0, L"STATIC", TR("SMB_STR_ADV_HINT_CASE_INSENSITIVE"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[5], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* ====== 通用 VFS/Mount 参数（Row 6-15，使用 CommonConfig 数据，STR_ 前缀） ====== */

    /* Row 6: vfs-cache-mode ComboBox */
    y = 555;
    d->hAdvLabels[6] = CreateWindowExW(0, L"STATIC", TR("STR_VFS_CACHE_MODE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[6], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[6] = CreateWindowExW(0, L"STATIC", L"--vfs-cache-mode", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[6], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvComboVfs = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 195, y, 330, 200, hwnd, (HMENU)SMB_IDC_ADV_COMBO_VFS, NULL, NULL);
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
    d->hAdvDescLabels[6] = CreateWindowExW(0, L"STATIC", vfsDesc, WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[6], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 7: dir-cache-time */
    y = 645;
    d->hAdvLabels[7] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_DIR_CACHE_TIME"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[7], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[7] = CreateWindowExW(0, L"STATIC", L"--dir-cache-time", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[7], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[3] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->dir_cache_time, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_DCT, NULL, NULL);
    SendMessageW(d->hAdvEdits[3], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[7] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_DIR_CACHE_TIME"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[7], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 8: buffer-size */
    y = 735;
    d->hAdvLabels[8] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_BUFFER_SIZE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[8], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[8] = CreateWindowExW(0, L"STATIC", L"--buffer-size", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[8], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[4] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->buffer_size, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_BS, NULL, NULL);
    SendMessageW(d->hAdvEdits[4], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[8] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_BUFFER_SIZE"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[8], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 9: transfers */
    y = 825;
    d->hAdvLabels[9] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_TRANSFERS"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[9], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[9] = CreateWindowExW(0, L"STATIC", L"--transfers", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[9], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    sprintf_s(transfersStr, sizeof(transfersStr), "%d", cc->transfers);
    d->hAdvEdits[5] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", transfersStr, WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_TR, NULL, NULL);
    SendMessageW(d->hAdvEdits[5], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[9] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_TRANSFERS"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[9], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 10: cache-dir (narrower edit + browse button) */
    y = 915;
    d->hAdvLabels[10] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_CACHE_DIR"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[10], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[10] = CreateWindowExW(0, L"STATIC", L"--cache-dir", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[10], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[6] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->cache_dir, WS_CHILD | ES_AUTOHSCROLL, 195, y, 260, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_CD, NULL, NULL);
    SendMessageW(d->hAdvEdits[6], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvBtnBrowse = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | BS_PUSHBUTTON, 465, y, 60, 25, hwnd, (HMENU)SMB_IDC_ADV_BTN_BROWSE, NULL, NULL);
    SendMessageW(d->hAdvBtnBrowse, WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[10] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_CACHE_DIR"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[10], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 11: vfs-cache-max-age */
    y = 1005;
    d->hAdvLabels[11] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_CACHE_MAX_AGE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[11], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[11] = CreateWindowExW(0, L"STATIC", L"--vfs-cache-max-age", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[11], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[7] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->vfs_cache_max_age, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_CMA, NULL, NULL);
    SendMessageW(d->hAdvEdits[7], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[11] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VFS_CACHE_MAX_AGE"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[11], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 12: vfs-read-chunk-size */
    y = 1095;
    d->hAdvLabels[12] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_READ_CHUNK"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[12], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[12] = CreateWindowExW(0, L"STATIC", L"--vfs-read-chunk-size", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[12], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[8] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->vfs_read_chunk_size, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_RCS, NULL, NULL);
    SendMessageW(d->hAdvEdits[8], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[12] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VFS_READ_CHUNK"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[12], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 13: vfs-read-chunk-size-limit */
    y = 1185;
    d->hAdvLabels[13] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_READ_CHUNK_LIMIT"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[13], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[13] = CreateWindowExW(0, L"STATIC", L"--vfs-read-chunk-size-limit", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[13], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[9] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->vfs_read_chunk_size_limit, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_RCSL, NULL, NULL);
    SendMessageW(d->hAdvEdits[9], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[13] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VFS_READ_CHUNK_LIMIT"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[13], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 14: volname */
    y = 1275;
    d->hAdvLabels[14] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VOLNAME"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[14], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[14] = CreateWindowExW(0, L"STATIC", L"--volname", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[14], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[10] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->volname, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_VOLNAME, NULL, NULL);
    SendMessageW(d->hAdvEdits[10], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[14] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VOLNAME"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[14], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 15: vfs-cache-max-size */
    y = 1365;
    d->hAdvLabels[15] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_CACHE_MAX_SIZE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[15], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[15] = CreateWindowExW(0, L"STATIC", L"--vfs-cache-max-size", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[15], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[11] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->vfs_cache_max_size, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)SMB_IDC_ADV_EDIT_VCMS, NULL, NULL);
    SendMessageW(d->hAdvEdits[11], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[15] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VFS_CACHE_MAX_SIZE"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[15], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Bottom buttons */
    y = 1455;
    d->hAdvBtnBack = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_BACK"), WS_CHILD | BS_PUSHBUTTON, 20, y, 121, 32, hwnd, (HMENU)SMB_IDC_ADV_BTN_BACK, NULL, NULL);
    SendMessageW(d->hAdvBtnBack, WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvBtnClearCache = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_CLEAR_CACHE"), WS_CHILD | BS_PUSHBUTTON, 148, y, 121, 32, hwnd, (HMENU)SMB_IDC_ADV_BTN_CLEAR_CACHE, NULL, NULL);
    SendMessageW(d->hAdvBtnClearCache, WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvBtnSave = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_OK"), WS_CHILD | BS_PUSHBUTTON, 276, y, 121, 32, hwnd, (HMENU)SMB_IDC_ADV_BTN_SAVE, NULL, NULL);
    SendMessageW(d->hAdvBtnSave, WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvBtnReset = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_RESET"), WS_CHILD | BS_PUSHBUTTON, 404, y, 121, 32, hwnd, (HMENU)SMB_IDC_ADV_BTN_RESET, NULL, NULL);
    SendMessageW(d->hAdvBtnReset, WM_SETFONT, (WPARAM)hFont, TRUE);
}

static void SmbShowAdvControls(ProtocolHandler* self, HWND hwnd) {
    SmbData* d = (SmbData*)self->data;
    int i;
    for (i = 0; i < 16; i++) {
        ShowWindow(d->hAdvLabels[i], SW_SHOW);
        ShowWindow(d->hAdvParamLabels[i], SW_SHOW);
        ShowWindow(d->hAdvDescLabels[i], SW_SHOW);
    }
    for (i = 0; i < 12; i++) ShowWindow(d->hAdvEdits[i], SW_SHOW);
    for (i = 0; i < 3; i++) ShowWindow(d->hAdvChecks[i], SW_SHOW);
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
    for (i = 0; i < 16; i++) {
        ShowWindow(d->hAdvLabels[i], SW_HIDE);
        ShowWindow(d->hAdvParamLabels[i], SW_HIDE);
        ShowWindow(d->hAdvDescLabels[i], SW_HIDE);
    }
    for (i = 0; i < 12; i++) ShowWindow(d->hAdvEdits[i], SW_HIDE);
    for (i = 0; i < 3; i++) ShowWindow(d->hAdvChecks[i], SW_HIDE);
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
    for (i = 0; i < 6; i++) { if (d->hMainLabels[i]) { DestroyWindow(d->hMainLabels[i]); } d->hMainLabels[i] = NULL; }
    if (d->hServerBox) { DestroyWindow(d->hServerBox); d->hServerBox = NULL; }
    if (d->hPortBox)   { DestroyWindow(d->hPortBox);   d->hPortBox = NULL; }
    if (d->hShareBox)  { DestroyWindow(d->hShareBox);  d->hShareBox = NULL; }
    if (d->hUserBox)   { DestroyWindow(d->hUserBox);   d->hUserBox = NULL; }
    if (d->hPassBox)   { DestroyWindow(d->hPassBox);   d->hPassBox = NULL; }
    /* 销毁高级设置控件 */
    for (i = 0; i < 16; i++) {
        if (d->hAdvLabels[i])      { DestroyWindow(d->hAdvLabels[i]);      d->hAdvLabels[i] = NULL; }
        if (d->hAdvParamLabels[i]) { DestroyWindow(d->hAdvParamLabels[i]); d->hAdvParamLabels[i] = NULL; }
        if (d->hAdvDescLabels[i])  { DestroyWindow(d->hAdvDescLabels[i]);  d->hAdvDescLabels[i] = NULL; }
    }
    for (i = 0; i < 12; i++) { if (d->hAdvEdits[i]) { DestroyWindow(d->hAdvEdits[i]); d->hAdvEdits[i] = NULL; } }
    for (i = 0; i < 3; i++) { if (d->hAdvChecks[i]) { DestroyWindow(d->hAdvChecks[i]); d->hAdvChecks[i] = NULL; } }
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
    hdwp = BeginDeferWindowPos(72);  /* 16*4 + 3 checkboxes + combo + browse + 4 buttons = 72 */
    if (!hdwp) return;

    /* Row 0: Domain */
    y = 15 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[0], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[0], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[0], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[0], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 1: SPN */
    y = 105 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[1], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[1], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[1], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[1], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 2: Use Kerberos */
    y = 195 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[2], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[2], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvChecks[0], NULL, 195, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[2], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 3: Idle Timeout */
    y = 285 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[3], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[3], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[2], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[3], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 4: Hide Special Share */
    y = 375 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[4], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[4], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvChecks[1], NULL, 195, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[4], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 5: Case Insensitive */
    y = 465 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[5], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[5], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvChecks[2], NULL, 195, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[5], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 6: vfs-cache-mode ComboBox */
    y = 555 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[6], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[6], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvComboVfs, NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[6], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 7: dir-cache-time */
    y = 645 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[7], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[7], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[3], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[7], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 8: buffer-size */
    y = 735 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[8], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[8], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[4], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[8], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 9: transfers */
    y = 825 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[9], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[9], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[5], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[9], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 10: cache-dir */
    y = 915 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[10], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[10], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[6], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnBrowse, NULL, 465, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[10], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 11: vfs-cache-max-age */
    y = 1005 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[11], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[11], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[7], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[11], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 12: vfs-read-chunk-size */
    y = 1095 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[12], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[12], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[8], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[12], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 13: vfs-read-chunk-size-limit */
    y = 1185 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[13], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[13], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[9], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[13], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 14: volname */
    y = 1275 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[14], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[14], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[10], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[14], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 15: vfs-cache-max-size */
    y = 1365 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[15], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[15], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[11], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[15], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Bottom buttons */
    y = 1455 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnBack, NULL, 20, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnClearCache, NULL, 148, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnSave, NULL, 276, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnReset, NULL, 404, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    if (hdwp) EndDeferWindowPos(hdwp);
}

static int SmbGetAdvContentHeight(ProtocolHandler* self) {
    (void)self;
    return 1502;  /* 1455 + 32 + 15 */
}

static LRESULT SmbHandleCtlColor(ProtocolHandler* self, HWND hCtrl, HDC hdc) {
    SmbData* d = (SmbData*)self->data;
    int idx;
    for (idx = 0; idx < 16; idx++) {
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
    CommonConfig* cc = d->commonCfg;
    (void)lParam;

    if (LOWORD(wParam) == SMB_IDC_ADV_BTN_SAVE) {
        char transfersBuf[16];
        int vfsSel;
        /* 从 UI 读取 SMB 专属设置 */
        GetWindowTextA(d->hAdvEdits[0], d->cfg.domain, sizeof(d->cfg.domain));
        GetWindowTextA(d->hAdvEdits[1], d->cfg.spn, sizeof(d->cfg.spn));
        d->cfg.use_kerberos = (SendMessageW(d->hAdvChecks[0], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
        GetWindowTextA(d->hAdvEdits[2], d->cfg.idle_timeout, sizeof(d->cfg.idle_timeout));
        d->cfg.hide_special_share = (SendMessageW(d->hAdvChecks[1], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
        d->cfg.case_insensitive = (SendMessageW(d->hAdvChecks[2], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
        /* 从 UI 读取通用 VFS 设置到 CommonConfig */
        GetWindowTextA(d->hAdvEdits[3], cc->dir_cache_time, sizeof(cc->dir_cache_time));
        GetWindowTextA(d->hAdvEdits[4], cc->buffer_size, sizeof(cc->buffer_size));
        memset(transfersBuf, 0, sizeof(transfersBuf));
        GetWindowTextA(d->hAdvEdits[5], transfersBuf, sizeof(transfersBuf));
        cc->transfers = atoi(transfersBuf);
        if (cc->transfers <= 0) cc->transfers = 4;
        GetWindowTextA(d->hAdvEdits[6], cc->cache_dir, sizeof(cc->cache_dir));
        GetWindowTextA(d->hAdvEdits[7], cc->vfs_cache_max_age, sizeof(cc->vfs_cache_max_age));
        GetWindowTextA(d->hAdvEdits[8], cc->vfs_read_chunk_size, sizeof(cc->vfs_read_chunk_size));
        GetWindowTextA(d->hAdvEdits[9], cc->vfs_read_chunk_size_limit, sizeof(cc->vfs_read_chunk_size_limit));
        GetWindowTextA(d->hAdvEdits[10], cc->volname, sizeof(cc->volname));
        GetWindowTextA(d->hAdvEdits[11], cc->vfs_cache_max_size, sizeof(cc->vfs_cache_max_size));
        vfsSel = (int)SendMessageW(d->hAdvComboVfs, CB_GETCURSEL, 0, 0);
        cc->vfs_cache_mode = (vfsSel != CB_ERR) ? vfsSel : 2;
        self->SaveConfig(self);
        SaveCommonConfig(cc);
        return 2;  /* 已处理：保存后请求 main.c 切换回主页面 */
    }
    else if (LOWORD(wParam) == SMB_IDC_ADV_BTN_RESET) {
        /* 重置 SMB 专属设置为默认值 */
        SetWindowTextA(d->hAdvEdits[0], "WORKGROUP");
        SetWindowTextA(d->hAdvEdits[1], "");
        SendMessageW(d->hAdvChecks[0], BM_SETCHECK, BST_UNCHECKED, 0);
        SetWindowTextA(d->hAdvEdits[2], "1m0s");
        SendMessageW(d->hAdvChecks[1], BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(d->hAdvChecks[2], BM_SETCHECK, BST_CHECKED, 0);
        /* 重置通用 VFS 设置为默认值 */
        SetWindowTextA(d->hAdvEdits[3], "24h");
        SetWindowTextA(d->hAdvEdits[4], "64M");
        SetWindowTextA(d->hAdvEdits[5], "4");
        SetWindowTextA(d->hAdvEdits[6], "");
        SetWindowTextA(d->hAdvEdits[7], "24h");
        SetWindowTextA(d->hAdvEdits[8], "128M");
        SetWindowTextA(d->hAdvEdits[9], "off");
        SetWindowTextA(d->hAdvEdits[10], "Network_Disk");
        SetWindowTextA(d->hAdvEdits[11], "15G");
        SendMessageW(d->hAdvComboVfs, CB_SETCURSEL, 3, 0);  /* full */
        SetWindowTextW(d->hAdvDescLabels[6], TR("STR_VFS_TIP_FULL"));
        return 1;
    }
    else if (LOWORD(wParam) == SMB_IDC_ADV_BTN_CLEAR_CACHE) {
        char cacheDir[MAX_PATH];
        wchar_t wCacheDir[MAX_PATH];
        int isDefaultDir = 0;
        GetWindowTextA(d->hAdvEdits[6], cacheDir, sizeof(cacheDir));
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
    else if (LOWORD(wParam) == SMB_IDC_ADV_COMBO_VFS && HIWORD(wParam) == CBN_SELCHANGE) {
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
            SetWindowTextW(d->hAdvDescLabels[6], vfsDesc);
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
        bi.lpszTitle = TR("STR_ADV_CACHE_DIR");
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        pidl = SHBrowseForFolderW(&bi);
        if (pidl) {
            if (SHGetPathFromIDListW(pidl, selectedPath)) {
                WideCharToMultiByte(CP_ACP, 0, selectedPath, -1, ansiPath, MAX_PATH, NULL, NULL);
                SetWindowTextA(d->hAdvEdits[6], ansiPath);
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
    CommonConfig* cc = d->commonCfg;
    char transfersBuf[16];
    int vfsSel;
    /* SMB 专属设置 → SmbConfig */
    GetWindowTextA(d->hAdvEdits[0], d->cfg.domain, sizeof(d->cfg.domain));
    GetWindowTextA(d->hAdvEdits[1], d->cfg.spn, sizeof(d->cfg.spn));
    d->cfg.use_kerberos = (SendMessageW(d->hAdvChecks[0], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    GetWindowTextA(d->hAdvEdits[2], d->cfg.idle_timeout, sizeof(d->cfg.idle_timeout));
    d->cfg.hide_special_share = (SendMessageW(d->hAdvChecks[1], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    d->cfg.case_insensitive = (SendMessageW(d->hAdvChecks[2], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    /* 通用 VFS 设置 → CommonConfig */
    GetWindowTextA(d->hAdvEdits[3], cc->dir_cache_time, sizeof(cc->dir_cache_time));
    GetWindowTextA(d->hAdvEdits[4], cc->buffer_size, sizeof(cc->buffer_size));
    memset(transfersBuf, 0, sizeof(transfersBuf));
    GetWindowTextA(d->hAdvEdits[5], transfersBuf, sizeof(transfersBuf));
    cc->transfers = atoi(transfersBuf);
    if (cc->transfers <= 0) cc->transfers = 4;
    GetWindowTextA(d->hAdvEdits[6], cc->cache_dir, sizeof(cc->cache_dir));
    GetWindowTextA(d->hAdvEdits[7], cc->vfs_cache_max_age, sizeof(cc->vfs_cache_max_age));
    GetWindowTextA(d->hAdvEdits[8], cc->vfs_read_chunk_size, sizeof(cc->vfs_read_chunk_size));
    GetWindowTextA(d->hAdvEdits[9], cc->vfs_read_chunk_size_limit, sizeof(cc->vfs_read_chunk_size_limit));
    GetWindowTextA(d->hAdvEdits[10], cc->volname, sizeof(cc->volname));
    GetWindowTextA(d->hAdvEdits[11], cc->vfs_cache_max_size, sizeof(cc->vfs_cache_max_size));
    vfsSel = (int)SendMessageW(d->hAdvComboVfs, CB_GETCURSEL, 0, 0);
    cc->vfs_cache_mode = (vfsSel != CB_ERR) ? vfsSel : 2;
}

static void SmbResetAdvSettings(ProtocolHandler* self) {
    SmbData* d = (SmbData*)self->data;
    /* 重置 SMB 专属设置为默认值 */
    SetWindowTextA(d->hAdvEdits[0], "WORKGROUP");
    SetWindowTextA(d->hAdvEdits[1], "");
    SendMessageW(d->hAdvChecks[0], BM_SETCHECK, BST_UNCHECKED, 0);
    SetWindowTextA(d->hAdvEdits[2], "1m0s");
    SendMessageW(d->hAdvChecks[1], BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(d->hAdvChecks[2], BM_SETCHECK, BST_CHECKED, 0);
    /* 重置通用 VFS 设置为默认值 */
    SetWindowTextA(d->hAdvEdits[3], "24h");
    SetWindowTextA(d->hAdvEdits[4], "64M");
    SetWindowTextA(d->hAdvEdits[5], "4");
    SetWindowTextA(d->hAdvEdits[6], "");
    SetWindowTextA(d->hAdvEdits[7], "24h");
    SetWindowTextA(d->hAdvEdits[8], "128M");
    SetWindowTextA(d->hAdvEdits[9], "off");
    SetWindowTextA(d->hAdvEdits[10], "Network_Disk");
    SetWindowTextA(d->hAdvEdits[11], "15G");
    SendMessageW(d->hAdvComboVfs, CB_SETCURSEL, 3, 0);  /* full */
    SetWindowTextW(d->hAdvDescLabels[6], TR("STR_VFS_TIP_FULL"));
}

/* ======================================================================
   挂载执行
   rclone SMB 命令格式: rclone mount :smb:sharename X: --smb-host ... --smb-user ...
   注意: 共享名是路径的一部分，不是 --smb-share 参数（rclone 无此选项）
   ====================================================================== */
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
    SaveCommonConfig(cc);

    LogMessage("INFO", "SMB mount action triggered with server=%s share=%s", d->cfg.server, d->cfg.share);

    /* 密码混淆 */
    char obscuredPass[256] = { 0 };
    RcloneObscurePassword(rclonePath, d->cfg.pass, obscuredPass, sizeof(obscuredPass));

    /* 构建 SMB 专属参数 */
    char smbParams[1024] = { 0 };
    char tmpBuf[256];

    /* --smb-port（非默认 445 时传递） */
    if (d->cfg.port[0] != '\0' && strcmp(d->cfg.port, "445") != 0) {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--smb-port \"%s\" ", d->cfg.port);
        strcat_s(smbParams, sizeof(smbParams), tmpBuf);
    }

    /* --smb-domain（非默认 WORKGROUP 时传递） */
    if (d->cfg.domain[0] != '\0' && strcmp(d->cfg.domain, "WORKGROUP") != 0) {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--smb-domain \"%s\" ", d->cfg.domain);
        strcat_s(smbParams, sizeof(smbParams), tmpBuf);
    }

    /* --smb-spn（非空时传递） */
    if (d->cfg.spn[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--smb-spn \"%s\" ", d->cfg.spn);
        strcat_s(smbParams, sizeof(smbParams), tmpBuf);
    }

    /* --smb-use-kerberos（启用时传递） */
    if (d->cfg.use_kerberos) {
        strcat_s(smbParams, sizeof(smbParams), "--smb-use-kerberos ");
    }

    /* --smb-idle-timeout（非默认 1m0s 时传递） */
    if (d->cfg.idle_timeout[0] != '\0' && strcmp(d->cfg.idle_timeout, "1m0s") != 0) {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--smb-idle-timeout \"%s\" ", d->cfg.idle_timeout);
        strcat_s(smbParams, sizeof(smbParams), tmpBuf);
    }

    /* --smb-hide-special-share=false（用户取消勾选时传递） */
    if (!d->cfg.hide_special_share) {
        strcat_s(smbParams, sizeof(smbParams), "--smb-hide-special-share=false ");
    }

    /* --smb-case-insensitive=false（用户取消勾选时传递） */
    if (!d->cfg.case_insensitive) {
        strcat_s(smbParams, sizeof(smbParams), "--smb-case-insensitive=false ");
    }

    /* 构建通用 VFS 参数（来自 CommonConfig） */
    char vfsParams[1024] = { 0 };
    const char* cacheMode = "off";
    switch (cc->vfs_cache_mode) {
        case 0: cacheMode = "off"; break;
        case 1: cacheMode = "minimal"; break;
        case 2: cacheMode = "writes"; break;
        case 3: cacheMode = "full"; break;
        default: cacheMode = "writes"; break;
    }
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
    if (cc->volname[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--volname \"%s\" ", cc->volname);
        strcat_s(vfsParams, sizeof(vfsParams), tmpBuf);
    }
    if (cc->vfs_cache_max_size[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-cache-max-size %s ", cc->vfs_cache_max_size);
        strcat_s(vfsParams, sizeof(vfsParams), tmpBuf);
    }

    /* 构建完整 rclone 命令行
     * 格式: rclone mount :smb:sharename X: --smb-host ... --smb-user ... --smb-pass ...
     * 共享名是 rclone 路径的一部分，不是 --smb-share 参数（rclone 无此选项） */
    char cmd[4096];
    char workDir[MAX_PATH];
    GetModuleFileNameA(NULL, workDir, MAX_PATH);
    char* slash = strrchr(workDir, '\\');
    if (slash) *slash = '\0';

    if (cc->debug_log) {
        char logPath[MAX_PATH];
        sprintf_s(logPath, sizeof(logPath), "%s\\rclone_error.log", workDir);
        sprintf_s(cmd, sizeof(cmd),
            "\"%s\" mount \":smb:%s\" %s: --smb-host \"%s\" --smb-user \"%s\" --smb-pass \"%s\" "
            "--vfs-cache-mode %s "
            "%s"
            "%s"
            "--log-file \"%s\" -vv",
            rclonePath, d->cfg.share, cc->drive, d->cfg.server, d->cfg.user, obscuredPass,
            cacheMode, smbParams, vfsParams, logPath
        );
        LogMessage("INFO", "Starting Rclone SMB mount with vfs-cache-mode=%s and debug logging enabled.", cacheMode);
    } else {
        sprintf_s(cmd, sizeof(cmd),
            "\"%s\" mount \":smb:%s\" %s: --smb-host \"%s\" --smb-user \"%s\" --smb-pass \"%s\" "
            "--vfs-cache-mode %s "
            "%s"
            "%s",
            rclonePath, d->cfg.share, cc->drive, d->cfg.server, d->cfg.user, obscuredPass,
            cacheMode, smbParams, vfsParams
        );
        LogMessage("INFO", "Starting Rclone SMB mount with vfs-cache-mode=%s and debug logging disabled.", cacheMode);
    }

    /* 调用 rclone_manager 执行挂载 */
    if (StartRcloneProcess(cmd, cc->drive)) {
        LogMessage("INFO", "SMB mount started successfully on drive %s:", cc->drive);
        return 1;
    }

    LogMessage("ERROR", "SMB mount failed to start. Check rclone_error.log for details.");
    if (!isAuto) MessageBoxW(hwnd, TR("MSG_MOUNT_FAIL"), TR("MSG_ERROR"), MB_OK | MB_ICONERROR);
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