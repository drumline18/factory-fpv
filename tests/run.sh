#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I Mods/FactoryFPV/Source/FactoryFPV/Public tests/flight_core_test.cpp -o build/flight_core_test
./build/flight_core_test
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I Mods/FactoryFPV/Source/FactoryFPV/Public tests/radio_calibration_test.cpp -o build/radio_calibration_test
./build/radio_calibration_test
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I Mods/FactoryFPV/Source/FactoryFPV/Public tests/prop_sound_test.cpp -o build/prop_sound_test
./build/prop_sound_test
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I Mods/FactoryFPV/Source/FactoryFPV/Public tests/radio_arm_test.cpp -o build/radio_arm_test
./build/radio_arm_test
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I Mods/FactoryFPV/Source/FactoryFPV/Public tests/drone_battery_test.cpp -o build/drone_battery_test
./build/drone_battery_test
for test in radio_actions_test drone_damage_test race_core_test; do
  "${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
    -I Mods/FactoryFPV/Source/FactoryFPV/Public "tests/$test.cpp" -o "build/$test"
  "./build/$test"
done
