// One read-only snapshot of the running app; never sends input or changes output.
#include <windows.h>
#include <psapi.h>
#include <magnification.h>
#include <powrprof.h>
#include <cstdio>

void PrintBytes(const void* data, size_t size) {
    auto bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) printf("%02x", bytes[i]);
}

int main() {
    HWND window = FindWindowW(L"ScreenSave", L"Screen Save");
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (!pid) { puts("app_not_running"); return 2; }
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    PROCESS_MEMORY_COUNTERS_EX2 memory{};
    memory.cb = sizeof(memory);
    if (!GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) return 3;
    FILETIME created, exited, kernel, user;
    GetProcessTimes(process, &created, &exited, &kernel, &user);
    ULARGE_INTEGER k{{kernel.dwLowDateTime, kernel.dwHighDateTime}};
    ULARGE_INTEGER u{{user.dwLowDateTime, user.dwHighDateTime}};
    MAGCOLOREFFECT colors{};
    MagInitialize();
    BOOL colorsRead = MagGetFullscreenColorEffect(&colors);
    MagUninitialize();
    HDC dc = GetDC(nullptr);
    WORD gamma[3][256]{};
    BOOL gammaRead = GetDeviceGammaRamp(dc, gamma);
    ReleaseDC(nullptr, dc);
    if (!colorsRead || !gammaRead) return 4;
    EXECUTION_STATE flags{};
    CallNtPowerInformation(SystemExecutionState, nullptr, 0, &flags, sizeof(flags));
    printf("{\"pid\":%lu,\"privateResident\":%llu,\"privateCommit\":%llu,\"workingSet\":%llu,\"cpuMs\":%.4f,\"matrix\":%.3f,\"gammaWhite\":%u,\"execution\":%lu,\"colorsHex\":\"",
        pid, static_cast<unsigned long long>(memory.PrivateWorkingSetSize),
        static_cast<unsigned long long>(memory.PrivateUsage), static_cast<unsigned long long>(memory.WorkingSetSize),
        (k.QuadPart + u.QuadPart) / 10000.0, colors.transform[0][0], gamma[0][255], flags);
    PrintBytes(&colors, sizeof(colors));
    printf("\",\"gammaHex\":\"");
    PrintBytes(gamma, sizeof(gamma));
    printf("\"}\n");
    CloseHandle(process);
}
