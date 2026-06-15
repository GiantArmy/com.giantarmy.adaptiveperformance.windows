---
uid: ap-windows-requirements
---

# Requirements

This page lists the minimum requirements to use the GiantArmy Adaptive Performance Windows provider.

## Unity Editor compatibility

This package is a Unity package, and its version is tied to the Unity Editor version.

## Device support

The provider targets devices running Windows standalone builds.

## Build requirements

1. Configure your project to target Windows standalone.
2. Enable Adaptive Performance and select the Windows provider in Project Settings.
3. Build and validate on a Windows machine for runtime telemetry and performance-mode behavior.

## Runtime constraints

The Windows provider doesn’t implement scheduler hint controls, Game Mode/Game State controls, or discrete CPU/GPU boost-level APIs.

For a complete status breakdown, see [Windows feature parity matrix](parity-matrix.md). For issue diagnosis, see [Troubleshooting](troubleshooting.md).

## Platform support matrix

| **Platform** | **Minimum Version** | **Provider** |
|:------------:|:-------------------:|:------------:|
| Windows      | Standalone          | GiantArmy Windows |
