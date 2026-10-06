// Developer integration probe for this application's Win32 messages only.
#include <windows.h>
#include <magnification.h>
#include <powrprof.h>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    HWND app = FindWindowW(L"ScreenSave", L"Screen Save");
    int command = argc > 1 ? atoi(argv[1]) : 0;
    if (command && !app) {
        fprintf(stderr, "Screen Save is not running.\n");
        return 1;
    }
    if (command == 9) {
        DWORD pid;
        GetWindowThreadProcessId(app, &pid);
        HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        SendMessageW(app, WM_CLOSE, 0, 0);
        WaitForSingleObject(process, 10000);
        DWORD code;
        GetExitCodeProcess(process, &code);
        printf("exit_code=%lu\n", code);
        CloseHandle(process);
    } else if (command == 3 || command == 4) {
        SendMessageW(app, WM_COMMAND, command, 0);
    } else if (command >= 1 && command <= 2 || command == 5) {
        // Real WM_HOTKEY events are queued; synchronous cross-process messages
        // forbid the handler's outgoing COM calls.
        PostMessageW(app, WM_HOTKEY, command == 5 ? 3 : command, 0);
        Sleep(300);
    }
    MAGCOLOREFFECT colors{};
    MagInitialize();
    MagGetFullscreenColorEffect(&colors);
    MagUninitialize();
    HDC dc = GetDC(nullptr);
    WORD ramp[3][256]{};
    BOOL read = GetDeviceGammaRamp(dc, ramp);
    ReleaseDC(nullptr, dc);
    EXECUTION_STATE flags{};
    CallNtPowerInformation(SystemExecutionState, nullptr, 0, &flags, sizeof(flags));
    printf("matrix=%.3f gamma_read=%d gamma_mid=%u gamma_white=%u execution=%lu\n",
        colors.transform[0][0],read,ramp[0][128],ramp[0][255],flags);
}
