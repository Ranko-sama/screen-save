// Diagnostic build only. Measurements precede file output; no display writes.
#include <psapi.h>
#include <cstdio>
struct InitSample {
    const char* stage;
    PROCESS_MEMORY_COUNTERS_EX2 memory;
    HMODULE modules[96];
    DWORD moduleBytes;
};
InitSample initSamples[12];
int initCount;

void InitMark(const char* stage) {
    auto& sample = initSamples[initCount++];
    sample.stage = stage;
    sample.memory.cb = sizeof(sample.memory);
    EnumProcessModules(GetCurrentProcess(), sample.modules, sizeof(sample.modules), &sample.moduleBytes);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&sample.memory), sizeof(sample.memory));
}

void InitSave() {
    HANDLE file = CreateFileW(INIT_OUTPUT, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    auto write = [&](const char* text) { DWORD count; WriteFile(file, text, static_cast<DWORD>(strlen(text)), &count, nullptr); };
    write("[\n");
    for (int i = 0; i < initCount; ++i) {
        const auto& sample = initSamples[i];
        char line[1024];
        sprintf_s(line, "%s{\"stage\":\"%s\",\"privateResident\":%zu,\"privateCommit\":%zu,\"workingSet\":%zu,\"modules\":[",
            i ? ",\n" : "", sample.stage, sample.memory.PrivateWorkingSetSize,
            sample.memory.PrivateUsage, sample.memory.WorkingSetSize);
        write(line);
        for (DWORD j = 0; j < sample.moduleBytes / sizeof(HMODULE); ++j) {
            char path[1024]{};
            GetModuleFileNameA(sample.modules[j], path, sizeof(path));
            const char* name = strrchr(path, '\\');
            sprintf_s(line, "%s\"%s\"", j ? "," : "", name ? name + 1 : path);
            write(line);
        }
        write("]}");
    }
    write("\n]\n");
    CloseHandle(file);
}
