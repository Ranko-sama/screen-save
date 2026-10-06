#include <windows.h>
#include <shellapi.h>
#include <magnification.h>
#include <wbemidl.h>

constexpr UINT TrayMessage = WM_APP + 1;
constexpr int DimSteps = 20;
IWbemServices* wmi;
IWbemClassObject* brightnessArguments;
BSTR brightnessMethodPath;
MAGCOLOREFFECT originalColors;
HDC display;
WORD originalGamma[3][256];
enum class DimmingMode { Matrix, Gamma };
DimmingMode dimmingMode = DimmingMode::Matrix;
NOTIFYICONDATAW tray{};
int dimLevel = 0;
int previousDimLevel = 0;

IWbemClassObject* Query(const wchar_t* text) {
    BSTR language = SysAllocString(L"WQL");
    BSTR query = SysAllocString(text);
    IEnumWbemClassObject* rows;
    wmi->ExecQuery(language, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &rows);
    IWbemClassObject* row;
    ULONG count;
    rows->Next(WBEM_INFINITE, 1, &row, &count);
    rows->Release();
    SysFreeString(query);
    SysFreeString(language);
    return row;
}

void OpenBrightness() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWbemLocator* locator;
    CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
        reinterpret_cast<void**>(&locator));
    BSTR nameSpace = SysAllocString(L"ROOT\\WMI");
    locator->ConnectServer(nameSpace, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &wmi);
    SysFreeString(nameSpace);
    locator->Release();
    CoSetProxyBlanket(wmi, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    auto instance = Query(L"SELECT * FROM WmiMonitorBrightnessMethods WHERE Active=TRUE");
    VARIANT value;
    VariantInit(&value);
    instance->Get(L"__PATH", 0, &value, nullptr, nullptr);
    brightnessMethodPath = SysAllocString(value.bstrVal);
    VariantClear(&value);
    instance->Release();

    BSTR className = SysAllocString(L"WmiMonitorBrightnessMethods");
    IWbemClassObject* definition;
    wmi->GetObject(className, 0, nullptr, &definition, nullptr);
    SysFreeString(className);
    IWbemClassObject* parameters;
    definition->GetMethod(L"WmiSetBrightness", 0, &parameters, nullptr);
    parameters->SpawnInstance(0, &brightnessArguments);
    parameters->Release();
    definition->Release();
    value.vt = VT_I4;
    value.lVal = 0;
    brightnessArguments->Put(L"Timeout", 0, &value, 0);
}

int ReadBrightness() {
    auto row = Query(L"SELECT CurrentBrightness FROM WmiMonitorBrightness WHERE Active=TRUE");
    VARIANT value;
    VariantInit(&value);
    row->Get(L"CurrentBrightness", 0, &value, nullptr, nullptr);
    const int brightness = value.bVal;
    VariantClear(&value);
    row->Release();
    return brightness;
}

void SetBrightness(int brightness) {
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_UI1;
    value.bVal = static_cast<BYTE>(brightness);
    brightnessArguments->Put(L"Brightness", 0, &value, 0);
    BSTR method = SysAllocString(L"WmiSetBrightness");
    wmi->ExecMethod(brightnessMethodPath, method,
        0, nullptr, brightnessArguments, nullptr, nullptr);
    SysFreeString(method);
}

void ApplyOutput(int level) {
    if (dimmingMode == DimmingMode::Gamma) {
        WORD ramp[3][256];
        // This laptop accepts linear Gamma gains from 1.0 through 0.5.
        for (int channel = 0; channel < 3; ++channel)
            for (int entry = 0; entry < 256; ++entry)
                ramp[channel][entry] = static_cast<WORD>(
                    originalGamma[channel][entry] * (2 * DimSteps - level) / (2 * DimSteps));
        SetDeviceGammaRamp(display, ramp);
    } else {
        auto colors = originalColors;
        const float gain = 1.0f - static_cast<float>(level) / DimSteps;
        for (int row = 0; row < 5; ++row)
            for (int column = 0; column < 3; ++column)
                colors.transform[row][column] *= gain;
        MagSetFullscreenColorEffect(&colors);
    }
}

