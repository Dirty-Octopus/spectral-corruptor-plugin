# Source edition

This is the MPL-2.0 source edition of Spectral Corruptor. Read README.md and docs/LICENSING.md before changing the licensing interface.

- Build macOS with `cmake --preset mac-release -DSCR_COPY_PLUGIN=OFF`, then `cmake --build Builds/mac-release` and `ctest --test-dir Builds/mac-release --output-on-failure`.
- The supplied licensing backend is intentionally unimplemented. Do not describe a successful build as an activated or usable plugin.
- Do not import official licensing source, cryptographic fixtures, keys, issuer tools, Factory Presets, official binaries or private Git history.
- Keep JUCE at the recorded submodule commit and preserve third-party notices.
- Preserve the MPL-2.0 notices on published source.
- Do not commit or push without the user's explicit instruction.
