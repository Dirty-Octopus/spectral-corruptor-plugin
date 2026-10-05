# Implementing your own licensing backend

The official implementation is intentionally absent. `Source/licensing/LicenseManager.h` defines the integration contract; the adjacent `.cpp` is an unimplemented placeholder that always rejects activation. It contains no official machine-code derivation, key, signature verification, code format or persistence implementation.

Replace the backend with your own implementation. You may change the interface internals to suit your design while retaining the calls used by the processor and GUI:

- `shared()` supplies a process-local manager shared by instances.
- `isActivated()` is read by the real-time audio path. Keep this operation bounded and lock-free; the supplied header uses an atomic flag.
- `getMachineCode()` provides a display string for the activation page. `machineCode()` and `defaultFile()` are hooks for your own identity and storage choices.
- `validate()` checks a candidate according to your implementation without changing state.
- `activate()` returns success only when your implementation has completed its checks and any required persistence.

Do identity queries, file access and expensive verification outside the audio thread. Preserve the existing failed-activation behavior, safe GUI callbacks and separation between project state and activation state. Do not use the official installation's activation storage for a custom backend.

The placeholder neither reads nor writes licences and never changes the inactive flag. Until you complete it, normal processing and plugin bypass both output pink noise with input blocked. Official activation codes are not supported by the placeholder.

`SCR_TEST_LICENSE_PLACEHOLDER=ON` runs a regression that verifies this default. When replacing the backend, disable that specific test and provide your own backend tests. `SCR_BUILD_PROCESSOR_TESTS=ON` enables the remaining published processor/UI tests, which require usable test instances from your backend. Official cryptographic tests and signed fixtures are not published.

这些是源码接口与实时线程约束，不是可直接激活插件的实现。你需要自行补全授权逻辑。MPL-2.0 赋予源码的修改和再分发权利不因默认占位实现而改变。
