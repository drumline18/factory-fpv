# Factory FPV

Factory FPV adds a remotely piloted FPV quadcopter to Satisfactory. It supports acro and self-level flight, USB radio calibration and switch bindings, camera and OSD tuning, and powered race courses with split times and a saved ghost.

This source snapshot corresponds to the `0.3.0-beta.1` Windows Steam single-player release, tested with Satisfactory 1.2.5.0 (CL517554) and Satisfactory Mod Loader 3.12.0. The playable release includes the cooked Unreal assets. This repository contains the C++ source, portable tests, Windows radio probe and build tools. Factory FPV is an unofficial community mod.

## Run the portable tests

Install CMake and a C++17 toolchain. On Windows, use an MSVC developer prompt with the Windows SDK.

```sh
cmake -S . -B build/core
cmake --build build/core --config Release
ctest --test-dir build/core -C Release --output-on-failure
```

The tests cover flight math, radio calibration and switch behavior, audio synthesis, simulated battery and damage, and race timing. The Windows build also produces `radio_probe`, a local DirectInput diagnostic. The portable tests run independently of the game and its SDK.

## Build for Satisfactory

A full game package also requires the matching CL517554 Satisfactory modding starter with SML 3.12.0, Unreal Engine 5.6.1-CSS, MSVC Build Tools, a Windows SDK, the matching Wwise integration, and the original plugin's Unreal content. See the [Satisfactory modding setup guide](https://docs.ficsit.app/satisfactory-modding/latest/Development/BeginnersGuide/dependencies.html) for the separate toolchain setup.

The development build script expects the starter and Build Tools under `.deps/` inside the checkout. With the matching toolchain and licensed asset inputs, `tools/build-mod.ps1 -EngineRoot .deps/UE-CSS -Package` builds and cooks the Windows Steam plugin. The public source snapshot omits the art, so a complete release cannot be rebuilt from this repository alone.

## Credits and licenses

- Factory FPV contributor code uses the [MIT license](LICENSE).
- The flight model adapts [invilso's GTA FPV flight model](https://github.com/invilso/gta-fpv-drone) under its [MIT license](licenses/invilso-MIT.txt).
- The Windows runtime uses vendored [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu and its HDE components by Vyacheslav Patkov under [separate BSD terms](Mods/FactoryFPV/MinHook-LICENSE.txt).
- The playable mod includes the VT323 font by the VT323 Project Authors under the SIL Open Font License. The release's pilot art and derived radio icon are separate from the MIT source license. Satisfactory and FICSIT branding belong to Coffee Stain.

## AI and network disclosure

Meshy generated the release's radio and goggles art. The radio inventory icon is a render of that radio model. AI helped write scripts for the Scout and race assets, authored portions of the mod's code including procedural audio, and assisted with documentation and listing materials. The mod runs without a generative AI service and makes no external network connections.
