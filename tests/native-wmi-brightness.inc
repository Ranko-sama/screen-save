// Experimental replacement for this laptop's single WMI brightness instance.
// GUIDs and parameter layout were read from the local WMI class metadata.
HMODULE brightnessLibrary;
HANDLE brightnessDataBlock, brightnessMethodBlock;
ULONG (WINAPI* nativeOpen)(GUID*, ULONG, HANDLE*);
ULONG (WINAPI* nativeQuery)(HANDLE, ULONG*, void*);
ULONG (WINAPI* nativeClose)(HANDLE);
ULONG (WINAPI* nativeExecute)(HANDLE, const wchar_t*, ULONG, ULONG, void*, ULONG*, void*);
wchar_t brightnessInstance[128];

void OpenBrightness() {
    brightnessLibrary = LoadLibraryW(L"advapi32.dll");
    nativeOpen = reinterpret_cast<decltype(nativeOpen)>(GetProcAddress(brightnessLibrary, "WmiOpenBlock"));
    nativeQuery = reinterpret_cast<decltype(nativeQuery)>(GetProcAddress(brightnessLibrary, "WmiQueryAllDataW"));
    nativeClose = reinterpret_cast<decltype(nativeClose)>(GetProcAddress(brightnessLibrary, "WmiCloseBlock"));
    nativeExecute = reinterpret_cast<decltype(nativeExecute)>(GetProcAddress(brightnessLibrary, "WmiExecuteMethodW"));
    GUID data{0xd43412ac, 0x67f9, 0x4fbb, {0xa0, 0x81, 0x17, 0x52, 0xa2, 0xc3, 0x3e, 0x84}};
    GUID methods{0x479b20b4, 0x5559, 0x46fe, {0xbe, 0x97, 0x7d, 0x22, 0x21, 0x54, 0x42, 0x1f}};
    nativeOpen(&data, WMIGUID_QUERY, &brightnessDataBlock);
    nativeOpen(&methods, WMIGUID_EXECUTE, &brightnessMethodBlock);
    alignas(8) BYTE bytes[512]{};
    ULONG size = sizeof(bytes);
    nativeQuery(brightnessDataBlock, &size, bytes);
    auto node = reinterpret_cast<WNODE_ALL_DATA*>(bytes);
    auto nameOffsets = reinterpret_cast<ULONG*>(bytes + node->OffsetInstanceNameOffsets);
    const BYTE* name = bytes + nameOffsets[0];
    const USHORT nameBytes = *reinterpret_cast<const USHORT*>(name);
    memcpy(brightnessInstance, name + sizeof(USHORT), nameBytes);
    brightnessInstance[nameBytes / sizeof(wchar_t)] = 0;
}

int ReadBrightness() {
    alignas(8) BYTE bytes[512]{};
    ULONG size = sizeof(bytes);
    nativeQuery(brightnessDataBlock, &size, bytes);
    return bytes[reinterpret_cast<WNODE_ALL_DATA*>(bytes)->DataBlockOffset];
}

void SetBrightness(int brightness) {
    // WmiSetBrightness method 1: uint32 Timeout (0), then uint8 Brightness.
    BYTE parameters[5]{0, 0, 0, 0, static_cast<BYTE>(brightness)};
    ULONG result = 0, resultBytes = sizeof(result);
    nativeExecute(brightnessMethodBlock, brightnessInstance, 1,
        sizeof(parameters), parameters, &resultBytes, &result);
}

void CloseBrightness() {
    nativeClose(brightnessMethodBlock);
    nativeClose(brightnessDataBlock);
    FreeLibrary(brightnessLibrary);
}
