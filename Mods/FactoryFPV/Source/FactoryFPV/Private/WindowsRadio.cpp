#include "WindowsRadio.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <string_view>
#if defined(FACTORYFPV_RADIO_PROBE_TEST)
#include <cstring>
#endif

#if defined(_WIN32) && (!defined(PLATFORM_WINDOWS) || PLATFORM_WINDOWS)
#if defined(PLATFORM_WINDOWS)
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#define DIRECTINPUT_VERSION 0x0800
// The CSS Unreal target sets WINVER below the declarations for this API.
// Expose them only for this header. The game runs on supported Win64 builds.
#pragma push_macro("WINVER")
#if WINVER < 0x0602
#undef WINVER
#define WINVER 0x0602
#endif
#include <cfgmgr32.h>
#pragma pop_macro("WINVER")
#include <dinput.h>
#include <objbase.h>
#if defined(PLATFORM_WINDOWS)
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace {
struct DirectInput {
    IDirectInput8W* api = nullptr;
    HRESULT result = E_FAIL;
    DirectInput() {
        result = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
            reinterpret_cast<void**>(&api), nullptr);
    }
    ~DirectInput() { if (api) api->Release(); }
};

BOOL CALLBACK CollectRadio(const DIDEVICEINSTANCEW* instance, void* context) {
    wchar_t id[39]{};
    if (StringFromGUID2(instance->guidInstance, id, 39)) {
        static_cast<std::vector<factoryfpv::RadioDevice>*>(context)->push_back({id, instance->tszProductName});
    }
    return DIENUM_CONTINUE;
}

const std::array<DWORD, 8> AxisOffsets{
    DIJOFS_X, DIJOFS_Y, DIJOFS_Z, DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ,
    DIJOFS_SLIDER(0), DIJOFS_SLIDER(1)
};

bool ReportRadioFailure(std::wstring& error, const wchar_t* operation, HRESULT result) {
    error = std::wstring(operation) + L" failed, HRESULT " + std::to_wstring(static_cast<unsigned long>(result));
    return false;
}

struct RadioRemovalWatch {
    const GUID interfaceClass;
    const std::wstring path;
    HCMNOTIFICATION registration = nullptr;
    std::atomic<bool> removed{false};

    RadioRemovalWatch(const GUID& selectedClass, std::wstring_view selectedPath)
        : interfaceClass(selectedClass), path(selectedPath) {}

    bool start() {
        CM_NOTIFY_FILTER filter{};
        filter.cbSize = sizeof(filter);
        filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
        filter.u.DeviceInterface.ClassGuid = interfaceClass;
        return CM_Register_Notification(&filter, this, onNotification, &registration) == CR_SUCCESS;
    }

    ~RadioRemovalWatch() {
        // Unregistration waits for pending callbacks before releasing their context.
        if (registration) CM_Unregister_Notification(registration);
    }

    static DWORD CALLBACK onNotification(HCMNOTIFICATION, void* context, CM_NOTIFY_ACTION action,
        CM_NOTIFY_EVENT_DATA* event, DWORD eventSize) {
        constexpr size_t LinkOffset = offsetof(CM_NOTIFY_EVENT_DATA, u.DeviceInterface.SymbolicLink);
        if (action != CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL || !context || !event ||
            eventSize < LinkOffset + sizeof(wchar_t) ||
            event->FilterType != CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE) return ERROR_SUCCESS;

        auto& watch = *static_cast<RadioRemovalWatch*>(context);
        if (!IsEqualGUID(event->u.DeviceInterface.ClassGuid, watch.interfaceClass)) return ERROR_SUCCESS;
        const auto* link = reinterpret_cast<const wchar_t*>(reinterpret_cast<const unsigned char*>(event) + LinkOffset);
        const size_t available = (eventSize - LinkOffset) / sizeof(wchar_t);
        const auto length = static_cast<int>(watch.path.size());
        if (watch.path.size() < available && link[length] == L'\0' &&
            CompareStringOrdinal(link, length, watch.path.data(), length, true) == CSTR_EQUAL) {
            watch.removed.store(true, std::memory_order_relaxed);
        }
        return ERROR_SUCCESS;
    }
};

std::unique_ptr<RadioRemovalWatch> WatchRadioRemoval(IDirectInputDevice8W& device) {
    DIPROPGUIDANDPATH identity{};
    identity.diph.dwSize = sizeof(identity);
    identity.diph.dwHeaderSize = sizeof(identity.diph);
    identity.diph.dwHow = DIPH_DEVICE;
    if (FAILED(device.GetProperty(DIPROP_GUIDANDPATH, &identity.diph)) || IsEqualGUID(identity.guidClass, GUID{})) return {};

    const auto* limit = identity.wszPath + MAX_PATH;
    const auto* end = std::find(static_cast<const wchar_t*>(identity.wszPath), limit, L'\0');
    if (end == identity.wszPath || end == limit) return {};
    const std::wstring_view path(identity.wszPath, static_cast<size_t>(end - identity.wszPath));
    auto watch = std::make_unique<RadioRemovalWatch>(identity.guidClass, path);
    if (!watch->start()) return {};
    return watch;
}
}