void ApplyDimming() {
    ApplyOutput(dimLevel);
    SetThreadExecutionState(ES_CONTINUOUS |
        (dimLevel ? ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED : 0));
}

void SelectMode(DimmingMode mode) {
    ApplyOutput(0);
    dimmingMode = mode;
    ApplyDimming();
}

void AdjustBrightness(bool increase) {
    previousDimLevel = 0;
    if (dimLevel > 0) {
        dimLevel = increase ? dimLevel - 1 : min(DimSteps, dimLevel + 1);
        ApplyDimming();
        return;
    }
    const int brightness = ReadBrightness();
    if (!increase && brightness == 0) {
        dimLevel = 1;
        ApplyDimming();
    } else {
        SetBrightness(increase ? min(100, brightness + 5) : max(0, brightness - 5));
    }
}

void ToggleBlack() {
    if (dimLevel == DimSteps) {
        dimLevel = previousDimLevel;
        previousDimLevel = 0;
    } else {
        previousDimLevel = dimLevel;
        dimLevel = DimSteps;
    }
    ApplyDimming();
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_HOTKEY) {
        if (w == 1) AdjustBrightness(false);
        if (w == 2) AdjustBrightness(true);
        if (w == 3) ToggleBlack();
        return 0;
    }
    if (message == TrayMessage && (l == WM_RBUTTONUP || l == WM_LBUTTONUP)) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, dimLevel == DimSteps ? L"恢复" :
            dimmingMode == DimmingMode::Matrix ? L"黑屏" : L"最暗");
        HMENU modes = CreatePopupMenu();
        AppendMenuW(modes, MF_STRING, 3, L"颜色矩阵（可全黑）");
        AppendMenuW(modes, MF_STRING, 4, L"Gamma（不能全黑）");
        CheckMenuRadioItem(modes, 3, 4, dimmingMode == DimmingMode::Matrix ? 3 : 4, MF_BYCOMMAND);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(modes), L"调光方式");
        AppendMenuW(menu, MF_STRING, 2, L"退出");
        POINT cursor;
        GetCursorPos(&cursor);
        HWND foreground = GetForegroundWindow();
        SetForegroundWindow(window);
        const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
            cursor.x, cursor.y, 0, window, nullptr);
        DestroyMenu(menu);
        SetForegroundWindow(foreground);
        if (command) SendMessageW(window, WM_COMMAND, command, 0);
        return 0;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(w) == 1) ToggleBlack();
        if (LOWORD(w) == 2) DestroyWindow(window);
        if (LOWORD(w) == 3) SelectMode(DimmingMode::Matrix);
        if (LOWORD(w) == 4) SelectMode(DimmingMode::Gamma);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    OpenBrightness();
    MagInitialize();
    MagGetFullscreenColorEffect(&originalColors);
    display = GetDC(nullptr);
    GetDeviceGammaRamp(display, originalGamma);
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.lpfnWndProc = WindowProcedure;
    cls.lpszClassName = L"ScreenSave";
    RegisterClassW(&cls);
    // Invisible message receiver only; no covering or rendering window.
    HWND window = CreateWindowW(cls.lpszClassName, L"Screen Save", 0,
        0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    RegisterHotKey(window, 1, MOD_CONTROL | MOD_ALT, VK_NEXT);
    RegisterHotKey(window, 2, MOD_CONTROL | MOD_ALT, VK_PRIOR);
    RegisterHotKey(window, 3, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'B');
    tray.cbSize = sizeof(tray);
    tray.hWnd = window;
    tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = TrayMessage;
    tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(tray.szTip, L"Screen Save · Ctrl+Alt+B 最暗/恢复");
    Shell_NotifyIconW(NIM_ADD, &tray);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    dimLevel = 0;
    ApplyDimming();
    Shell_NotifyIconW(NIM_DELETE, &tray);
    for (int id = 1; id <= 3; ++id) UnregisterHotKey(window, id);
    MagUninitialize();
    ReleaseDC(nullptr, display);
    brightnessArguments->Release();
    wmi->Release();
    SysFreeString(brightnessMethodPath);
    CoUninitialize();
    return 0;
}
