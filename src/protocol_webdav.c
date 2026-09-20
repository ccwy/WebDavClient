#include "protocol_webdav.h"
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
   高级设置对话框控件 ID（ WebDAV 专属）
   ====================================================================== */
#define WD_IDC_ADV_EDIT_DCT       201
#define WD_IDC_ADV_EDIT_BS        202
#define WD_IDC_ADV_EDIT_TR        203
#define WD_IDC_ADV_EDIT_CD        204
#define WD_IDC_ADV_EDIT_CMA       205
#define WD_IDC_ADV_EDIT_RCS       206
#define WD_IDC_ADV_EDIT_RCSL      207
#define WD_IDC_ADV_BTN_BROWSE     208
#define WD_IDC_ADV_BTN_OK         209
#define WD_IDC_ADV_BTN_CANCEL     210
#define WD_IDC_ADV_BTN_RESET      211
#define WD_IDC_ADV_EDIT_VOLNAME   212
#define WD_IDC_ADV_COMBO_VFS      213
#define WD_IDC_ADV_EDIT_VCMS      214
#define WD_IDC_ADV_BTN_BACK       215
#define WD_IDC_ADV_BTN_SAVE       216
#define WD_IDC_ADV_BTN_CLEAR_CACHE 217
#define WD_IDC_ADV_COMBO_VENDOR   218   /* --webdav-vendor 下拉框 */
#define WD_IDC_ADV_EDIT_HEADERS   219   /* --webdav-headers 编辑框 */
#define WD_IDC_ADV_CHECK_NO_CERT  220   /* --no-check-certificate 复选框 */

/* ======================================================================
   内部辅助：创建带字体的控件
   ====================================================================== */
static HWND WdCreateStyledExW(DWORD dwExStyle, LPCWSTR cls, LPCWSTR text,
                               DWORD style, int x, int y, int w, int h,
                               HWND parent, HMENU id, HFONT font) {
    HWND hw = CreateWindowExW(dwExStyle, cls, text, style, x, y, w, h, parent, id, NULL, NULL);
    if (hw && font) SendMessageW(hw, WM_SETFONT, (WPARAM)font, TRUE);
    return hw;
}

static HWND WdCreateStyledExA(DWORD dwExStyle, LPCSTR cls, LPCSTR text,
                               DWORD style, int x, int y, int w, int h,
                               HWND parent, HMENU id, HFONT font) {
    HWND hw = CreateWindowExA(dwExStyle, cls, text, style, x, y, w, h, parent, id, NULL, NULL);
    if (hw && font) SendMessageW(hw, WM_SETFONT, (WPARAM)font, TRUE);
    return hw;
}

static HWND WdCreateBoldLabel(LPCWSTR text, int x, int y, int w, int h,
                               HWND parent, HFONT boldFont) {
    HWND hw = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                               x, y, w, h, parent, NULL, NULL, NULL);
    if (hw && boldFont) SendMessageW(hw, WM_SETFONT, (WPARAM)boldFont, TRUE);
    return hw;
}

/* ======================================================================
   配置加载 / 保存（委托给 config.c 统一读写 config.ini，wd_ 前缀键）
   ====================================================================== */
static void WdLoadConfig(ProtocolHandler* self) {
    WebDavData* d = (WebDavData*)self->data;
    LoadWebDavConfig(&d->cfg);
}

static void WdSaveConfig(ProtocolHandler* self) {
    WebDavData* d = (WebDavData*)self->data;
    SaveWebDavConfig(&d->cfg);
}

/* ======================================================================
   主页面 UI
   ====================================================================== */
static void WdCreateMainControls(ProtocolHandler* self, HWND hwnd,
                                  HFONT hFont, HFONT hBoldFont) {
    WebDavData* d = (WebDavData*)self->data;

    d->hMainLabels[0] = WdCreateBoldLabel(TR("WD_STR_HOST"), 30, 70, 110, 28, hwnd, hBoldFont);
    d->hHostBox = WdCreateStyledExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.host,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 70, 390, 28, hwnd, NULL, hFont);

    d->hMainLabels[1] = WdCreateBoldLabel(TR("WD_STR_PORT"), 30, 115, 110, 28, hwnd, hBoldFont);
    d->hPortBox = WdCreateStyledExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.port,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, 145, 115, 130, 28, hwnd, NULL, hFont);
    d->hSslCheck = WdCreateStyledExW(0, L"BUTTON", TR("WD_STR_SSL"),
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 295, 117, 240, 25, hwnd, NULL, hFont);
    if (d->cfg.ssl) SendMessageA(d->hSslCheck, BM_SETCHECK, BST_CHECKED, 0);

    d->hMainLabels[2] = WdCreateBoldLabel(TR("WD_STR_PATH"), 30, 160, 110, 28, hwnd, hBoldFont);
    d->hPathBox = WdCreateStyledExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.path,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 160, 390, 28, hwnd, NULL, hFont);

    d->hMainLabels[3] = WdCreateBoldLabel(TR("WD_STR_USER"), 30, 205, 110, 28, hwnd, hBoldFont);
    d->hUserBox = WdCreateStyledExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.user,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 145, 205, 390, 28, hwnd, NULL, hFont);

    d->hMainLabels[4] = WdCreateBoldLabel(TR("WD_STR_PASS"), 30, 250, 110, 28, hwnd, hBoldFont);
    d->hPassBox = WdCreateStyledExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.pass,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD, 145, 250, 390, 28, hwnd, NULL, hFont);

    d->hMainLabels[5] = WdCreateBoldLabel(TR("STR_DRIVE"), 30, 295, 110, 28, hwnd, hBoldFont);
    /* 注意： drive 编辑框由 main.c 创建，此处不创建 */
}

