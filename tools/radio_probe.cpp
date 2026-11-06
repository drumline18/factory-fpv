#include "WindowsRadio.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <iostream>

namespace {
bool CheckRefusedOpen(factoryfpv::WindowsRadio& radio, const std::wstring& id, HWND window, const wchar_t* operation) {
    if (radio.open(id, window, nullptr)) {
        std::wcerr << L"Unexpectedly opened the self-test device.\n";
        return false;
    }
    const auto diagnostic = radio.error();
    const auto prefix = std::wstring(operation) + L" failed, HRESULT ";
    const bool identified = diagnostic.compare(0, prefix.size(), prefix) == 0 &&
        diagnostic.size() > prefix.size() && diagnostic.find_first_not_of(L"0123456789", prefix.size()) == std::wstring::npos;
    std::array<long,8> axes{};
    axes.fill(-1);
    std::array<bool,128> buttons{};
    buttons.fill(true);
    const bool stayedClosed = !radio.read(axes, &buttons) && radio.error() == diagnostic &&
        std::all_of(axes.begin(), axes.end(), [](long value) { return value == -1; }) &&
        std::all_of(buttons.begin(), buttons.end(), [](bool pressed) { return pressed; });
    const bool unpublished = std::none_of(radio.axisInfo().begin(), radio.axisInfo().end(),
        [](const auto& axis) { return axis.present; }) &&
        std::none_of(radio.buttonPresence().begin(), radio.buttonPresence().end(), [](bool present) { return present; });
    const bool passed = identified && stayedClosed && unpublished;
    auto& output = passed ? std::wcout : std::wcerr;
    output << (passed ? L"PASS " : L"FAIL ") << operation << L": " << diagnostic << L'\n';
    return passed;
}

int SelfTest() {
    GUID unused{};
    wchar_t id[39]{};
    const auto window = GetDesktopWindow();
    if (!window || FAILED(CoCreateGuid(&unused)) || !StringFromGUID2(unused, id, 39)) {
        std::wcerr << L"Could not prepare an unattached radio test ID.\n";
        return 1;
    }
    const auto devices = factoryfpv::WindowsRadio::devices();
    if (std::any_of(devices.begin(), devices.end(), [id](const auto& device) { return device.id == id; })) {
        std::wcerr << L"The generated self-test ID is attached.\n";
        return 1;
    }
    factoryfpv::WindowsRadio radio;
    const bool missingWindow = CheckRefusedOpen(radio, id, nullptr, L"Game window handle");
    const bool noSelection = CheckRefusedOpen(radio, L"", window, L"No radio selected. Use fpv.radio.select <GUID>");
    const bool malformed = CheckRefusedOpen(radio, L"{INVALID-GUID}", window, L"Radio GUID");
    const bool unattached = CheckRefusedOpen(radio, id, window, L"GetDeviceStatus");
    bool removal = true;
#if defined(FACTORYFPV_RADIO_PROBE_TEST)
    removal = factoryfpv::WindowsRadio::selfTestRemovalNotifications();
    auto& output = removal ? std::wcout : std::wcerr;
    output << (removal ? L"PASS" : L"FAIL") << L" exact Windows HID removal and sticky read invalidation\n";
#endif
    return missingWindow && noSelection && malformed && unattached && removal ? 0 : 1;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 1) {
        const auto devices = factoryfpv::WindowsRadio::devices();
        for (const auto& device : devices) {
            std::wcout << device.id << L"  " << device.name << L'\n';
        }
        if (devices.empty()) std::wcout << L"No DirectInput controllers found.\n";
        return 0;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--self-test") return SelfTest();
    if (argc != 2 && !(argc==3 && std::wstring(argv[2])==L"--snapshot")) {
        std::wcerr << L"Usage: radio_probe [--self-test | controller GUID [--snapshot]]\n";
        return 2;
    }

    const HWND window = CreateWindowExW(0, L"STATIC", L"Factory FPV radio probe",
        WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    int result = 0;
    {
        factoryfpv::WindowsRadio radio;
        if (!radio.open(argv[1], window, nullptr)) {
            std::wcerr << L"Could not open that DirectInput controller. " << radio.error() << L'\n';
            result = 1;
        } else {
            std::wcout << (argc==3 ? L"Radio snapshot (axes and held buttons).\n" : L"Move one stick/switch at a time. Watching for 30 seconds.\n");
            for (int sample = 0; sample < (argc==3 ? 1 : 300); ++sample) {
                std::array<long, 8> axes{};
                std::array<bool,128> buttons{};
                if (!radio.read(axes,&buttons)) {
                    std::wcerr << L"\nController input lost. " << radio.error() << L'\n';
                    result = 1;
                    break;
                }
                std::wcout << L"\r";
                constexpr const wchar_t* names[] = {L"X", L"Y", L"Z", L"Rx", L"Ry", L"Rz", L"S0", L"S1"};
                for (size_t axis = 0; axis < axes.size(); ++axis) {
                    std::wcout << names[axis] << L"=" << axes[axis] << L"   ";
                }
                std::wcout << L" Buttons: ";
                for(size_t button=0;button<buttons.size();++button) if(buttons[button]) std::wcout << (button+1) << L" ";
                std::wcout << L"        " << std::flush;
                Sleep(100);
            }
            std::wcout << L'\n';
        }
    }
    if (window) DestroyWindow(window);
    return result;
}


