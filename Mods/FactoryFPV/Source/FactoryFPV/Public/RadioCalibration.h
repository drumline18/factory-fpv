#pragma once
#include "FlightCore.h"
#include <array>
#include <string>

namespace factoryfpv {
struct RadioAxisInfo {
    bool present = false;
    long minimum = 0;
    long maximum = 65535;
};
struct CalibratedRadioAxis {
    int channel = -1;
    Calibration calibration;
};

// Captures one physical stick axis at a time. The requested positive endpoint
// determines inversion, including pitch-back as the positive pitch direction.
class RadioCalibration {
public:
    std::array<CalibratedRadioAxis, 4> result{};
    int stage = -1;
    std::string error;

    void begin(const std::array<long, 8>& sample, const std::array<RadioAxisInfo, 8>& info) {
        neutral = sample;
        available = info;
        result = {};
        stage = 0;
        error.clear();
        minimum = maximum = current = sample;
    }
    void sample(const std::array<long, 8>& values) {
        if (stage < 0 || stage >= 4) return;
        current = values;
        for (int axis = 0; axis < 8; ++axis) {
            minimum[axis] = std::min(minimum[axis], values[axis]);
            maximum[axis] = std::max(maximum[axis], values[axis]);
        }
    }
    bool capture() {
        if (stage < 0 || stage >= 4) return false;
        int selected = -1;
        double largest = 0, second = 0;
        for (int axis = 0; axis < 8; ++axis) {
            const bool assigned = std::any_of(result.begin(), result.begin() + stage,
                [axis](const auto& item) { return item.channel == axis; });
            if (!available[axis].present || assigned) continue;
            const double span = static_cast<double>(available[axis].maximum) - available[axis].minimum;
            if (span <= 0) continue;
            const double travel = (static_cast<double>(maximum[axis]) - minimum[axis]) / span;
            if (travel > largest) {
                second = largest;
                largest = travel;
                selected = axis;
            } else second = std::max(second, travel);
        }
        if (selected < 0 || largest < 0.5) {
            error = "Move the requested stick through its full range, then hold the indicated endpoint.";
            return false;
        }
        if (second > largest * 0.35) {
            error = "More than one axis moved. Choose Retry and move only the requested stick.";
            return false;
        }
        const double low = minimum[selected], high = maximum[selected];
        const double center = stage == 3 ? (low + high) * 0.5 : neutral[selected];
        if (center - low < (high - low) * 0.15 || high - center < (high - low) * 0.15) {
            error = "The stick did not travel to both sides of neutral. Retry with the stick centered first.";
            return false;
        }
        const double endpoint = (current[selected] - center) / (high - low);
        if (std::abs(endpoint) < 0.3) {
            error = "Hold the indicated endpoint while pressing Capture.";
            return false;
        }
        result[stage] = {selected, {low, center, high, endpoint < 0}};
        ++stage;
        error.clear();
        minimum = maximum = current;
        return true;
    }
    void retry(const std::array<long, 8>& values) {
        neutral = values;
        minimum = maximum = current = values;
        error.clear();
    }
private:
    std::array<RadioAxisInfo, 8> available{};
    std::array<long, 8> neutral{}, minimum{}, maximum{}, current{};
};
}