static void WdShowMainControls(ProtocolHandler* self, HWND hwnd) {
    WebDavData* d = (WebDavData*)self->data;
    int i;
    for (i = 0; i < 6; i++) ShowWindow(d->hMainLabels[i], SW_SHOW);
    ShowWindow(d->hHostBox, SW_SHOW);
    ShowWindow(d->hPortBox, SW_SHOW);
    ShowWindow(d->hPathBox, SW_SHOW);
    ShowWindow(d->hUserBox, SW_SHOW);
    ShowWindow(d->hPassBox, SW_SHOW);
    ShowWindow(d->hSslCheck, SW_SHOW);
}

static void WdHideMainControls(ProtocolHandler* self, HWND hwnd) {
    WebDavData* d = (WebDavData*)self->data;
    int i;
    for (i = 0; i < 6; i++) ShowWindow(d->hMainLabels[i], SW_HIDE);
    ShowWindow(d->hHostBox, SW_HIDE);
    ShowWindow(d->hPortBox, SW_HIDE);
    ShowWindow(d->hPathBox, SW_HIDE);
    ShowWindow(d->hUserBox, SW_HIDE);
    ShowWindow(d->hPassBox, SW_HIDE);
    ShowWindow(d->hSslCheck, SW_HIDE);
}

/* ======================================================================
   高级设置页面 UI
   ====================================================================== */
