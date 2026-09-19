---
name: windows_sdk_macro_conflict
description: Windows SDK dlgs.h 头文件预定义的对话框控件ID宏会与C变量名冲突
type: feedback
---

**规则**: 在 Win32 GUI 编程中，变量名不得使用 `edt1`-`edt16`、`stc1`-`stc32`、`cmb1`-`cmb16`、`lst1`-`lst16`、`btn1`-`btn16`、`chk1`-`chk2`、`fra1`-`fra2`、`grp1`-`grp2`、`ico1`-`ico2`、`rct1`-`rct2` 等名称。

**Why**: Windows SDK 的 `<dlgs.h>`（通过 `<windows.h>` 间接包含）预定义了这些标识符作为对话框控件ID宏（如 `#define edt1 0x0480`）。使用这些名称作为变量名会导致宏展开，产生难以理解的编译错误（C2143 syntax error: missing ';' before 'constant'）。

**How to apply**: 所有 HWND 句柄变量使用 `h` 前缀命名（如 `hEdt1`、`hLbl1`、`hBtnOk`），与现有代码风格（`hHostBox`、`hPortBox`）保持一致。此规则同时适用于 MSVC C89 严格模式（/Za）和常规编译。