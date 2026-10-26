#define UNICODE
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <commctrl.h>
static void failed(unsigned line)
{
	char s[32] = "imagelist line ";
	unsigned n = 15;
	char rev[12];
	unsigned k = 0;
	do {
		rev[k++] = '0' + line % 10;
		line /= 10;
	} while (line);
	while (k)
		s[n++] = rev[--k];
	s[n++] = '\n';
	DWORD written;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &written, 0);
}
#define CHECK(expr)                       \
	do {                              \
		if (!(expr)) {            \
			ok = 0;           \
			failed(__LINE__); \
		}                         \
	} while (0)
void mainCRTStartup(void)
{
	int ok = 1, w = 0, h = 0;
	DWORD written;
	HMODULE module = GetModuleHandleW(0);
	HIMAGELIST list = ImageList_Create(2, 2, ILC_COLOR32 | ILC_MASK, 1, 1);
	CHECK(list && ImageList_GetImageCount(list) == 0);
	CHECK(ImageList_GetIconSize(list, &w, &h) && w == 2 && h == 2);
	CHECK(!ImageList_Create(0, 2, ILC_COLOR32, 0, 0));
	CHECK(!ImageList_GetIconSize(list, 0, &h) &&
	      GetLastError() == ERROR_INVALID_PARAMETER);
	HBITMAP strip = LoadBitmapW(module, MAKEINTRESOURCEW(201));
	CHECK(strip &&
	      ImageList_AddMasked(list, strip, RGB(255, 0, 255)) == 0 &&
	      ImageList_GetImageCount(list) == 2);
	BITMAPINFO info = { 0 };
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = 4;
	info.bmiHeader.biHeight = -4;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	DWORD *pixels = 0;
	HBITMAP destination = CreateDIBSection(0, &info, DIB_RGB_COLORS,
					       (void **)&pixels, 0, 0);
	HDC dc = CreateCompatibleDC(0);
	HGDIOBJ old = SelectObject(dc, destination);
	CHECK(destination && pixels && dc && old);
	for (unsigned i = 0; i < 16; i++)
		pixels[i] = 0x112233;
	CHECK(ImageList_Draw(list, 0, dc, 0, 0, ILD_NORMAL));
	CHECK((pixels[0] & 0xffffff) == 0xff0000 &&
	      (pixels[1] & 0xffffff) == 0x00ff00 &&
	      (pixels[4] & 0xffffff) == 0xffff00 &&
	      (pixels[5] & 0xffffff) == 0x00ffff);
	CHECK(ImageList_Draw(list, 1, dc, 0, 2, ILD_TRANSPARENT));
	CHECK((pixels[8] & 0xffffff) == 0x112233 &&
	      (pixels[9] & 0xffffff) == 0x0000ff &&
	      (pixels[12] & 0xffffff) == 0x112233 &&
	      (pixels[13] & 0xffffff) == 0xffffff);
	CHECK(ImageList_SetBkColor(list, RGB(1, 2, 3)) == CLR_NONE &&
	      ImageList_GetBkColor(list) == RGB(1, 2, 3));
	CHECK(ImageList_DrawEx(list, 1, dc, 0, 0, 2, 2, CLR_DEFAULT,
			       CLR_DEFAULT, ILD_NORMAL));
	CHECK((pixels[0] & 0xffffff) == 0x010203 &&
	      (pixels[1] & 0xffffff) == 0x0000ff);
	CHECK(ImageList_DrawEx(list, 0, dc, 0, 0, 4, 4, CLR_NONE, CLR_DEFAULT,
			       ILD_SCALE));
	CHECK((pixels[0] & 0xffffff) == 0xff0000 &&
	      (pixels[1] & 0xffffff) == 0xff0000 &&
	      (pixels[3] & 0xffffff) == 0x00ff00 &&
	      (pixels[15] & 0xffffff) == 0x00ffff);
	for (unsigned i = 0; i < 16; i++)
		pixels[i] = 0x808080;
	CHECK(ImageList_DrawEx(list, 0, dc, 0, 0, 2, 2, CLR_NONE, CLR_NONE,
			       ILD_BLEND50));
	CHECK((pixels[0] & 0xffffff) == 0xbf4040);
	HICON icon = ImageList_GetIcon(list, 1, ILD_TRANSPARENT);
	CHECK(icon != 0);
	HIMAGELIST copy = ImageList_Create(2, 2, ILC_COLOR32 | ILC_MASK, 0, 1);
	CHECK(ImageList_ReplaceIcon(copy, -1, icon) == 0 &&
	      ImageList_ReplaceIcon(copy, 0, icon) == 0 &&
	      ImageList_GetImageCount(copy) == 1);
	CHECK(ImageList_Remove(list, 0) && ImageList_GetImageCount(list) == 1);
	CHECK(ImageList_Remove(list, -1) && ImageList_GetImageCount(list) == 0);
	CHECK(!ImageList_Remove(list, 0));
	CHECK(ImageList_SetIconSize(list, 1, 1) &&
	      ImageList_Add(list, strip, 0) == 0 &&
	      ImageList_GetImageCount(list) == 4);
	CHECK(!ImageList_SetIconSize(list, -1, 2));
	CHECK(ImageList_Destroy(list) && !ImageList_Destroy(list) &&
	      GetLastError() == ERROR_INVALID_HANDLE);
	for (unsigned i = 0; i < 16; i++)
		pixels[i] = 0x112233;
	CHECK(ImageList_Draw(copy, 0, dc, 0, 0, ILD_TRANSPARENT) &&
	      (pixels[0] & 0xffffff) == 0x112233 &&
	      (pixels[1] & 0xffffff) == 0x0000ff);
	CHECK(DestroyIcon(icon));
	BYTE mask_bits[4] = { 0x40, 0, 0, 0 };
	HBITMAP mask = CreateBitmap(4, 2, 1, 1, mask_bits);
	HIMAGELIST masked =
		ImageList_Create(2, 2, ILC_COLOR32 | ILC_MASK, 0, 1);
	BITMAP mask_info;
	CHECK(mask &&
	      GetObjectW(mask, sizeof(mask_info), &mask_info) ==
		      sizeof(mask_info) &&
	      mask_info.bmBitsPixel == 1 &&
	      ImageList_Add(masked, strip, mask) == 0);
	for (unsigned i = 0; i < 16; i++)
		pixels[i] = 0x112233;
	CHECK(ImageList_Draw(masked, 0, dc, 0, 0, ILD_TRANSPARENT) &&
	      (pixels[0] & 0xffffff) == 0xff0000 &&
	      (pixels[1] & 0xffffff) == 0x112233);
	CHECK(!ImageList_Draw(copy, 7, dc, 0, 0, 0));
	CHECK(ImageList_Destroy(masked) && ImageList_Destroy(copy) &&
	      DeleteObject(mask) && DeleteObject(strip));
	HIMAGELIST resource =
		ImageList_LoadImageW(module, MAKEINTRESOURCEW(201), 2, 1,
				     RGB(255, 0, 255), IMAGE_BITMAP, 0);
	HIMAGELIST ansi = ImageList_LoadImageA(module, MAKEINTRESOURCEA(201), 2,
					       1, CLR_NONE, IMAGE_BITMAP, 0);
	CHECK(resource && ansi && ImageList_GetImageCount(resource)==2 && ImageList_GetImageCount(ansi)==2);
	CHECK(!ImageList_LoadImageW(module, MAKEINTRESOURCEW(999), 2, 1,
				    CLR_NONE, IMAGE_BITMAP, 0));
	CHECK(ImageList_Destroy(resource) && ImageList_Destroy(ansi));
	CHECK(SelectObject(dc, old) == destination && DeleteDC(dc) &&
	      DeleteObject(destination));
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "imagelist ok\n" : "imagelist FAIL\n", ok ? 13 : 15,
		  &written, 0);
	ExitProcess(ok ? 0 : 1);
}
