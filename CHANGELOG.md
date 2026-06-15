# Changelog
All notable changes to this package are documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](http://semver.org/spec/v2.0.0.html).

## [6.0.0] - 2026-06-11

### Added
* GPU metrics support: load %, current/max clock (MHz), and power draw (W) via new native APIs.
  - NVIDIA: NVML (load, clocks, power)
  - AMD: ADL2 PMLog, Overdrive5 CurrentActivity, Overdrive6 CurrentStatus (load, clocks, power)
  - Intel: IGCL ControlLib (frequency domains, engine activity)
  - Fallback: PDH GPU Engine counters (load only, any WDDM GPU)
* Intel IGCL power telemetry thermal path (ctlPowerTelemetryGet).
* Intel IGCL discrete temperature sensor fallback (ctlEnumTemperatureSensors).
* Intel DPTF ACPI WMI thermal path for Intel-specific thermal zones.
* PDH Thermal Zone counter path with static/dummy value rejection (295-310K sentinel filter).
* WMI CIMV2 performance counter thermal fallback.
* CallNtPowerInformation (PowrProf) system thermal fallback.
* Cached WMI connections to avoid full COM round-trip every frame.
* Thread-local state pattern for safe multi-threaded access from Unity's job system.
* ThermalBridgeCli test tool for standalone validation.

### Changed
* Native bridge split into modular source files for maintainability:
  - `ThermalCommon.h` — shared state, types, and module declarations
  - `ThermalNvidia.cpp` — NVIDIA thermal and GPU metrics
  - `ThermalAmd.cpp` — AMD thermal and GPU metrics
  - `ThermalIntel.cpp` — Intel thermal and GPU metrics
  - `ThermalFallbacks.cpp` — PDH, WMI, PowrProf thermal and PDH GPU Engine
  - `GiantArmyWindowsThermalBridge.cpp` — orchestrator, helpers, C API exports
* AMD GPU metrics now properly filters to AMD adapters by vendor ID and deduplicates by bus number.
* AMD GPU metrics cascades through PMLog → OD5 → OD6 → ADL1 legacy fallback.
* Thermal probe chain rejects static ACPI dummy values in 295-310K range.

### Requirements
* Unity 6000.0+
* Windows 10 1709+ (for PDH GPU Engine counters)
* Visual Studio 2022 + CMake 3.21+ (native build)
