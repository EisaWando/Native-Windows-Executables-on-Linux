#define UNICODE
#include <windows.h>
void mainCRTStartup(void)
{
	DWORD written;
	int ok = 1;
	HINSTANCE module = GetModuleHandleW(0);
	WNDCLASSEXW cls = { 0 };
	cls.cbSize = sizeof(cls);
	cls.lpfnWndProc = DefWindowProcW;
	cls.hInstance = module;
	cls.lpszClassName = L"dialog-item-parent";
	ok &= RegisterClassExW(&cls) != 0;
	HWND parent = CreateWindowExW(0, cls.lpszClassName, L"hidden", 0, 0, 0,
				      0, 0, 0, 0, module, 0);
	HWND edit = CreateWindowExW(0, L"Edit", L"", WS_CHILD, 0, 0, 100, 20,
				    parent, (HMENU)42, module, 0);
	HWND a = CreateWindowExW(0, L"Button", L"a", WS_CHILD | BS_RADIOBUTTON,
				 0, 0, 20, 20, parent, (HMENU)43, module, 0);
	HWND b = CreateWindowExW(0, L"Button", L"b", WS_CHILD | BS_RADIOBUTTON,
				 0, 0, 20, 20, parent, (HMENU)44, module, 0);
	HWND nested = CreateWindowExW(0, L"Static", L"nested", WS_CHILD, 0, 0,
				      0, 0, edit, (HMENU)45, module, 0);
	ok &= parent && edit && a && b && nested;
	ok &= GetDlgCtrlID(edit) == 42 && GetDlgItem(parent, 42) == edit &&
	      !GetDlgItem(parent, 999);
	ok &= IsChild(parent, edit) && IsChild(parent, nested) &&
	      !IsChild(edit, parent) && !IsChild(parent, parent);
	ok &= SetDlgItemTextA(parent, 42, "h\xc3\xa9");
	WCHAR wide[32];
	char ansi[32];
	ok &= GetDlgItemTextW(parent, 42, wide, 32) == 2 && wide[1] == 0xe9;
	ok &= GetDlgItemTextA(parent, 42, ansi, 32) == 3 &&
	      (unsigned char)ansi[1] == 0xc3;
	ok &= GetDlgItemTextA(parent, 42, ansi, 3) == 1 && ansi[1] == 0;
	BOOL translated = FALSE;
	ok &= SetDlgItemInt(parent, 42, (UINT)-2147483647 - 1, TRUE);
	ok &= GetDlgItemInt(parent, 42, &translated, TRUE) == 0x80000000u &&
	      translated;
	ok &= !GetDlgItemInt(parent, 42, &translated, FALSE) && !translated;
	ok &= SetDlgItemInt(parent, 42, 0xffffffffu, FALSE);
	ok &= GetDlgItemInt(parent, 42, &translated, FALSE) == 0xffffffffu &&
	      translated;
	ok &= !GetDlgItemInt(parent, 42, &translated, TRUE) && !translated;
	ok &= SetDlgItemTextW(parent, 42, L"4294967296") &&
	      !GetDlgItemInt(parent, 42, &translated, FALSE) && !translated;
	ok &= SetDlgItemTextW(parent, 42, L"  +17 trailing") &&
	      GetDlgItemInt(parent, 42, &translated, TRUE) == 17 && translated;
	ok &= CheckDlgButton(parent, 43, BST_CHECKED) &&
	      IsDlgButtonChecked(parent, 43) == BST_CHECKED;
	ok &= CheckRadioButton(parent, 43, 44, 44) &&
	      !IsDlgButtonChecked(parent, 43) &&
	      IsDlgButtonChecked(parent, 44) == BST_CHECKED;
	ok &= SendDlgItemMessageW(parent, 44, BM_GETCHECK, 0, 0) == BST_CHECKED;
	ok &= DestroyWindow(parent) && !IsWindow(nested);
	ok &= UnregisterClassW(cls.lpszClassName, module);
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "dialog items ok\n" : "dialog items FAIL\n",
		  ok ? 16 : 18, &written, 0);
	ExitProcess(ok ? 0 : 1);
}
