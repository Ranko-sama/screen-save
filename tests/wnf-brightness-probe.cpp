// Developer-only, read-only WNF probe. No publication or display changes.
// API declarations: winsiderss/phnt ntrtl.h (WNF section).
#include <windows.h>
#include <cstdio>
#include <cstdlib>

constexpr ULONGLONG BrightnessHotkey = 0x41C6013DA3BD5075;
using Callback = LONG(NTAPI*)(ULONGLONG, ULONG, const GUID*, void*, const void*, ULONG);
using Query = LONG(NTAPI*)(ULONG*, ULONGLONG, Callback, void*, const GUID*);
using Subscribe = LONG(NTAPI*)(void**, ULONGLONG, ULONG, Callback, void*, const GUID*, ULONG, ULONG);
using Unsubscribe = LONG(NTAPI*)(void*);
ULONGLONG started;

LONG NTAPI Observe(ULONGLONG, ULONG stamp, const GUID*, void* context, const void* buffer, ULONG length) {
    printf("elapsed_ms=%llu source=%s stamp=%lu size=%lu bytes=", GetTickCount64() - started,
        static_cast<const char*>(context), stamp, length);
    for (ULONG i = 0; i < length && i < 32; ++i)
        printf("%02x", static_cast<const BYTE*>(buffer)[i]);
    puts("");
    fflush(stdout);
    return 0;
}

int main(int argc, char** argv) {
    started = GetTickCount64();
    HMODULE dll = GetModuleHandleW(L"ntdll.dll");
    auto query = reinterpret_cast<Query>(GetProcAddress(dll, "RtlQueryWnfStateData"));
    auto subscribe = reinterpret_cast<Subscribe>(GetProcAddress(dll, "RtlSubscribeWnfStateChangeNotification"));
    auto unsubscribe = reinterpret_cast<Unsubscribe>(GetProcAddress(dll, "RtlUnsubscribeWnfStateChangeNotification"));
    printf("exports query=%d subscribe=%d unsubscribe=%d\n", !!query, !!subscribe, !!unsubscribe);
    if (!query || !subscribe || !unsubscribe) return 1;
    ULONG stamp = 0;
    char initial[] = "initial", event[] = "event";
    LONG status = query(&stamp, BrightnessHotkey, Observe, initial, nullptr);
    printf("query_status=0x%08lx stamp=%lu\n", status, stamp);
    if (status < 0) return 2;
    void* subscription = nullptr;
    status = subscribe(&subscription, BrightnessHotkey, stamp, Observe, event, nullptr, 0, 0);
    printf("subscribe_status=0x%08lx\n", status);
    fflush(stdout);
    if (status < 0) return 3;
    const int seconds = argc > 1 ? atoi(argv[1]) : 0;
    printf("READY seconds=%d; observation only\n", seconds);
    fflush(stdout);
    if (seconds > 0) Sleep(static_cast<DWORD>(seconds) * 1000);
    status = unsubscribe(subscription);
    printf("unsubscribe_status=0x%08lx\n", status);
    return status < 0 ? 4 : 0;
}
