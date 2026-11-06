#pragma once

#include "RadioCalibration.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace factoryfpv {
struct RadioDevice {
    std::wstring id;
    std::wstring name;
};

class WindowsRadio {
public:
    WindowsRadio();
    ~WindowsRadio();
    static std::vector<RadioDevice> devices();
    // Open succeeds only after the selected device returns an input sample.
    bool open(const std::wstring& id, void* window, const std::array<int, 4>* channels);
    // A failed active read retains its error and requires an explicit reopen.
    bool read(std::array<long, 8>& axes, std::array<bool,128>* buttons=nullptr);
    const std::array<RadioAxisInfo, 8>& axisInfo() const { return info; }
    const std::array<bool,128>& buttonPresence() const { return presentButtons; }
    const std::wstring& error() const { return lastError; }
#if defined(_WIN32) && defined(FACTORYFPV_RADIO_PROBE_TEST)
    static bool selfTestRemovalNotifications();
#endif
private:
    std::array<RadioAxisInfo, 8> info{};
    std::array<bool,128> presentButtons{};
    std::wstring lastError;
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
