#define _WIN32_WINNT 0x0a00
#include <windows.h>
#include <bcrypt.h>
static int failures;
#define CHECK(x) do {if(!(x))failures++;}while(0)
static int digest_is(const BYTE *b,const char *hex)
{const char *digits="0123456789abcdef";for(int i=0;i<32;i++)if(digits[b[i]>>4]!=hex[2*i]||digits[b[i]&15]!=hex[2*i+1])return 0;return 1;}
void mainCRTStartup(void)
{
    BCRYPT_ALG_HANDLE a;BCRYPT_HASH_HANDLE h,copy;BYTE digest[32];ULONG n,value;static BYTE object[1024],key[131],chunk[1000];
    SetLastError(0x12345678);
    CHECK(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,0,0)==0);
    CHECK(BCryptGetProperty(a,BCRYPT_HASH_LENGTH,(BYTE *)&value,4,&n,0)==0&&n==4&&value==32);
    CHECK(BCryptGetProperty(a,BCRYPT_OBJECT_LENGTH,(BYTE *)&value,4,&n,0)==0&&value<=sizeof(object));
    CHECK(BCryptGetProperty(a,BCRYPT_HASH_LENGTH,0,0,&n,0)==(NTSTATUS)0xc0000023&&n==4);
    CHECK(BCryptCreateHash(a,&h,object,sizeof(object),0,0,0)==0);
    CHECK(BCryptFinishHash(h,digest,32,0)==0&&digest_is(digest,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(BCryptHashData(h,(BYTE *)"a",1,0)<0);CHECK(BCryptDestroyHash(h)==0&&BCryptDestroyHash(h)<0);
    CHECK(BCryptCreateHash(a,&h,0,0,0,0,0)==0&&BCryptHashData(h,(BYTE *)"a",1,0)==0);
    CHECK(BCryptDuplicateHash(h,&copy,0,0,0)==0);
    CHECK(BCryptHashData(h,(BYTE *)"bc",2,0)==0&&BCryptFinishHash(h,digest,32,0)==0&&digest_is(digest,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));BCryptDestroyHash(h);
    CHECK(BCryptHashData(copy,(BYTE *)"bc",2,0)==0&&BCryptFinishHash(copy,digest,32,0)==0&&digest_is(digest,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));BCryptDestroyHash(copy);
    CHECK(BCryptCreateHash(a,&h,0,0,0,0,BCRYPT_HASH_REUSABLE_FLAG)==0);
    for(int i=0;i<1000;i++)chunk[i]='a';for(int i=0;i<1000;i++)CHECK(BCryptHashData(h,chunk,1000,0)==0);
    CHECK(BCryptFinishHash(h,digest,32,0)==0&&digest_is(digest,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    CHECK(BCryptFinishHash(h,digest,32,0)==0&&digest_is(digest,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);
    CHECK(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,0,BCRYPT_ALG_HANDLE_HMAC_FLAG)==0);
    for(int i=0;i<20;i++)key[i]=0x0b;
    CHECK(BCryptCreateHash(a,&h,0,0,key,20,0)==0&&BCryptHashData(h,(BYTE *)"Hi There",8,0)==0&&BCryptFinishHash(h,digest,32,0)==0&&digest_is(digest,"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));BCryptDestroyHash(h);
    for(int i=0;i<131;i++)key[i]=0xaa;
    CHECK(BCryptCreateHash(a,&h,0,0,key,131,0)==0&&BCryptHashData(h,(BYTE *)"Test Using Larger Than Block-Size Key - Hash Key First",54,0)==0&&BCryptFinishHash(h,digest,32,0)==0&&digest_is(digest,"60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));
    CHECK(BCryptCloseAlgorithmProvider(a,0)==0);CHECK(BCryptDestroyHash(h)==0);
    CHECK(BCryptGenRandom(0,digest,32,BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0);int nonzero=0;for(int i=0;i<32;i++)nonzero|=digest[i];CHECK(nonzero!=0);
    CHECK(GetLastError()==0x12345678);
    DWORD written;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),failures?"crypto FAIL\n":"crypto ok\n",failures?12:10,&written,0);ExitProcess(failures?1:0);
}