namespace factoryfpv {
struct WindowsRadio::Impl {
    DirectInput input;
    IDirectInputDevice8W* device = nullptr;
    GUID id{};
    std::chrono::steady_clock::time_point nextStatusCheck{};
    std::unique_ptr<RadioRemovalWatch> removalWatch;

    bool removed() const {
        return removalWatch && removalWatch->removed.load(std::memory_order_relaxed);
    }

    bool sample(DIJOYSTATE2& state, std::wstring& error, bool checkAttachment = false) {
        if (removed()) return ReportRadioFailure(error, L"PnP device interface removal", HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED));
        const auto now = std::chrono::steady_clock::now();
        if (checkAttachment || now >= nextStatusCheck) {
            const HRESULT result = input.api->GetDeviceStatus(id);
            if (result != DI_OK) return ReportRadioFailure(error, L"GetDeviceStatus", result);
            nextStatusCheck = now + std::chrono::milliseconds(250);
        }
        const HRESULT poll = device->Poll();
        if (FAILED(poll)) return ReportRadioFailure(error, L"Poll", poll);
        const HRESULT read = device->GetDeviceState(sizeof(state), &state);
        if (read != DI_OK) return ReportRadioFailure(error, L"GetDeviceState", read);
        if (removed()) return ReportRadioFailure(error, L"PnP device interface removal", HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED));
        return true;
    }
    ~Impl() {
        removalWatch.reset();
        if (device) {
            device->Unacquire();
            device->Release();
        }
    }
};
WindowsRadio::WindowsRadio() = default;
WindowsRadio::~WindowsRadio() = default;

std::vector<RadioDevice> WindowsRadio::devices() {
    DirectInput input;
    std::vector<RadioDevice> result;
    if (input.api) input.api->EnumDevices(DI8DEVCLASS_GAMECTRL, CollectRadio, &result, DIEDFL_ATTACHEDONLY);
    return result;
}

bool WindowsRadio::open(const std::wstring& id, void* window, const std::array<int, 4>* channels) {
    impl.reset();
    lastError.clear();
    info = {};
    presentButtons = {};
    auto fail = [this](const wchar_t* operation, HRESULT result) {
        return ReportRadioFailure(lastError, operation, result);
    };
    if (!window) return fail(L"Game window handle", E_HANDLE);
    if (id.empty()) return fail(L"No radio selected. Use fpv.radio.select <GUID>", E_INVALIDARG);
    auto candidate = std::make_unique<Impl>();
    if (!candidate->input.api) return fail(L"DirectInput8Create", candidate->input.result);
    HRESULT result = CLSIDFromString(id.c_str(), &candidate->id);
    if (FAILED(result) || IsEqualGUID(candidate->id, GUID{})) return fail(L"Radio GUID", E_INVALIDARG);
    result = candidate->input.api->GetDeviceStatus(candidate->id);
    if (result != DI_OK) return fail(L"GetDeviceStatus", result);
    result = candidate->input.api->CreateDevice(candidate->id, &candidate->device, nullptr);
    if (FAILED(result)) return fail(L"CreateDevice", result);

    auto* device = candidate->device;
    result = device->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(result)) return fail(L"SetDataFormat", result);
    result = device->SetCooperativeLevel(static_cast<HWND>(window), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(result)) return fail(L"SetCooperativeLevel", result);
    // Some DirectInput devices do not expose an interface. They retain the periodic attachment check.
    candidate->removalWatch = WatchRadioRemoval(*device);
    if (channels) {
        for (int channel : *channels) {
            if (channel < 0 || channel >= static_cast<int>(AxisOffsets.size())) {
                return fail(L"Axis index outside 0-7", E_INVALIDARG);
            }
            DIDEVICEOBJECTINSTANCEW axis{};
            axis.dwSize = sizeof(axis);
            result = device->GetObjectInfo(&axis, AxisOffsets[channel], DIPH_BYOFFSET);
            if (FAILED(result)) return fail(L"GetObjectInfo", result);
        }
    }
    std::array<RadioAxisInfo,8> reportedAxes{};
    for (size_t axis = 0; axis < AxisOffsets.size(); ++axis) {
        DIDEVICEOBJECTINSTANCEW object{};
        object.dwSize = sizeof(object);
        if (FAILED(device->GetObjectInfo(&object, AxisOffsets[axis], DIPH_BYOFFSET))) continue;
        reportedAxes[axis].present = true;
        DIPROPRANGE range{};
        range.diph.dwSize = sizeof(range);
        range.diph.dwHeaderSize = sizeof(range.diph);
        range.diph.dwObj = AxisOffsets[axis];
        range.diph.dwHow = DIPH_BYOFFSET;
        if (SUCCEEDED(device->GetProperty(DIPROP_RANGE, &range.diph))) {
            reportedAxes[axis].minimum = range.lMin;
            reportedAxes[axis].maximum = range.lMax;
        }
    }
    std::array<bool,128> reportedButtons{};
    for (size_t button = 0; button < reportedButtons.size(); ++button) {
        DIDEVICEOBJECTINSTANCEW object{};
        object.dwSize = sizeof(object);
        const auto offset = static_cast<DWORD>(offsetof(DIJOYSTATE2, rgbButtons) + button);
        reportedButtons[button] = device->GetObjectInfo(&object, offset, DIPH_BYOFFSET) == DI_OK &&
            (object.dwType & DIDFT_BUTTON) != 0;
    }
    result = device->Acquire();
    if (FAILED(result)) return fail(L"Acquire", result);
    // Acquisition alone does not show that the controller can deliver input.
    DIJOYSTATE2 initialState{};
    if (!candidate->sample(initialState, lastError, true)) return false;
    impl = std::move(candidate);
    info = reportedAxes;
    presentButtons = reportedButtons;
    return true;
}

