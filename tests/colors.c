#include <windows.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shlwapi.h>
void mainCRTStartup(void)
{
    int ok=1;WORD h,l,s;DWORD color;BOOL opaque;
    ok&=DwmGetColorizationColor(&color,&opaque)==S_OK&&color==0xff0078d4&&opaque;
    ok&=DwmGetColorizationColor(0,&opaque)==E_INVALIDARG;
    ok&=DwmIsCompositionEnabled(&opaque)==S_OK&&!opaque;
    ok&=!IsThemeActive()&&!IsAppThemed()&&GetThemeAppProperties()==0;
    ColorRGBToHLS(RGB(255,0,0),&h,&l,&s);ok&=h==0&&l==120&&s==240;
    ColorRGBToHLS(RGB(0,255,0),&h,&l,&s);ok&=h==80&&l==120&&s==240;
    ColorRGBToHLS(RGB(0,0,255),&h,&l,&s);ok&=h==160&&l==120&&s==240;
    ok&=ColorHLSToRGB(0,120,240)==RGB(255,0,0)&&ColorHLSToRGB(80,120,240)==RGB(0,255,0)&&ColorHLSToRGB(160,120,240)==RGB(0,0,255);
    for(int r=0;r<=255;r+=51)for(int g=0;g<=255;g+=51)for(int b=0;b<=255;b+=51){
        ColorRGBToHLS(RGB(r,g,b),&h,&l,&s);DWORD c=ColorHLSToRGB(h,l,s);
        int dr=(int)GetRValue(c)-r,dg=(int)GetGValue(c)-g,db=(int)GetBValue(c)-b;
        ok&=dr>=-8&&dr<=8&&dg>=-8&&dg<=8&&db>=-8&&db<=8;
    }
    ok&=ColorAdjustLuma(RGB(255,0,0),1000,TRUE)==RGB(255,255,255);
    ok&=ColorAdjustLuma(RGB(255,0,0),-1000,TRUE)==RGB(0,0,0);
    ok&=ColorAdjustLuma(RGB(255,0,0),500,FALSE)==RGB(255,0,0);
    DWORD written;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),ok?"colors ok\n":"colors FAIL\n",ok?10:12,&written,0);ExitProcess(ok?0:1);
}
