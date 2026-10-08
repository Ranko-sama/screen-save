// Read-only interface discovery. No brightness writes, drivers or settings changes.
#include <windows.h>
#include <wmistr.h>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <cstdio>
#include <vector>

BOOL CALLBACK Monitor(HMONITOR monitor, HDC, LPRECT, LPARAM) {
    DWORD count = 0;
    BOOL ok = GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &count);
    printf("DDC enumerate ok=%d count=%lu error=%lu\n", ok, count, ok ? 0 : GetLastError());
    if (!ok || !count) return TRUE;
    std::vector<PHYSICAL_MONITOR> physical(count);
    if (!GetPhysicalMonitorsFromHMONITOR(monitor, count, physical.data())) return TRUE;
    for (auto& item : physical) {
        DWORD caps = 0, temperatures = 0, minimum = 0, current = 0, maximum = 0;
        ok = GetMonitorCapabilities(item.hPhysicalMonitor, &caps, &temperatures);
        printf("DDC capabilities ok=%d caps=%lu error=%lu\n", ok, caps, ok ? 0 : GetLastError());
        ok = GetMonitorBrightness(item.hPhysicalMonitor, &minimum, &current, &maximum);
        printf("DDC brightness ok=%d min=%lu current=%lu max=%lu error=%lu\n", ok, minimum, current, maximum, ok ? 0 : GetLastError());
    }
    DestroyPhysicalMonitors(count, physical.data());
    return TRUE;
}

int main() {
    HANDLE lcd = CreateFileW(L"\\\\.\\LCD", 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    printf("LegacyLCD open=%d error=%lu\n", lcd != INVALID_HANDLE_VALUE, lcd != INVALID_HANDLE_VALUE ? 0 : GetLastError());
    if (lcd != INVALID_HANDLE_VALUE) CloseHandle(lcd);
    EnumDisplayMonitors(nullptr, nullptr, Monitor, 0);
    HMODULE library = LoadLibraryW(L"advapi32.dll");
    auto open = reinterpret_cast<ULONG(WINAPI*)(GUID*, ULONG, HANDLE*)>(GetProcAddress(library, "WmiOpenBlock"));
    auto query = reinterpret_cast<ULONG(WINAPI*)(HANDLE, ULONG*, void*)>(GetProcAddress(library, "WmiQueryAllDataW"));
    auto close = reinterpret_cast<ULONG(WINAPI*)(HANDLE)>(GetProcAddress(library, "WmiCloseBlock"));
    if (!open || !query || !close) return 1;
    GUID brightness{0xd43412ac,0x67f9,0x4fbb,{0xa0,0x81,0x17,0x52,0xa2,0xc3,0x3e,0x84}};
    GUID methods{0x479b20b4,0x5559,0x46fe,{0xbe,0x97,0x7d,0x22,0x21,0x54,0x42,0x1f}};
    HANDLE block = nullptr;
    ULONG status = open(&brightness, WMIGUID_QUERY, &block);
    printf("NativeWMI read_open status=%lu handle=%p\n", status, block);
    if (!status) {
        alignas(8) BYTE bytes[4096]{};
        ULONG size = sizeof(bytes);
        status = query(block, &size, bytes);
        printf("NativeWMI query status=%lu size=%lu\n", status, size);
        if (!status) {
            auto data = reinterpret_cast<WNODE_ALL_DATA*>(bytes);
            printf("NativeWMI flags=%lx count=%lu data_offset=%lu name_offsets=%lu fixed_size=%lu\n",
                data->WnodeHeader.Flags, data->InstanceCount, data->DataBlockOffset,
                data->OffsetInstanceNameOffsets, data->FixedInstanceSize);
            if (data->InstanceCount && data->DataBlockOffset < size) {
                auto names = reinterpret_cast<ULONG*>(bytes + data->OffsetInstanceNameOffsets);
                const USHORT nameBytes = *reinterpret_cast<USHORT*>(bytes + names[0]);
                printf("NativeWMI instance=%.*ls\n", nameBytes / 2, reinterpret_cast<wchar_t*>(bytes + names[0] + 2));
                printf("NativeWMI data=");
                for (ULONG i = data->DataBlockOffset; i < size && i < data->DataBlockOffset + 128; ++i) printf("%02x ", bytes[i]);
                puts("");
            }
        }
        close(block);
    }
    block = nullptr;
    status = open(&methods, WMIGUID_EXECUTE, &block);
    printf("NativeWMI method_open status=%lu handle=%p (no method invoked)\n", status, block);
    if (!status) close(block);
    FreeLibrary(library);
}