bool WindowsRadio::read(std::array<long, 8>& axes, std::array<bool,128>* buttons) {
    if (!impl) return false;
    DIJOYSTATE2 state{};
    if (!impl->sample(state, lastError)) {
        impl.reset();
        info = {};
        presentButtons = {};
        return false;
    }
    axes = {state.lX, state.lY, state.lZ, state.lRx, state.lRy, state.lRz, state.rglSlider[0], state.rglSlider[1]};
    if (buttons) for (size_t i=0;i<buttons->size();++i) (*buttons)[i]=(state.rgbButtons[i]&0x80)!=0;
    return true;
}

#if defined(FACTORYFPV_RADIO_PROBE_TEST)
bool WindowsRadio::selfTestRemovalNotifications() {
    // GUID_DEVINTERFACE_HID from the Windows SDK. The synthetic interface path
    // cannot match a real HID path, even while the OS registration is live.
    constexpr GUID hidClass{0x4d1e55b2, 0xf16f, 0x11cf, {0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
    constexpr std::wstring_view selected = LR"(\\?\HID#FACTORYFPV_PROBE_SELECTED)";
    constexpr std::wstring_view lowerCase = LR"(\\?\hid#factoryfpv_probe_selected)";
    constexpr std::wstring_view other = LR"(\\?\HID#FACTORYFPV_PROBE_SELECTED_OTHER)";
    WindowsRadio radio;
    radio.impl = std::make_unique<Impl>();
    radio.impl->removalWatch = std::make_unique<RadioRemovalWatch>(hidClass, selected);
    auto& watch = *radio.impl->removalWatch;
    if (!watch.start()) return false;

    const auto notify = [&watch](CM_NOTIFY_ACTION action, std::wstring_view link, const GUID& interfaceClass,
        CM_NOTIFY_FILTER_TYPE type, bool terminated = true) {
        constexpr size_t LinkOffset = offsetof(CM_NOTIFY_EVENT_DATA, u.DeviceInterface.SymbolicLink);
        struct EventBuffer {
            CM_NOTIFY_EVENT_DATA event{};
            wchar_t extra[MAX_PATH]{};
        } buffer;
        buffer.event.FilterType = type;
        buffer.event.u.DeviceInterface.ClassGuid = interfaceClass;
        const auto bytes = link.size() * sizeof(wchar_t);
        std::memcpy(reinterpret_cast<unsigned char*>(&buffer) + LinkOffset, link.data(), bytes);
        const auto eventSize = static_cast<DWORD>(LinkOffset + bytes + (terminated ? sizeof(wchar_t) : 0));
        RadioRemovalWatch::onNotification(nullptr, &watch, action, &buffer.event, eventSize);
    };
    notify(CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL, selected, hidClass, CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE);
    notify(CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL, other, hidClass, CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE);
    notify(CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL, selected, GUID{}, CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE);
    notify(CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL, selected, hidClass, CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE);
    notify(CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL, selected, hidClass, CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE, false);
    if (radio.impl->removed()) return false;

    notify(CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL, lowerCase, hidClass, CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE);
    notify(CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL, selected, hidClass, CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE);
    if (!radio.impl->removed()) return false;

    // No DirectInput device is installed in the synthetic Impl. The normal
    // read must retire it before calling any method or changing the outputs.
    radio.info[0].present = true;
    radio.presentButtons[0] = true;
    std::array<long,8> axes{1, 2, 3, 4, 5, 6, 7, 8};
    std::array<bool,128> buttons{};
    buttons[0] = true;
    const auto expectedAxes = axes;
    const auto expectedButtons = buttons;
    if (radio.read(axes, &buttons) || radio.impl || radio.info[0].present || radio.presentButtons[0] ||
        radio.error().rfind(L"PnP device interface removal", 0) != 0) return false;
    const auto error = radio.error();
    return !radio.read(axes, &buttons) && radio.error() == error && axes == expectedAxes && buttons == expectedButtons;
}
#endif
}
#else
namespace factoryfpv {
struct WindowsRadio::Impl {};
WindowsRadio::WindowsRadio() = default;
WindowsRadio::~WindowsRadio() = default;
std::vector<RadioDevice> WindowsRadio::devices() { return {}; }
bool WindowsRadio::open(const std::wstring&, void*, const std::array<int, 4>*) { return false; }
bool WindowsRadio::read(std::array<long, 8>&, std::array<bool,128>*) { return false; }
}
#endif
