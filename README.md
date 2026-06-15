# GiantArmy Adaptive Performance Windows

The GiantArmy Adaptive Performance Windows package (`com.giantarmy.adaptiveperformance.windows`) provides the Adaptive Performance subsystem for Windows platforms. It enables real-time thermal monitoring and GPU metrics (load, clocks, power) via a native C++ bridge DLL that probes vendor-specific APIs at runtime.

## Supported GPU Vendors

| Vendor | Thermal API | GPU Metrics API |
|--------|-------------|-----------------|
| NVIDIA | NVAPI | NVML |
| AMD | ADL / ADL2 | ADL2 PMLog |
| Intel | IGCL Power Telemetry / Discrete Sensors / DPTF ACPI | IGCL (frequency + engine activity) |
| Any (fallback) | PDH Thermal Zone, WMI ACPI, PowrProf | PDH GPU Engine (WDDM) |

## Project Structure

```
Runtime/
  Plugins/Windows/NativeBridge~/  ← Native C++ DLL source (~ hides from Unity)
    ThermalCommon.h                  Shared state, types, module declarations
    ThermalNvidia.cpp                NVIDIA thermal + GPU metrics
    ThermalAmd.cpp                   AMD thermal + GPU metrics
    ThermalIntel.cpp                 Intel thermal + GPU metrics
    ThermalFallbacks.cpp             PDH, WMI, PowrProf thermal + PDH GPU Engine
    GiantArmyWindowsThermalBridge.cpp  Orchestrator, helpers, C API exports
    include/GiantArmyWindowsThermalBridge.h  Public C API header
    tools/ThermalBridgeCli.cpp       CLI test tool
    CMakeLists.txt                   Build definition
  Provider/                        ← Unity C# subsystem provider
Editor/                            ← Editor tooling (settings UI, device simulator)
Tests/                             ← Runtime tests
```

## Requirements

- **Unity**: 6000.0+
- **Dependency**: `com.unity.modules.adaptiveperformance` 1.0.0+
- **Native build**: Visual Studio 2022, CMake 3.21+, Windows SDK 10.0.26100+

## Building the Native DLL

1. **Generate the build system** (from the NativeBridge~ directory):

   ```powershell
   cd Runtime/Plugins/Windows/NativeBridge~
   mkdir build -ErrorAction SilentlyContinue
   cd build
   & "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" .. -G "Visual Studio 17 2022" -A x64
   ```

2. **Build Release**:

   ```powershell
   & "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.com" GiantArmyWindowsThermalBridge.sln /Build "Release"
   ```

3. **Test**:

   ```powershell
   cd Release
   .\ThermalBridgeCli.exe
   ```

   Expected output shows thermal source, temperature reading, and GPU metrics.

4. **Deploy**: Copy `build/Release/GiantArmyWindowsThermalBridge.dll` to `Runtime/Plugins/Windows/x86_64/`.

## Installation (Unity)

Install via Unity Package Manager using the git URL or local path. The Adaptive Performance dependency will be resolved automatically. Once installed, the provider initializes on Windows builds without additional configuration.

## Architecture

The native bridge uses a **probe chain** pattern — on first call it tries each vendor API in priority order (NVIDIA → AMD → Intel → fallbacks) and caches the first successful source for subsequent frames. Thermal and GPU metrics probes are independent; a system can report GPU metrics even if thermal is unavailable.

All per-thread state uses `thread_local` storage for safe multi-threaded access from Unity's job system.
