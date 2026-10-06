#include "../screen-save.cpp"
#include <cstdio>

void ProbeQuery(const char* delivery) {
    BSTR language = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT CurrentBrightness FROM WmiMonitorBrightness WHERE Active=TRUE");
    IEnumWbemClassObject* rows = nullptr;
    HRESULT result = wmi->ExecQuery(language, query,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &rows);
    printf("%s ExecQuery=0x%08lx rows=%s\n", delivery, result, rows ? "present" : "null");
    if (rows) rows->Release();
    SysFreeString(query);
    SysFreeString(language);
}
LRESULT CALLBACK ProbeWindow(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_APP) {
        ProbeQuery(w ? "synchronous SendMessage" : "queued PostMessage");
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
DWORD WINAPI SendProbe(void* target) {
    SendMessageW(static_cast<HWND>(target), WM_APP, 1, 0);
    PostMessageW(static_cast<HWND>(target), WM_APP, 0, 0);
    return 0;
}
int main() {
    OpenBrightness();
    ProbeQuery("direct call");
    WNDCLASSW cls{};
    cls.lpfnWndProc = ProbeWindow;
    cls.lpszClassName = L"ScreenSaveWmiMessageProbe";
    cls.hInstance = GetModuleHandle(nullptr);
    RegisterClassW(&cls);
    HWND window = CreateWindowW(cls.lpszClassName, L"", 0, 0, 0, 0, 0,
        nullptr, nullptr, cls.hInstance, nullptr);
    HANDLE sender = CreateThread(nullptr, 0, SendProbe, window, 0, nullptr);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        DispatchMessageW(&message);
        if (message.message == WM_APP && message.wParam == 0) break;
    }
    WaitForSingleObject(sender, 1000);
    CloseHandle(sender);
    DestroyWindow(window);
    brightnessArguments->Release();
    wmi->Release();
    SysFreeString(brightnessMethodPath);
    CoUninitialize();
}
