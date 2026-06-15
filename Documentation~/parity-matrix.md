---
uid: ap-windows-parity-matrix
---

# Windows feature parity matrix

This page describes feature parity between the GiantArmy Windows provider and the baseline Adaptive Performance feature set.

## Full parity

| Feature | Status | Notes |
|:--|:--|:--|
| Provider registration and lifecycle | Full | Windows provider is discoverable and follows standard Adaptive Performance lifecycle flow. |
| Performance mode reporting | Full | Provider reports current mode based on low-power state and runtime heuristics. |
| Windows provider settings integration | Full | Provider settings are available in Project Settings and build preprocessing. |

## Partial parity

| Feature | Status | Notes |
|:--|:--|:--|
| Thermal warning updates | Partial | Best-effort WMI thermal zone queries are used when available, with try/catch fallback to no thermal data. |
| Temperature level updates | Partial | Normalized temperature is derived from best-effort WMI thermal zone readings when available. |
| Device Simulator controls | Partial | Editor simulation is currently reflection-backed to maximize Unity-version tolerance; behavior should be validated against target editor versions. |
| Runtime thermal mapping calibration | Partial | Thermal-to-normalized mapping is implemented and should be tuned with on-device traces for production calibration. |

## Not available

| Feature | Status | Reason |
|:--|:--|:--|
| Scheduler hint APIs | Not available | Windows doesn’t expose third-party scheduler hint controls. |
| Game Mode / Game State controls | Not available | Windows doesn’t provide app-managed Game Mode/Game State provider controls. |
| Discrete CPU level control | Not available | Windows doesn’t expose direct CPU performance-level APIs compatible with Adaptive Performance level setters. |
| Discrete GPU level control | Not available | Windows doesn’t expose direct GPU performance-level APIs compatible with Adaptive Performance level setters. |
| CPU boost / GPU boost toggles | Not available | Boost semantics are platform-managed and not available as explicit app-level toggles on Windows. |

## Notes

For details on setup and troubleshooting, see [Getting started](getting-started.md), [Requirements and compatibility](requirements.md), and [Troubleshooting](troubleshooting.md).