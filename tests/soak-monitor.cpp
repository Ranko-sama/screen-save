#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <initguid.h>
#include <powrprof.h>

int offEvents = 0, suspendEvents = 0, displayState = -1;
ULONGLONG started;
LRESULT CALLBACK Monitor(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_POWERBROADCAST) {
        if (w == PBT_APMSUSPEND) ++suspendEvents;
        if (w == PBT_POWERSETTINGCHANGE) {
            auto event = reinterpret_cast<POWERBROADCAST_SETTING*>(l);
            if (event->PowerSetting == GUID_CONSOLE_DISPLAY_STATE) {
                displayState = *reinterpret_cast<DWORD*>(event->Data);
                if (displayState != 1) ++offEvents;
                printf("display_state=%d elapsed_ms=%llu\n", displayState, GetTickCount64()-started);
                fflush(stdout);
            }
        }
        return TRUE;
    }
    if (message == WM_TIMER) {
        LASTINPUTINFO input{sizeof(input)};
        GetLastInputInfo(&input);
        printf("elapsed_ms=%llu display_state=%d non_on_events=%d suspend_events=%d input_idle_ms=%lu\n",
            GetTickCount64()-started,displayState,offEvents,suspendEvents,GetTickCount()-input.dwTime);
        fflush(stdout);
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window,message,w,l);
}
int main(int argc,char** argv) {
    started=GetTickCount64();
    WNDCLASSW cls{}; cls.lpfnWndProc=Monitor; cls.lpszClassName=L"ScreenSaveSoakMonitor"; cls.hInstance=GetModuleHandle(nullptr);
    RegisterClassW(&cls);
    HWND window=CreateWindowW(cls.lpszClassName,L"",0,0,0,0,0,nullptr,nullptr,cls.hInstance,nullptr);
    auto notification=RegisterPowerSettingNotification(window,&GUID_CONSOLE_DISPLAY_STATE,DEVICE_NOTIFY_WINDOW_HANDLE);
    SetTimer(window,1,(argc>1?atoi(argv[1]):930)*1000,nullptr);
    MSG message;
    while(GetMessageW(&message,nullptr,0,0)>0) DispatchMessageW(&message);
    UnregisterPowerSettingNotification(notification);
}
