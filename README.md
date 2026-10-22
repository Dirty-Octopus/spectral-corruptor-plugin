# Spectral Corruptor — Source Edition

A modular spectral audio effect by **dir.oct. / Dirty Octopus**, using C++17 and JUCE 8.0.4. The project's published source files are licensed under **MPL-2.0**; dependencies keep their own licences.

## 官方版本与公开源码

本插件在正常售卖的同时，公开除官方授权加密实现之外的插件源代码。

如果你认可这个项目，并希望支持后续开发，可以 **¥99.9** 购买官方版本。付费支持者可以选择在商店的「支持者」页面留下自己的名字和留言。

如果你不希望付费，也完全没问题。源代码对所有人开放，你可以自行补全授权相关实现并编译使用。

官方版本包含由我们制作的 **Factory Presets**。这些预设是独立内容，**不包含在本仓库中**。预设管理、导入、导出和保存功能的代码照常公开。

付费不是使用这个项目的唯一方式，而是一种更省事的使用方式，也是对后续开发的支持。

## What is included

All plugin DSP, modules, GUI, macros, modulation, state/preset management and build integration are included. The official licensing backend, cryptographic keys and fixtures, issuer tools, licence files, official binaries and Factory Presets are excluded. The licensing interface and a deliberately unimplemented backend are supplied instead.

**The default build compiles but cannot process input audio.** It outputs only pink noise, including with internal bypass or Dry/Wet at zero. No activation string, DEMO entry or official `.sclicense` file can unlock the supplied placeholder. The official 48-hour Demo is unavailable in this source build. Implement your own backend before using your build. See [licensing integration](docs/LICENSING.md).

## v0.2.6 - 2026-10-22

This source edition includes the v0.2.6 editing improvements. Nearby curve points are retained on insertion; GRID X/Y independently use 1-64 divisions, with Command on macOS / Alt on Windows held during dragging to snap. Mapping response graphs start collapsed and have sliders plus editable numeric values. Formula opens a larger editable, resizable native window by default, or an internal plugin window selected in Settings. Colour slots provide a palette and a live HEX swatch.

Official binaries additionally offer a continuous 48-hour machine Demo with real-time processing and latency-compensated dry audio for host-declared offline renders. Its secure storage and authentication implementation is excluded here; the supplied placeholder remains locked and never starts a Demo.

## FFT, Remap and Partial features

The library now contains 38 audio effects in eight folders. This version adds:

- Blackman-Harris, Gaussian, Flat Top, Welch and Triangle FFT windows, plus a custom 2–16x Overlap factor with the actual overlap factor, percentage and hop size displayed.
- Frequency Remap with editable curves or formula node graphs in Hz and logarithmic normalized coordinates. Custom formula parameters create sliders that accept Macro and modulation assignments.
- An independent response curve for each Mapping route. Remap, LFO Custom and Mapping curve nodes support separate X/Y assignments; EQ nodes support Frequency/Gain assignments. Right-click assigns a coordinate, and double-click deletes an internal curve node.
- A PARTIAL folder containing Stretcher, Stiff String, Centroid, P Grid and F Grid. These stages preserve slice identity while moving or resynthesizing its sound, with Band Centre/Energy Centroid anchors and Sine/Triangle/Saw/Square resynthesis. They require more than one Frequency Slice; Solo requires Post Split.
- Per-effect resource accounting and visible bypass status when an effect exceeds the memory budget, while other stages continue and controls remain editable.

See the [FFT, Remap, Partial and curve guide](docs/SPECTRAL_REMAP_PARTIALS.md). Existing Spectral Clipper, eight-band Spectral Equalizer, Shift preset recovery, Audio Follower, drag-to-parameter routing, colour profiles and bilingual Quick Guide remain available; see [the interaction guide](docs/V0_2_UX.md).

The source edition does not download or install official binaries over your own build. Update this source repository and rebuild. JUCE's standard SHA-256 utility remains linked for source compatibility; the official activation backend, native verification implementation and keys remain excluded.

## Build

```sh
git clone --recurse-submodules https://github.com/Dirty-Octopus/spectral-corruptor-plugin.git
cd spectral-corruptor-plugin
cmake --preset mac-release -DSCR_COPY_PLUGIN=OFF
cmake --build Builds/mac-release --parallel 4
ctest --test-dir Builds/mac-release --output-on-failure
```

macOS requires CMake, Ninja and Xcode Command Line Tools; the preset builds arm64 + x86_64 VST3, AU and Standalone. On Windows, use a Visual Studio developer shell with `cmake -B Builds/win-release -G Ninja -DCMAKE_BUILD_TYPE=Release`, then build and run CTest in that directory.

The source edition is named **Spectral Corruptor Source** and has a separate plugin identifier, so an unimplemented local build does not replace the official plugin. Audio controls and preset format remain compatible. The initial preset list contains no bundled Factory Presets; make your own sounds using Add Effect, then save them.

Default CI tests the DSP foundation and verifies that the supplied placeholder stays locked and no factory presets are bundled. After implementing your backend, configure with `-DSCR_TEST_LICENSE_PLACEHOLDER=OFF -DSCR_BUILD_PROCESSOR_TESTS=ON` to build the published processor/UI regressions. Your backend must make test instances usable through its normal lifecycle; no working unlock implementation or official test credentials are provided.

[FFT, Remap and Partials](docs/SPECTRAL_REMAP_PARTIALS.md) · [Harmonics](docs/HARMONIC_EFFECTS.md) · [Spectral shapes](docs/SPECTRAL_SHAPES.md) · [Macros and modulation](docs/MACROS.md) · [GENERIC effects](docs/GENERIC.md)

## Licensing

See [LICENSE](LICENSE) for MPL-2.0 and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency licences. MPL-2.0 applies to the first-party source distributed here, not to the excluded official licensing implementation or Factory Presets. The default runtime limitation describes the supplied placeholder; it does not add restrictions to the rights granted by MPL-2.0.

This repository has an independent public history. Private repository history and release packages are not mirrored here.
