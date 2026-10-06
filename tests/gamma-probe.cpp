#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

int main(int argc, char** argv) {
    HDC dc = GetDC(nullptr);
    WORD original[3][256]{};
    if (!GetDeviceGammaRamp(dc, original)) return 1;
    printf("original_mid=%u original_white=%u\n",original[0][128],original[0][255]);
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    DWORD hold = argc > 1 ? atoi(argv[1]) : 100;
    int denominator = argc > 2 ? 40 : 20;
    for(int level=0;level<=20;++level) {
        WORD ramp[3][256], actual[3][256]{};
        for(int c=0;c<3;++c) for(int i=0;i<256;++i)
            ramp[c][i]=static_cast<WORD>(original[c][i]*(denominator-level)/denominator);
        SetDeviceGammaRamp(dc, original);
        BOOL set=SetDeviceGammaRamp(dc,ramp);
        Sleep(hold);
        BOOL read=GetDeviceGammaRamp(dc,actual);
        printf("gain=%.2f set=%d read=%d matches=%d white=%u\n",
            (denominator-level)/static_cast<double>(denominator),set,read,memcmp(ramp,actual,sizeof(ramp))==0,actual[0][255]);
        fflush(stdout);
    }
    BOOL restored=SetDeviceGammaRamp(dc,original);
    WORD actual[3][256]{};
    GetDeviceGammaRamp(dc,actual);
    printf("restored=%d exact=%d\n",restored,memcmp(original,actual,sizeof(original))==0);
    SetThreadExecutionState(ES_CONTINUOUS);
    ReleaseDC(nullptr,dc);
}
