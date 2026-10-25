#define _WIN32_WINNT 0x0a00
#include <windows.h>
#include <wintrust.h>
#include <softpub.h>
void mainCRTStartup(void)
{
	static WINTRUST_DATA data;
	static WINTRUST_FILE_INFO file;
	GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
	data.cbStruct = sizeof(data);
	data.dwUIChoice = WTD_UI_NONE;
	data.dwUnionChoice = WTD_CHOICE_FILE;
	data.pFile = &file;
	data.dwStateAction = WTD_STATEACTION_VERIFY;
	file.cbStruct = sizeof(file);
	file.pcwszFilePath = L"Z:\\tmp\\not-a-trusted-program.exe";
	int ok = WinVerifyTrust(0, &action, &data) ==
			 TRUST_E_PROVIDER_UNKNOWN &&
		 data.hWVTStateData == 0;
	data.dwStateAction = WTD_STATEACTION_CLOSE;
	ok &= WinVerifyTrust(0, &action, &data) == 0;
	action.Data1++;
	ok &= WinVerifyTrust(0, &action, &data) == TRUST_E_ACTION_UNKNOWN;
	ok &= WinVerifyTrust(0, 0, &data) == E_INVALIDARG;
	DWORD written;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "trust ok\n" : "trust FAIL\n", ok ? 9 : 11, &written, 0);
	ExitProcess(ok ? 0 : 1);
}
