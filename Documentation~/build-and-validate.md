---
uid: ap-windows-build-and-validate
---

# Build and validate the Windows provider

Use this guide to build a Windows player with the GiantArmy Adaptive Performance Windows provider and validate the integration end to end.

## Prerequisites

1. Unity 6 (6000.0 or newer).
2. A Windows standalone build target.
3. A Windows machine for validation.
4. The Adaptive Performance package installed in your Unity project.

## Configure the provider

1. Open **Edit > Project Settings > Adaptive Performance**.
2. Open the **Windows** tab.
3. In **Providers**, enable **Windows Provider**.
4. Optional: in **Development Settings**, enable **Windows Provider Logging**.

## Build from Unity

1. Open **File > Build Settings**.
2. Select **Windows, Mac, Linux** and click **Switch Platform** if needed.
3. Confirm your Player settings target Windows standalone.
4. Click **Build** (or **Build And Run**) and produce a Windows player.

During Windows preprocess build, this package injects the provider settings asset into preloaded assets when the Windows provider is enabled.

## Build and run on Windows

1. Run the generated Windows executable.
2. Confirm the provider settings are loaded from the active build profile.
3. Verify the runtime logs and Adaptive Performance values are updating.
4. Build and run on a Windows machine.

## Validate runtime behavior

1. Confirm the provider initializes without startup errors.
2. Confirm Adaptive Performance values update over time:
   - Performance mode
   - Warning level
   - Temperature level
3. Toggle the simulator low power control and verify performance mode transitions to battery behavior.
4. If logging is enabled, verify logs with the prefix **[AP Windows]**.

## Validate in editor with Device Simulator

1. Enter Play mode with Device Simulator available.
2. Open the **Adaptive Performance Windows** simulator panel.
3. Use controls to test signal paths:
   - Low Power Mode
   - Target Frame Rate
   - Performance Mode
   - Warning Level
   - Temperature Level

## Common build issues

1. Provider not active at runtime:
   - Verify **Windows Provider** is enabled in Adaptive Performance settings.
2. No thermal or low-power updates:
   - Test on a Windows build rather than editor-only simulation.
3. Build succeeds but no provider data:
   - Check deployment target and provider settings selection for the active build profile.

For troubleshooting details, see [Troubleshooting](troubleshooting.md).
