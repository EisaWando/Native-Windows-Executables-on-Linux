/* Build with x86_64-w64-mingw32-gcc; imports only three KERNEL32 APIs. */
#include <windows.h>

void mainCRTStartup(void)
{
	static const char message[] = "hello from PE x64\r\n";
	DWORD written;

	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message,
		  sizeof(message) - 1, &written, NULL);
	ExitProcess(0);
}
