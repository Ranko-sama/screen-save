// Developer-only, one-shot external reader. No injection, suspension, heap walk,
// display API, input, working-set trimming, or target-memory writes.
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdio>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using Address = uintptr_t;
struct Allocation {
    DWORD type{};
    size_t reserved{}, committed{}, resident{}, privateResident{};
    std::string kind, name;
};
struct ThreadBasic {
    LONG exitStatus;
    void* teb;
    void* processId;
    void* threadId;
    ULONG_PTR affinity;
    LONG priority, basePriority;
};

void String(const std::string& text) {
    putchar('"');
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') putchar('\\');
        if (c < 32) printf("\\u%04x", c); else putchar(c);
    }
    putchar('"');
}

std::string Utf8(const wchar_t* text) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    result.resize(size - 1);
    return result;
}

Address AllocationBase(HANDLE process, Address address) {
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQueryEx(process, reinterpret_cast<void*>(address), &info, sizeof(info))
        ? reinterpret_cast<Address>(info.AllocationBase) : 0;
}

void Counters(const PROCESS_MEMORY_COUNTERS_EX2& memory) {
    printf("{\"privateResident\":%zu,\"privateCommit\":%zu,\"workingSet\":%zu}",
        memory.PrivateWorkingSetSize, memory.PrivateUsage, memory.WorkingSetSize);
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    DWORD pid = wcstoul(argv[1], nullptr, 10);
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) return 3;
    const ULONGLONG started = GetTickCount64();
    PROCESS_MEMORY_COUNTERS_EX2 before{}, after{};
    before.cb = after.cb = sizeof(before);
    if (!GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&before), sizeof(before))) return 4;

    // Query page residency before reading the small PEB/TEB metadata. Do not
    // fault in or lock nonresident pages just to classify copy-on-write pages.
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    std::map<Address, Allocation> allocations;
    std::vector<PSAPI_WORKING_SET_EX_INFORMATION> pages;
    std::vector<Address> owners;
    for (Address address = 0;;) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQueryEx(process, reinterpret_cast<void*>(address), &info, sizeof(info))) break;
        Address next = reinterpret_cast<Address>(info.BaseAddress) + info.RegionSize;
        if (info.State != MEM_FREE) {
            Address base = reinterpret_cast<Address>(info.AllocationBase);
            auto& item = allocations[base];
            item.type = info.Type;
            if (info.State == MEM_RESERVE) item.reserved += info.RegionSize;
            if (info.State == MEM_COMMIT) {
                item.committed += info.RegionSize;
                for (Address p = reinterpret_cast<Address>(info.BaseAddress); p < next; p += system.dwPageSize) {
                    PSAPI_WORKING_SET_EX_INFORMATION page{};
                    page.VirtualAddress = reinterpret_cast<void*>(p);
                    pages.push_back(page);
                    owners.push_back(base);
                }
            }
        }
        if (next <= address) break;
        address = next;
    }
    for (size_t offset = 0; offset < pages.size(); offset += 4096) {
        const size_t count = (pages.size() - offset < 4096) ? pages.size() - offset : 4096;
        if (!QueryWorkingSetEx(process, pages.data() + offset,
            static_cast<DWORD>(count * sizeof(pages[0])))) return 5;
    }
    for (size_t i = 0; i < pages.size(); ++i) {
        if (!pages[i].VirtualAttributes.Valid) continue;
        auto& item = allocations[owners[i]];
        item.resident += system.dwPageSize;
        if (!pages[i].VirtualAttributes.Shared) item.privateResident += system.dwPageSize;
    }
    // Independently enumerate resident pages to expose coverage discrepancies.
    // This buffer belongs to the observer, not to ScreenSave.
    std::vector<ULONG_PTR> workingSetBuffer(131073);
    bool listRead = QueryWorkingSet(process, workingSetBuffer.data(),
        static_cast<DWORD>(workingSetBuffer.size() * sizeof(ULONG_PTR))) != FALSE;
    auto workingSet = reinterpret_cast<PSAPI_WORKING_SET_INFORMATION*>(workingSetBuffer.data());
    size_t listPrivate = 0;
    std::vector<PSAPI_WORKING_SET_BLOCK> uncovered;
    if (listRead) for (size_t i = 0; i < workingSet->NumberOfEntries; ++i) {
        const auto page = workingSet->WorkingSetInfo[i];
        if (!page.Shared) listPrivate += system.dwPageSize;
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQueryEx(process, reinterpret_cast<void*>(page.VirtualPage * system.dwPageSize), &info, sizeof(info)) ||
            info.State != MEM_COMMIT) uncovered.push_back(page);
    }
    for (auto& [base, item] : allocations) {
        item.kind = item.type == MEM_IMAGE ? "image" : item.type == MEM_MAPPED ? "mapped" : "private_unattributed";
        if (item.type == MEM_IMAGE || item.type == MEM_MAPPED) {
            wchar_t name[1024]{};
            if (GetMappedFileNameW(process, reinterpret_cast<void*>(base), name, 1024)) item.name = Utf8(name);
        }
    }

    // Read only the local x64 PEB heap-directory fields, not heap blocks.
    // Layout cross-checked against System Informer's phnt/ntpebteb.h.
    // Roots identify their containing allocation only: extra heap segments stay
    // unattributed, rather than being guessed from their size or address.
    auto queryProcess = reinterpret_cast<NTSTATUS(NTAPI*)(HANDLE, PROCESSINFOCLASS, void*, ULONG, ULONG*)>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    PROCESS_BASIC_INFORMATION basic{};
    ULONG heapCount = 0;
    size_t metadataBytesRead = 0;
    bool heapDirectoryRead = false;
    auto read = [&](Address address, void* buffer, size_t size) {
        SIZE_T got = 0;
        BOOL ok = ReadProcessMemory(process, reinterpret_cast<void*>(address), buffer, size, &got);
        metadataBytesRead += got;
        return ok && got == size;
    };
    if (queryProcess(process, ProcessBasicInformation, &basic, sizeof(basic), nullptr) >= 0) {
        Address peb = reinterpret_cast<Address>(basic.PebBaseAddress), heapArray = 0, processHeap = 0;
        Address base = AllocationBase(process, peb);
        if (allocations.count(base)) allocations[base].kind = "peb_allocation";
        if (read(peb + 0x30, &processHeap, sizeof(processHeap)) &&
            read(peb + 0xe8, &heapCount, sizeof(heapCount)) &&
            read(peb + 0xf0, &heapArray, sizeof(heapArray)) && heapCount > 0 && heapCount <= 128) {
            std::vector<Address> heaps(heapCount);
            heapDirectoryRead = read(heapArray, heaps.data(), heaps.size() * sizeof(Address));
            if (heapDirectoryRead) for (Address heap : heaps) {
                base = AllocationBase(process, heap);
                if (allocations.count(base)) {
                    allocations[base].kind = "heap_root_allocation";
                    allocations[base].name = heap == processHeap ? "default process heap" : "additional heap root";
                }
            }
        }
    }
    auto queryThread = reinterpret_cast<NTSTATUS(NTAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*)>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
    HANDLE threadList = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 thread{sizeof(thread)};
    size_t threads = 0, stacksFound = 0;
    if (Thread32First(threadList, &thread)) do {
        if (thread.th32OwnerProcessID != pid) continue;
        ++threads;
        HANDLE handle = OpenThread(THREAD_QUERY_INFORMATION, FALSE, thread.th32ThreadID);
        ThreadBasic detail{};
        if (handle && queryThread(handle, 0, &detail, sizeof(detail), nullptr) >= 0) {
            Address teb = reinterpret_cast<Address>(detail.teb), base = AllocationBase(process, teb);
            if (allocations.count(base)) allocations[base].kind =
                allocations[base].kind == "peb_allocation" ? "peb_teb_allocation" : "teb_allocation";
            NT_TIB tib{};
            if (read(teb, &tib, sizeof(tib)) && tib.StackBase) {
                base = AllocationBase(process, reinterpret_cast<Address>(tib.StackBase) - 1);
                if (allocations.count(base)) {
                    allocations[base].kind = "thread_stack";
                    allocations[base].name = "thread " + std::to_string(thread.th32ThreadID);
                    ++stacksFound;
                }
            }
        }
        if (handle) CloseHandle(handle);
    } while (Thread32Next(threadList, &thread));
    CloseHandle(threadList);
    if (!GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after), sizeof(after))) return 6;
    printf("{\"pid\":%lu,\"durationMs\":%llu,\"before\":", pid, GetTickCount64() - started);
    Counters(before);
    printf(",\"after\":"); Counters(after);
    printf(",\"residentListRead\":%s,\"residentListBytes\":%zu,\"residentListPrivateBytes\":%zu,\"uncoveredPages\":[",
        listRead ? "true" : "false", listRead ? workingSet->NumberOfEntries * system.dwPageSize : 0, listPrivate);
    for (size_t i = 0; i < uncovered.size(); ++i)
        printf("%s{\"address\":\"0x%llx\",\"shared\":%u}", i ? "," : "",
            static_cast<unsigned long long>(uncovered[i].VirtualPage * system.dwPageSize), static_cast<unsigned>(uncovered[i].Shared));
    printf("],\"metadataBytesRead\":%zu,\"heapDirectoryRead\":%s,\"heapCount\":%lu,\"threads\":%zu,\"stacksFound\":%zu,\"allocations\":[",
        metadataBytesRead, heapDirectoryRead ? "true" : "false", heapCount, threads, stacksFound);
    bool first = true;
    for (const auto& [base, item] : allocations) {
        if (!first) putchar(','); first = false;
        printf("{\"base\":\"0x%llx\",\"kind\":", static_cast<unsigned long long>(base)); String(item.kind);
        printf(",\"name\":"); String(item.name);
        printf(",\"type\":%lu,\"reservedBytes\":%zu,\"committedBytes\":%zu,\"residentBytes\":%zu,\"privateResidentBytes\":%zu}",
            item.type, item.reserved, item.committed, item.resident, item.privateResident);
    }
    puts("]}");
    CloseHandle(process);
    return 0;
}
