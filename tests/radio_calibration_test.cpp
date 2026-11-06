#include "RadioCalibration.h"
#include <iostream>
#include <stdexcept>
using namespace factoryfpv;
int main() {
    int checks = 0;
    auto check = [&](bool value, const char* label) {
        ++checks;
        if (!value) throw std::runtime_error(label);
    };
    try {
        std::array<RadioAxisInfo, 8> info{};
        for (int axis = 0; axis < 4; ++axis) info[axis] = {true, 0, 65535};
        std::array<long, 8> neutral{32543,33089,0,33057,32767,32767,32767,32767};
        RadioCalibration wizard;
        wizard.begin(neutral, info);
        wizard.sample(neutral);
        check(!wizard.capture(), "stationary sticks rejected");
        auto values = neutral;
        values[0] = 0; wizard.sample(values);
        values[0] = 65182; wizard.sample(values);
        check(wizard.capture(), "roll detected");
        check(wizard.result[0].channel == 0 && !wizard.result[0].calibration.reversed, "roll mapping");
        values = neutral; wizard.sample(values);
        values[1] = 65535; wizard.sample(values);
        values[1] = 0; wizard.sample(values);
        check(wizard.capture(), "pitch detected after release of prior stick");
        check(wizard.result[1].channel == 1 && wizard.result[1].calibration.reversed, "pitch inversion");
        values = neutral; wizard.sample(values);
        values[3] = 0; wizard.sample(values);
        values[3] = 65375; wizard.sample(values);
        check(wizard.capture(), "yaw detected");
        values = neutral; wizard.sample(values);
        values[2] = 65535; wizard.sample(values);
        check(wizard.capture(), "throttle detected");
        check(wizard.stage == 4, "wizard reaches review");
        check(wizard.result[3].calibration.axis(0, true) == 0, "throttle bottom");
        check(wizard.result[3].calibration.axis(65535, true) == 1, "throttle top");
        check(!wizard.capture(), "completed wizard ignores capture");
        wizard.begin(neutral, info);
        values = neutral; values[0] = 0; values[1] = 0; wizard.sample(values);
        values[0] = 65535; values[1] = 65535; wizard.sample(values);
        check(!wizard.capture(), "ambiguous simultaneous axes rejected");
        wizard.retry(neutral);
        values = neutral; values[0] = 0; wizard.sample(values);
        values[0] = 65535; wizard.sample(values);
        values[0] = neutral[0]; wizard.sample(values);
        check(!wizard.capture(), "endpoint must be held");
        wizard.retry(neutral);
        values[0] = 0; wizard.sample(values);
        values[0] = 65535; wizard.sample(values);
        check(wizard.capture(), "retry recovers");
        check(wizard.result[0].calibration.axis(neutral[0]) == 0, "captured neutral maps to zero");

        values = neutral;
        values[1] = 65535;
        wizard.begin(values, info);
        values[0] = 0; wizard.sample(values);
        values[0] = 65182; wizard.sample(values);
        check(wizard.capture(), "roll captured while pitch is held stationary off center");
        const auto savedRoll = wizard.result[0];
        values[0] = neutral[0];
        values[1] = 0; wizard.sample(values);
        check(!wizard.capture() && wizard.stage == 1, "pitch calibration rejects the off-center startup neutral");
        wizard.retry(neutral);
        check(wizard.stage == 1 && wizard.error.empty(), "centered retry stays on pitch and clears the error");
        values = neutral;
        values[1] = 65535; wizard.sample(values);
        values[1] = 0; wizard.sample(values);
        check(wizard.capture() && wizard.stage == 2, "pitch succeeds after centering and retrying");
        const auto& pitch = wizard.result[1];
        check(pitch.channel == 1 && pitch.calibration.center == neutral[1] && pitch.calibration.reversed,
            "retried pitch uses the new center and still captures inversion");
        const auto& roll = wizard.result[0];
        check(roll.channel == savedRoll.channel && roll.calibration.minimum == savedRoll.calibration.minimum &&
            roll.calibration.center == savedRoll.calibration.center && roll.calibration.maximum == savedRoll.calibration.maximum &&
            roll.calibration.reversed == savedRoll.calibration.reversed, "retry preserves completed roll mapping");
        std::cout << checks << " calibration checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