static void WdCreateAdvControls(ProtocolHandler* self, HWND hwnd,
                                 HFONT hFont, HFONT hBoldFont, HFONT hDescFont) {
    WebDavData* d = (WebDavData*)self->data;
    CommonConfig* cc = d->commonCfg;
    int y;
    char transfersStr[16];
    const wchar_t* vfsDesc = NULL;

    d->hAdvDescFont = hDescFont;

    /* ====== 通用 VFS/Mount 参数（Row 0-9，使用 CommonConfig 数据，STR_ 前缀） ====== */

    /* Row 0: vfs-cache-mode ComboBox */
    y = 15;
    d->hAdvLabels[0] = CreateWindowExW(0, L"STATIC", TR("STR_VFS_CACHE_MODE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[0], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[0] = CreateWindowExW(0, L"STATIC", L"--vfs-cache-mode", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[0], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvComboVfs = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 195, y, 330, 200, hwnd, (HMENU)WD_IDC_ADV_COMBO_VFS, NULL, NULL);
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
    d->hAdvDescLabels[0] = CreateWindowExW(0, L"STATIC", vfsDesc, WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[0], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 1: dir-cache-time */
    y = 105;
    d->hAdvLabels[1] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_DIR_CACHE_TIME"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[1], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[1] = CreateWindowExW(0, L"STATIC", L"--dir-cache-time", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[1], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[0] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->dir_cache_time, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)WD_IDC_ADV_EDIT_DCT, NULL, NULL);
    SendMessageW(d->hAdvEdits[0], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[1] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_DIR_CACHE_TIME"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[1], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 2: buffer-size */
    y = 195;
    d->hAdvLabels[2] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_BUFFER_SIZE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[2], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[2] = CreateWindowExW(0, L"STATIC", L"--buffer-size", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[2], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[1] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->buffer_size, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)WD_IDC_ADV_EDIT_BS, NULL, NULL);
    SendMessageW(d->hAdvEdits[1], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[2] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_BUFFER_SIZE"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[2], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 3: transfers */
    y = 285;
    d->hAdvLabels[3] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_TRANSFERS"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[3], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[3] = CreateWindowExW(0, L"STATIC", L"--transfers", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[3], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    sprintf_s(transfersStr, sizeof(transfersStr), "%d", cc->transfers);
    d->hAdvEdits[2] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", transfersStr, WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 195, y, 330, 28, hwnd, (HMENU)WD_IDC_ADV_EDIT_TR, NULL, NULL);
    SendMessageW(d->hAdvEdits[2], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[3] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_TRANSFERS"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[3], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 4: cache-dir (narrower edit + browse button) */
    y = 375;
    d->hAdvLabels[4] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_CACHE_DIR"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[4], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[4] = CreateWindowExW(0, L"STATIC", L"--cache-dir", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[4], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[3] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->cache_dir, WS_CHILD | ES_AUTOHSCROLL, 195, y, 260, 28, hwnd, (HMENU)WD_IDC_ADV_EDIT_CD, NULL, NULL);
    SendMessageW(d->hAdvEdits[3], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvBtnBrowse = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | BS_PUSHBUTTON, 465, y, 60, 25, hwnd, (HMENU)WD_IDC_ADV_BTN_BROWSE, NULL, NULL);
    SendMessageW(d->hAdvBtnBrowse, WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[4] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_CACHE_DIR"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[4], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 5: vfs-cache-max-age */
    y = 465;
    d->hAdvLabels[5] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_CACHE_MAX_AGE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[5], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[5] = CreateWindowExW(0, L"STATIC", L"--vfs-cache-max-age", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[5], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[4] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->vfs_cache_max_age, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)WD_IDC_ADV_EDIT_CMA, NULL, NULL);
    SendMessageW(d->hAdvEdits[4], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[5] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VFS_CACHE_MAX_AGE"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[5], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 6: vfs-read-chunk-size */
    y = 555;
    d->hAdvLabels[6] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_READ_CHUNK"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[6], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[6] = CreateWindowExW(0, L"STATIC", L"--vfs-read-chunk-size", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[6], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[5] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->vfs_read_chunk_size, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)WD_IDC_ADV_EDIT_RCS, NULL, NULL);
    SendMessageW(d->hAdvEdits[5], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[6] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VFS_READ_CHUNK"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[6], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 7: vfs-read-chunk-size-limit */
    y = 645;
    d->hAdvLabels[7] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_READ_CHUNK_LIMIT"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[7], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[7] = CreateWindowExW(0, L"STATIC", L"--vfs-read-chunk-size-limit", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[7], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[6] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->vfs_read_chunk_size_limit, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)WD_IDC_ADV_EDIT_RCSL, NULL, NULL);
    SendMessageW(d->hAdvEdits[6], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[7] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VFS_READ_CHUNK_LIMIT"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[7], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 8: volname */
    y = 735;
    d->hAdvLabels[8] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VOLNAME"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[8], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[8] = CreateWindowExW(0, L"STATIC", L"--volname", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[8], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[7] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->volname, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)WD_IDC_ADV_EDIT_VOLNAME, NULL, NULL);
    SendMessageW(d->hAdvEdits[7], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[8] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VOLNAME"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[8], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 9: vfs-cache-max-size */
    y = 825;
    d->hAdvLabels[9] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_VFS_CACHE_MAX_SIZE"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[9], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[9] = CreateWindowExW(0, L"STATIC", L"--vfs-cache-max-size", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[9], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[8] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cc->vfs_cache_max_size, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)WD_IDC_ADV_EDIT_VCMS, NULL, NULL);
    SendMessageW(d->hAdvEdits[8], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[9] = CreateWindowExW(0, L"STATIC", TR("STR_ADV_HINT_VFS_CACHE_MAX_SIZE"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[9], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* ====== WebDAV 专属参数（Row 10-12，使用 WebDavConfig 数据，WD_ 前缀） ====== */

    /* Row 10: vendor (--webdav-vendor 下拉框) */
    y = 915;
    d->hAdvLabels[10] = CreateWindowExW(0, L"STATIC", TR("WD_STR_ADV_VENDOR"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[10], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[10] = CreateWindowExW(0, L"STATIC", L"--webdav-vendor", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[10], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvComboVendor = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 195, y, 330, 200, hwnd, (HMENU)WD_IDC_ADV_COMBO_VENDOR, NULL, NULL);
    SendMessageW(d->hAdvComboVendor, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(d->hAdvComboVendor, CB_ADDSTRING, 0, (LPARAM)TR("WD_STR_VENDOR_OTHER"));
    SendMessageW(d->hAdvComboVendor, CB_ADDSTRING, 0, (LPARAM)TR("WD_STR_VENDOR_NEXTCLOUD"));
    SendMessageW(d->hAdvComboVendor, CB_ADDSTRING, 0, (LPARAM)TR("WD_STR_VENDOR_OWNCLOUD"));
    SendMessageW(d->hAdvComboVendor, CB_ADDSTRING, 0, (LPARAM)TR("WD_STR_VENDOR_INFINITESCALE"));
    SendMessageW(d->hAdvComboVendor, CB_ADDSTRING, 0, (LPARAM)TR("WD_STR_VENDOR_SHAREPOINT"));
    SendMessageW(d->hAdvComboVendor, CB_ADDSTRING, 0, (LPARAM)TR("WD_STR_VENDOR_SHAREPOINT_NTLM"));
    SendMessageW(d->hAdvComboVendor, CB_ADDSTRING, 0, (LPARAM)TR("WD_STR_VENDOR_RCLONE"));
    SendMessageW(d->hAdvComboVendor, CB_ADDSTRING, 0, (LPARAM)TR("WD_STR_VENDOR_FASTMAIL"));
    /* 设置当前 vendor 选择 */
    {
        const char* v = d->cfg.vendor;
        int vIdx = 0; /* 默认 other */
        if (strcmp(v, "nextcloud") == 0) vIdx = 1;
        else if (strcmp(v, "owncloud") == 0) vIdx = 2;
        else if (strcmp(v, "infinitescale") == 0) vIdx = 3;
        else if (strcmp(v, "sharepoint") == 0) vIdx = 4;
        else if (strcmp(v, "sharepoint-ntlm") == 0) vIdx = 5;
        else if (strcmp(v, "rclone") == 0) vIdx = 6;
        else if (strcmp(v, "fastmail") == 0) vIdx = 7;
        SendMessageW(d->hAdvComboVendor, CB_SETCURSEL, (WPARAM)vIdx, 0);
    }
    d->hAdvDescLabels[10] = CreateWindowExW(0, L"STATIC", TR("WD_STR_ADV_HINT_VENDOR"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[10], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 11: headers (--webdav-headers 编辑框) */
    y = 1005;
    d->hAdvLabels[11] = CreateWindowExW(0, L"STATIC", TR("WD_STR_ADV_HEADERS"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[11], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[11] = CreateWindowExW(0, L"STATIC", L"--webdav-headers", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[11], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvEdits[9] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->cfg.headers, WS_CHILD | ES_AUTOHSCROLL, 195, y, 330, 28, hwnd, (HMENU)WD_IDC_ADV_EDIT_HEADERS, NULL, NULL);
    SendMessageW(d->hAdvEdits[9], WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvDescLabels[11] = CreateWindowExW(0, L"STATIC", TR("WD_STR_ADV_HINT_HEADERS"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[11], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Row 12: no-check-certificate (--no-check-certificate 复选框) */
    y = 1095;
    d->hAdvLabels[12] = CreateWindowExW(0, L"STATIC", TR("WD_STR_ADV_NO_CHECK_CERT"), WS_CHILD, 20, y + 3, 165, 25, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvLabels[12], WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    d->hAdvParamLabels[12] = CreateWindowExW(0, L"STATIC", L"--no-check-certificate", WS_CHILD, 20, y + 28, 165, 15, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvParamLabels[12], WM_SETFONT, (WPARAM)hDescFont, TRUE);
    d->hAdvCheckNoCert = CreateWindowExW(0, L"BUTTON", TR("WD_STR_ADV_ENABLE"), WS_CHILD | BS_AUTOCHECKBOX, 195, y + 3, 330, 25, hwnd, (HMENU)WD_IDC_ADV_CHECK_NO_CERT, NULL, NULL);
    SendMessageW(d->hAdvCheckNoCert, WM_SETFONT, (WPARAM)hFont, TRUE);
    if (d->cfg.no_check_cert) SendMessageA(d->hAdvCheckNoCert, BM_SETCHECK, BST_CHECKED, 0);
    d->hAdvDescLabels[12] = CreateWindowExW(0, L"STATIC", TR("WD_STR_ADV_HINT_NO_CHECK_CERT"), WS_CHILD, 195, y + 36, 330, 40, hwnd, NULL, NULL, NULL);
    SendMessageW(d->hAdvDescLabels[12], WM_SETFONT, (WPARAM)hDescFont, TRUE);

    /* Bottom buttons */
    y = 1185;
    d->hAdvBtnBack = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_BACK"), WS_CHILD | BS_PUSHBUTTON, 20, y, 121, 32, hwnd, (HMENU)WD_IDC_ADV_BTN_BACK, NULL, NULL);
    SendMessageW(d->hAdvBtnBack, WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvBtnClearCache = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_CLEAR_CACHE"), WS_CHILD | BS_PUSHBUTTON, 148, y, 121, 32, hwnd, (HMENU)WD_IDC_ADV_BTN_CLEAR_CACHE, NULL, NULL);
    SendMessageW(d->hAdvBtnClearCache, WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvBtnSave = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_OK"), WS_CHILD | BS_PUSHBUTTON, 276, y, 121, 32, hwnd, (HMENU)WD_IDC_ADV_BTN_SAVE, NULL, NULL);
    SendMessageW(d->hAdvBtnSave, WM_SETFONT, (WPARAM)hFont, TRUE);
    d->hAdvBtnReset = CreateWindowExW(0, L"BUTTON", TR("STR_ADV_RESET"), WS_CHILD | BS_PUSHBUTTON, 404, y, 121, 32, hwnd, (HMENU)WD_IDC_ADV_BTN_RESET, NULL, NULL);
    SendMessageW(d->hAdvBtnReset, WM_SETFONT, (WPARAM)hFont, TRUE);
}

static void WdShowAdvControls(ProtocolHandler* self, HWND hwnd) {
    WebDavData* d = (WebDavData*)self->data;
    int i;
    for (i = 0; i < 13; i++) {
        ShowWindow(d->hAdvLabels[i], SW_SHOW);
        ShowWindow(d->hAdvParamLabels[i], SW_SHOW);
        ShowWindow(d->hAdvDescLabels[i], SW_SHOW);
    }
    for (i = 0; i < 10; i++) ShowWindow(d->hAdvEdits[i], SW_SHOW);
    ShowWindow(d->hAdvComboVfs, SW_SHOW);
    ShowWindow(d->hAdvComboVendor, SW_SHOW);
    ShowWindow(d->hAdvCheckNoCert, SW_SHOW);
    ShowWindow(d->hAdvBtnBrowse, SW_SHOW);
    ShowWindow(d->hAdvBtnBack, SW_SHOW);
    ShowWindow(d->hAdvBtnClearCache, SW_SHOW);
    ShowWindow(d->hAdvBtnSave, SW_SHOW);
    ShowWindow(d->hAdvBtnReset, SW_SHOW);
}

static void WdHideAdvControls(ProtocolHandler* self, HWND hwnd) {
    WebDavData* d = (WebDavData*)self->data;
    int i;
    for (i = 0; i < 13; i++) {
        ShowWindow(d->hAdvLabels[i], SW_HIDE);
        ShowWindow(d->hAdvParamLabels[i], SW_HIDE);
        ShowWindow(d->hAdvDescLabels[i], SW_HIDE);
    }
    for (i = 0; i < 10; i++) ShowWindow(d->hAdvEdits[i], SW_HIDE);
    ShowWindow(d->hAdvComboVfs, SW_HIDE);
    ShowWindow(d->hAdvComboVendor, SW_HIDE);
    ShowWindow(d->hAdvCheckNoCert, SW_HIDE);
    ShowWindow(d->hAdvBtnBrowse, SW_HIDE);
    ShowWindow(d->hAdvBtnBack, SW_HIDE);
    ShowWindow(d->hAdvBtnClearCache, SW_HIDE);
    ShowWindow(d->hAdvBtnSave, SW_HIDE);
    ShowWindow(d->hAdvBtnReset, SW_HIDE);
}

static void WdDestroyControls(ProtocolHandler* self, HWND hwnd) {
    WebDavData* d = (WebDavData*)self->data;
    int i;
    /* 销毁主页面控件 */
    for (i = 0; i < 6; i++) { if (d->hMainLabels[i]) DestroyWindow(d->hMainLabels[i]); d->hMainLabels[i] = NULL; }
    if (d->hHostBox) { DestroyWindow(d->hHostBox); d->hHostBox = NULL; }
    if (d->hPortBox) { DestroyWindow(d->hPortBox); d->hPortBox = NULL; }
    if (d->hPathBox) { DestroyWindow(d->hPathBox); d->hPathBox = NULL; }
    if (d->hUserBox) { DestroyWindow(d->hUserBox); d->hUserBox = NULL; }
    if (d->hPassBox) { DestroyWindow(d->hPassBox); d->hPassBox = NULL; }
    if (d->hSslCheck) { DestroyWindow(d->hSslCheck); d->hSslCheck = NULL; }
    /* 销毁高级设置控件 */
    for (i = 0; i < 13; i++) {
        if (d->hAdvLabels[i]) { DestroyWindow(d->hAdvLabels[i]); d->hAdvLabels[i] = NULL; }
        if (d->hAdvParamLabels[i]) { DestroyWindow(d->hAdvParamLabels[i]); d->hAdvParamLabels[i] = NULL; }
        if (d->hAdvDescLabels[i]) { DestroyWindow(d->hAdvDescLabels[i]); d->hAdvDescLabels[i] = NULL; }
    }
    for (i = 0; i < 10; i++) { if (d->hAdvEdits[i]) { DestroyWindow(d->hAdvEdits[i]); d->hAdvEdits[i] = NULL; } }
    if (d->hAdvComboVfs) { DestroyWindow(d->hAdvComboVfs); d->hAdvComboVfs = NULL; }
    if (d->hAdvComboVendor) { DestroyWindow(d->hAdvComboVendor); d->hAdvComboVendor = NULL; }
    if (d->hAdvCheckNoCert) { DestroyWindow(d->hAdvCheckNoCert); d->hAdvCheckNoCert = NULL; }
    if (d->hAdvBtnBrowse) { DestroyWindow(d->hAdvBtnBrowse); d->hAdvBtnBrowse = NULL; }
    if (d->hAdvBtnBack) { DestroyWindow(d->hAdvBtnBack); d->hAdvBtnBack = NULL; }
    if (d->hAdvBtnClearCache) { DestroyWindow(d->hAdvBtnClearCache); d->hAdvBtnClearCache = NULL; }
    if (d->hAdvBtnSave) { DestroyWindow(d->hAdvBtnSave); d->hAdvBtnSave = NULL; }
    if (d->hAdvBtnReset) { DestroyWindow(d->hAdvBtnReset); d->hAdvBtnReset = NULL; }
}

static void WdUpdateAdvPositions(ProtocolHandler* self, int scrollPos) {
    WebDavData* d = (WebDavData*)self->data;
    HDWP hdwp;
    int y;
    hdwp = BeginDeferWindowPos(58);
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
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[7], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[8], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 9: vfs-cache-max-size */
    y = 825 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[9], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[9], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[8], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[9], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 10: vendor (--webdav-vendor) */
    y = 915 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[10], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[10], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvComboVendor, NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[10], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 11: headers (--webdav-headers) */
    y = 1005 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[11], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[11], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvEdits[9], NULL, 195, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[11], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Row 12: no-check-certificate */
    y = 1095 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvLabels[12], NULL, 20, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvParamLabels[12], NULL, 20, y + 28, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvCheckNoCert, NULL, 195, y + 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvDescLabels[12], NULL, 195, y + 36, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    /* Bottom buttons */
    y = 1185 - scrollPos;
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnBack, NULL, 20, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnClearCache, NULL, 148, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnSave, NULL, 276, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);
    hdwp = DeferWindowPos(hdwp, d->hAdvBtnReset, NULL, 404, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS);

    if (hdwp) EndDeferWindowPos(hdwp);
}

static int WdGetAdvContentHeight(ProtocolHandler* self) {
    (void)self;
    return 1237;  /* 15 + 90*13 + 15 + 32 + 15 */
}

static LRESULT WdHandleCtlColor(ProtocolHandler* self, HWND hCtrl, HDC hdc) {
    WebDavData* d = (WebDavData*)self->data;
    int idx;
    for (idx = 0; idx < 13; idx++) {
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
static int WdHandleCommand(ProtocolHandler* self, HWND hwnd,
                            WPARAM wParam, LPARAM lParam) {
    WebDavData* d = (WebDavData*)self->data;
    CommonConfig* cc = d->commonCfg;
    (void)lParam;

    if (LOWORD(wParam) == WD_IDC_ADV_BTN_SAVE) {
        char transfersBuf[16];
        int vfsSel;
        /* 从 UI 读取通用 VFS 设置到 CommonConfig */
        GetWindowTextA(d->hAdvEdits[0], cc->dir_cache_time, sizeof(cc->dir_cache_time));
        GetWindowTextA(d->hAdvEdits[1], cc->buffer_size, sizeof(cc->buffer_size));
        memset(transfersBuf, 0, sizeof(transfersBuf));
        GetWindowTextA(d->hAdvEdits[2], transfersBuf, sizeof(transfersBuf));
        cc->transfers = atoi(transfersBuf);
        if (cc->transfers <= 0) cc->transfers = 4;
        GetWindowTextA(d->hAdvEdits[3], cc->cache_dir, sizeof(cc->cache_dir));
        GetWindowTextA(d->hAdvEdits[4], cc->vfs_cache_max_age, sizeof(cc->vfs_cache_max_age));
        GetWindowTextA(d->hAdvEdits[5], cc->vfs_read_chunk_size, sizeof(cc->vfs_read_chunk_size));
        GetWindowTextA(d->hAdvEdits[6], cc->vfs_read_chunk_size_limit, sizeof(cc->vfs_read_chunk_size_limit));
        GetWindowTextA(d->hAdvEdits[7], cc->volname, sizeof(cc->volname));
        GetWindowTextA(d->hAdvEdits[8], cc->vfs_cache_max_size, sizeof(cc->vfs_cache_max_size));
        vfsSel = (int)SendMessageW(d->hAdvComboVfs, CB_GETCURSEL, 0, 0);
        cc->vfs_cache_mode = (vfsSel != CB_ERR) ? vfsSel : 2;

        /* 从 UI 读取 WebDAV 专属设置 */
        GetWindowTextA(d->hAdvEdits[9], d->cfg.headers, sizeof(d->cfg.headers));
        {
            int vIdx = (int)SendMessageW(d->hAdvComboVendor, CB_GETCURSEL, 0, 0);
            const char* vendors[] = {"other", "nextcloud", "owncloud", "infinitescale", "sharepoint", "sharepoint-ntlm", "rclone", "fastmail"};
            if (vIdx >= 0 && vIdx < 8) strcpy_s(d->cfg.vendor, sizeof(d->cfg.vendor), vendors[vIdx]);
        }
        d->cfg.no_check_cert = (SendMessageA(d->hAdvCheckNoCert, BM_GETCHECK, 0, 0) == BST_CHECKED);

        self->SaveConfig(self);
        SaveCommonConfig(cc);
        return 2;  /* 已处理：保存后请求 main.c 切换回主页面 */
    }
    else if (LOWORD(wParam) == WD_IDC_ADV_BTN_RESET) {
        /* 重置通用 VFS 设置为默认值 */
        SetWindowTextA(d->hAdvEdits[0], "24h");
        SetWindowTextA(d->hAdvEdits[1], "64M");
        SetWindowTextA(d->hAdvEdits[2], "4");
        SetWindowTextA(d->hAdvEdits[3], "");
        SetWindowTextA(d->hAdvEdits[4], "24h");
        SetWindowTextA(d->hAdvEdits[5], "128M");
        SetWindowTextA(d->hAdvEdits[6], "off");
        SetWindowTextA(d->hAdvEdits[7], "Network_Disk");
        SetWindowTextA(d->hAdvEdits[8], "15G");
        SendMessageW(d->hAdvComboVfs, CB_SETCURSEL, 3, 0);  /* full */
        SetWindowTextW(d->hAdvDescLabels[0], TR("STR_VFS_TIP_FULL"));

        /* 重置 WebDAV 专属设置为默认值 */
        SendMessageW(d->hAdvComboVendor, CB_SETCURSEL, 0, 0);  /* other */
        SetWindowTextA(d->hAdvEdits[9], "");  /* headers */
        SendMessageA(d->hAdvCheckNoCert, BM_SETCHECK, BST_UNCHECKED, 0);  /* no_check_cert */
        return 1;
    }
    else if (LOWORD(wParam) == WD_IDC_ADV_BTN_CLEAR_CACHE) {
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
    else if (LOWORD(wParam) == WD_IDC_ADV_COMBO_VFS && HIWORD(wParam) == CBN_SELCHANGE) {
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
            SetWindowTextW(d->hAdvDescLabels[0], vfsDesc);
        }
        return 1;
    }
    else if (LOWORD(wParam) == WD_IDC_ADV_BTN_BROWSE) {
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
                SetWindowTextA(d->hAdvEdits[3], ansiPath);
            }
            CoTaskMemFree(pidl);
        }
        return 1;
    }
    else if (LOWORD(wParam) == WD_IDC_ADV_BTN_BACK) {
        return 2;  /* 请求 main.c 切换回主页面 */
    }

    return 0;  /* 未处理 */
}

/* ======================================================================
   高级设置 UI ↔ 配置 同步
   ====================================================================== */
static void WdSaveAdvSettingsFromUI(ProtocolHandler* self) {
    WebDavData* d = (WebDavData*)self->data;
    CommonConfig* cc = d->commonCfg;
    char transfersBuf[16];
    int vfsSel;
    /* 通用 VFS 设置 → CommonConfig */
    GetWindowTextA(d->hAdvEdits[0], cc->dir_cache_time, sizeof(cc->dir_cache_time));
    GetWindowTextA(d->hAdvEdits[1], cc->buffer_size, sizeof(cc->buffer_size));
    memset(transfersBuf, 0, sizeof(transfersBuf));
    GetWindowTextA(d->hAdvEdits[2], transfersBuf, sizeof(transfersBuf));
    cc->transfers = atoi(transfersBuf);
    if (cc->transfers <= 0) cc->transfers = 4;
    GetWindowTextA(d->hAdvEdits[3], cc->cache_dir, sizeof(cc->cache_dir));
    GetWindowTextA(d->hAdvEdits[4], cc->vfs_cache_max_age, sizeof(cc->vfs_cache_max_age));
    GetWindowTextA(d->hAdvEdits[5], cc->vfs_read_chunk_size, sizeof(cc->vfs_read_chunk_size));
    GetWindowTextA(d->hAdvEdits[6], cc->vfs_read_chunk_size_limit, sizeof(cc->vfs_read_chunk_size_limit));
    GetWindowTextA(d->hAdvEdits[7], cc->volname, sizeof(cc->volname));
    GetWindowTextA(d->hAdvEdits[8], cc->vfs_cache_max_size, sizeof(cc->vfs_cache_max_size));
    vfsSel = (int)SendMessageW(d->hAdvComboVfs, CB_GETCURSEL, 0, 0);
    cc->vfs_cache_mode = (vfsSel != CB_ERR) ? vfsSel : 2;

    /* WebDAV 专属设置 → WebDavConfig */
    GetWindowTextA(d->hAdvEdits[9], d->cfg.headers, sizeof(d->cfg.headers));
    {
        int vIdx = (int)SendMessageW(d->hAdvComboVendor, CB_GETCURSEL, 0, 0);
        const char* vendors[] = {"other", "nextcloud", "owncloud", "infinitescale", "sharepoint", "sharepoint-ntlm", "rclone", "fastmail"};
        if (vIdx >= 0 && vIdx < 8) strcpy_s(d->cfg.vendor, sizeof(d->cfg.vendor), vendors[vIdx]);
    }
    d->cfg.no_check_cert = (SendMessageA(d->hAdvCheckNoCert, BM_GETCHECK, 0, 0) == BST_CHECKED);
}

static void WdResetAdvSettings(ProtocolHandler* self) {
    WebDavData* d = (WebDavData*)self->data;
    /* 重置通用 VFS 设置为默认值 */
    SetWindowTextA(d->hAdvEdits[0], "24h");
    SetWindowTextA(d->hAdvEdits[1], "64M");
    SetWindowTextA(d->hAdvEdits[2], "4");
    SetWindowTextA(d->hAdvEdits[3], "");
    SetWindowTextA(d->hAdvEdits[4], "24h");
    SetWindowTextA(d->hAdvEdits[5], "128M");
    SetWindowTextA(d->hAdvEdits[6], "off");
    SetWindowTextA(d->hAdvEdits[7], "Network_Disk");
    SetWindowTextA(d->hAdvEdits[8], "15G");
    SendMessageW(d->hAdvComboVfs, CB_SETCURSEL, 3, 0);  /* full */
    SetWindowTextW(d->hAdvDescLabels[0], TR("STR_VFS_TIP_FULL"));

    /* 重置 WebDAV 专属设置为默认值 */
    SendMessageW(d->hAdvComboVendor, CB_SETCURSEL, 0, 0);  /* other */
    SetWindowTextA(d->hAdvEdits[9], "");  /* headers */
    SendMessageA(d->hAdvCheckNoCert, BM_SETCHECK, BST_UNCHECKED, 0);
}

/* ======================================================================
   挂载执行
   ====================================================================== */
static const char* WdGetVfsCacheModeStr(int mode) {
    switch (mode) {
        case 0: return "off";
        case 1: return "minimal";
        case 2: return "writes";
        case 3: return "full";
        default: return "writes";
    }
}

static int WdExecuteMount(ProtocolHandler* self, HWND hwnd,
                           const char* rclonePath, int isAuto) {
    WebDavData* d = (WebDavData*)self->data;
    CommonConfig* cc = d->commonCfg;

    /* 从 UI 读取主页面配置 */
    GetWindowTextA(d->hHostBox, d->cfg.host, sizeof(d->cfg.host));
    GetWindowTextA(d->hPortBox, d->cfg.port, sizeof(d->cfg.port));
    GetWindowTextA(d->hPathBox, d->cfg.path, sizeof(d->cfg.path));
    GetWindowTextA(d->hUserBox, d->cfg.user, sizeof(d->cfg.user));
    GetWindowTextA(d->hPassBox, d->cfg.pass, sizeof(d->cfg.pass));

    /* drive 存储在 CommonConfig 中，由 main.c 的编辑框读取 */
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

    d->cfg.ssl = (SendMessageA(d->hSslCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

    self->SaveConfig(self);
    SaveCommonConfig(cc);

    /* 构建 WebDAV URL */
    const char* scheme = d->cfg.ssl ? "https" : "http";
    char finalUrl[512];
    int isIPv6 = (strchr(d->cfg.host, ':') != NULL);
    if (d->cfg.port[0] != '\0') {
        if (isIPv6)
            sprintf_s(finalUrl, sizeof(finalUrl), "%s://[%s]:%s%s", scheme, d->cfg.host, d->cfg.port, d->cfg.path);
        else
            sprintf_s(finalUrl, sizeof(finalUrl), "%s://%s:%s%s", scheme, d->cfg.host, d->cfg.port, d->cfg.path);
    } else {
        if (isIPv6)
            sprintf_s(finalUrl, sizeof(finalUrl), "%s://[%s]%s", scheme, d->cfg.host, d->cfg.path);
        else
            sprintf_s(finalUrl, sizeof(finalUrl), "%s://%s%s", scheme, d->cfg.host, d->cfg.path);
    }

    LogMessage("INFO", "Mount action triggered with URL: %s", finalUrl);

    /* 密码混淆 */
    char obscuredPass[256] = { 0 };
    RcloneObscurePassword(rclonePath, d->cfg.pass, obscuredPass, sizeof(obscuredPass));

    /* 构建通用 VFS 参数字符串（来自 CommonConfig） */
    char advParams[1024] = { 0 };
    char tmpBuf[256];
    const char* cacheMode = WdGetVfsCacheModeStr(cc->vfs_cache_mode);

    if (cc->dir_cache_time[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--dir-cache-time %s ", cc->dir_cache_time);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    if (cc->buffer_size[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--buffer-size %s ", cc->buffer_size);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    if (cc->transfers > 0) {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--transfers %d ", cc->transfers);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    if (cc->cache_dir[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--cache-dir \"%s\" ", cc->cache_dir);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    if (cc->vfs_cache_max_age[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-cache-max-age %s ", cc->vfs_cache_max_age);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    if (cc->vfs_read_chunk_size[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-read-chunk-size %s ", cc->vfs_read_chunk_size);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    if (cc->vfs_read_chunk_size_limit[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-read-chunk-size-limit %s ", cc->vfs_read_chunk_size_limit);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    if (cc->volname[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--volname \"%s\" ", cc->volname);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }
    if (cc->vfs_cache_max_size[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--vfs-cache-max-size %s ", cc->vfs_cache_max_size);
        strcat_s(advParams, sizeof(advParams), tmpBuf);
    }

    /* 构建 WebDAV 专属参数字符串 */
    char wdParams[512] = { 0 };
    /* --webdav-vendor（非 other 时传递，other 是默认值） */
    if (d->cfg.vendor[0] != '\0' && strcmp(d->cfg.vendor, "other") != 0) {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--webdav-vendor %s ", d->cfg.vendor);
        strcat_s(wdParams, sizeof(wdParams), tmpBuf);
    }
    /* --webdav-headers（非空时传递） */
    if (d->cfg.headers[0] != '\0') {
        sprintf_s(tmpBuf, sizeof(tmpBuf), "--webdav-headers \"%s\" ", d->cfg.headers);
        strcat_s(wdParams, sizeof(wdParams), tmpBuf);
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
            "\"%s\" mount :webdav: %s: --webdav-url \"%s\" --webdav-user \"%s\" --webdav-pass \"%s\" "
            "%s"  /* webdav 专属参数 */
            "--vfs-cache-mode %s "
            "%s"  /* 通用 VFS 参数 */
            "%s"  /* --no-check-certificate（条件） */
            "--log-file \"%s\" -vv",
            rclonePath, cc->drive, finalUrl, d->cfg.user, obscuredPass,
            wdParams, cacheMode, advParams,
            d->cfg.no_check_cert ? "--no-check-certificate " : "",
            logPath
        );
        LogMessage("INFO", "Starting Rclone mount with vfs-cache-mode=%s and debug logging enabled.", cacheMode);
    } else {
        sprintf_s(cmd, sizeof(cmd),
            "\"%s\" mount :webdav: %s: --webdav-url \"%s\" --webdav-user \"%s\" --webdav-pass \"%s\" "
            "%s"  /* webdav 专属参数 */
            "--vfs-cache-mode %s "
            "%s"  /* 通用 VFS 参数 */
            "%s",  /* --no-check-certificate（条件） */
            rclonePath, cc->drive, finalUrl, d->cfg.user, obscuredPass,
            wdParams, cacheMode, advParams,
            d->cfg.no_check_cert ? "--no-check-certificate " : ""
        );
        LogMessage("INFO", "Starting Rclone mount with vfs-cache-mode=%s and debug logging disabled.", cacheMode);
    }

    /* 调用 rclone_manager 执行挂载 */
    if (StartRcloneProcess(cmd, cc->drive)) {
        LogMessage("INFO", "WebDAV mount started successfully on drive %s:", cc->drive);
        return 1;
    }

    LogMessage("ERROR", "WebDAV mount failed to start. Check rclone_error.log for details.");
    if (!isAuto) MessageBoxW(hwnd, TR("MSG_MOUNT_FAIL"), TR("MSG_ERROR"), MB_OK | MB_ICONERROR);
    return 0;
}

/* ======================================================================
   清理
   ====================================================================== */
static void WdDestroy(ProtocolHandler* self) {
    WebDavData* d = (WebDavData*)self->data;
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
ProtocolHandler* CreateWebDavHandler(CommonConfig* commonCfg) {
    WebDavData* d = (WebDavData*)calloc(1, sizeof(WebDavData));
    if (!d) return NULL;

    d->commonCfg = commonCfg;

    ProtocolHandler* h = (ProtocolHandler*)calloc(1, sizeof(ProtocolHandler));
    if (!h) { free(d); return NULL; }

    h->name    = "webdav";
    h->data    = d;

    h->CreateMainControls  = WdCreateMainControls;
    h->ShowMainControls    = WdShowMainControls;
    h->HideMainControls    = WdHideMainControls;
    h->CreateAdvControls   = WdCreateAdvControls;
    h->ShowAdvControls     = WdShowAdvControls;
    h->HideAdvControls     = WdHideAdvControls;
    h->DestroyControls     = WdDestroyControls;
    h->UpdateAdvPositions  = WdUpdateAdvPositions;
    h->GetAdvContentHeight = WdGetAdvContentHeight;
    h->HandleCtlColor      = WdHandleCtlColor;
    h->HandleCommand       = WdHandleCommand;
    h->LoadConfig          = WdLoadConfig;
    h->SaveConfig          = WdSaveConfig;
    h->SaveAdvSettingsFromUI = WdSaveAdvSettingsFromUI;
    h->ResetAdvSettings    = WdResetAdvSettings;
    h->ExecuteMount        = WdExecuteMount;
    h->Destroy             = WdDestroy;

    return h;
}