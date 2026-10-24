#include <windows.h>
void mainCRTStartup(void)
{
    int ok=1;SID_IDENTIFIER_AUTHORITY nt=SECURITY_NT_AUTHORITY,world=SECURITY_WORLD_SID_AUTHORITY;
    PSID admin=0,everyone=0;BOOL member=FALSE;BYTE copy[68];static BYTE info[256];DWORD size;
    ok&=AllocateAndInitializeSid(&nt,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&admin);
    ok&=IsValidSid(admin)&&GetLengthSid(admin)==16&&*GetSidSubAuthorityCount(admin)==2&&*GetSidSubAuthority(admin,1)==544;
    ok&=CopySid(sizeof(copy),copy,admin)&&EqualSid(copy,admin);
    ok&=!CopySid(8,copy,admin)&&GetLastError()==ERROR_INSUFFICIENT_BUFFER;
    ok&=AllocateAndInitializeSid(&world,1,0,0,0,0,0,0,0,0,&everyone)&&CheckTokenMembership(0,everyone,&member)&&member;
    HANDLE token;ok&=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token);
    ok&=!GetTokenInformation(token,TokenUser,0,0,&size)&&GetLastError()==ERROR_INSUFFICIENT_BUFFER&&size>=sizeof(TOKEN_USER)+12;
    ok&=GetTokenInformation(token,TokenUser,info,sizeof(info),&size);
    PSID user=((TOKEN_USER *)info)->User.Sid;ok&=IsValidSid(user)&&(BYTE *)user>=info&&(BYTE *)user+GetLengthSid(user)<=info+size;
    TOKEN_ELEVATION elevation;ok&=GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&size);
    ok&=CheckTokenMembership(0,admin,&member)&&member==(BOOL)elevation.TokenIsElevated;
    ok&=GetTokenInformation(token,TokenIntegrityLevel,info,sizeof(info),&size);
    TOKEN_MANDATORY_LABEL *label=(void *)info;ok&=IsValidSid(label->Label.Sid)&&*GetSidSubAuthority(label->Label.Sid,0)==(elevation.TokenIsElevated?SECURITY_MANDATORY_HIGH_RID:SECURITY_MANDATORY_MEDIUM_RID);
    ok&=WaitForSingleObject(token,0)==WAIT_FAILED&&GetLastError()==ERROR_INVALID_HANDLE;
    HANDLE unused;ok&=!OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,FALSE,&unused)&&GetLastError()==ERROR_NO_TOKEN;
    ok&=CloseHandle(token);ok&=!GetTokenInformation(token,TokenUser,info,sizeof(info),&size)&&GetLastError()==ERROR_INVALID_HANDLE;
    ok&=FreeSid(admin)==0&&FreeSid(everyone)==0;
    DWORD written;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),ok?"security ok\n":"security FAIL\n",ok?12:14,&written,0);ExitProcess(ok?0:1);
}
