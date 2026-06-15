---
uid: ap-windows-troubleshooting
---

# Troubleshooting

Use this page to diagnose common setup, build, and runtime issues for the GiantArmy Adaptive Performance Windows provider.

## Build and setup checks

1. Confirm your target platform is Windows standalone.
2. Confirm the provider package is installed and enabled in **Project Settings > Adaptive Performance > Windows**.
3. Confirm the Windows provider settings asset is present in build preprocessing output.

## Runtime checks

1. Build and run on a Windows machine; thermal and low-power notifications aren’t representative in most editor paths.
2. Enable **Windows Provider Logging** in development builds if you need diagnostics.
3. Verify warning level and temperature level values change during thermal pressure scenarios.
4. Verify performance mode changes when Low Power Mode is toggled.

## Common symptoms

### Provider doesn’t initialize

Possible causes:
1. App isn’t running on Windows.
2. Provider isn’t selected in Adaptive Performance project settings.
3. Required runtime package dependencies are missing.

### Warning/temperature stays static

Possible causes:
1. Device didn’t enter a meaningful thermal transition.
2. Native thermal API isn’t available on current runtime context.
3. Build was tested only in editor simulation rather than on hardware.

### Performance mode doesn’t switch to battery mode

Possible causes:
1. Low Power Mode listener isn’t active due to unsupported runtime context.
2. Low Power Mode wasn’t enabled at OS level.
3. App hasn’t resumed/updated after the state change.

## Known platform constraints

1. Scheduler hint APIs are unavailable on Windows.
2. GameMode/GameState-style controls are unavailable on Windows.
3. Discrete CPU/GPU level setters and boost toggles are unsupported on Windows.

Refer to [Windows feature parity matrix](parity-matrix.md) for the complete parity status.