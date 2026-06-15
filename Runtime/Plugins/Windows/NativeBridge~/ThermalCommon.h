#pragma once

#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "include/GiantArmyWindowsThermalBridge.h"

// ============================================================
// Shared constants
// ============================================================

constexpr wchar_t kWmiNamespace[] = L"ROOT\\WMI";
constexpr wchar_t kWmiQueryLanguage[] = L"WQL";
constexpr wchar_t kWmiQuery[] = L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature";
constexpr wchar_t kWmiIntelDptfQuery[] = L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature WHERE InstanceName LIKE '%INT340%' OR InstanceName LIKE '%INTC%' OR InstanceName LIKE '%Intel%'";
constexpr wchar_t kWmiPropertyCurrentTemperature[] = L"CurrentTemperature";
constexpr wchar_t kWmiFallbackNamespace[] = L"ROOT\\CIMV2";
constexpr wchar_t kWmiFallbackQuery[] = L"SELECT Temperature FROM Win32_PerfFormattedData_Counters_ThermalZoneInformation";
constexpr wchar_t kWmiFallbackPropertyTemperature[] = L"Temperature";
constexpr wchar_t kPdhThermalCounterPath[] = L"\\Thermal Zone Information(*)\\Temperature";
constexpr wchar_t kPdhGpuEngineCounterPath[] = L"\\GPU Engine(*)\\Utilization Percentage";
constexpr LONG kWmiEnumTimeoutMs = 500;

enum TemperatureUnitMode
{
    TemperatureUnitModeCelsius = 0,
    TemperatureUnitModeKelvin = 1,
    TemperatureUnitModeTenthsKelvin = 2
};

// ============================================================
// Shared thread-local state (defined in GiantArmyWindowsThermalBridge.cpp)
// ============================================================

extern thread_local int t_LastDiagnosticCode;
extern thread_local int t_LastHResult;
extern thread_local char t_LastDiagnosticMessage[256];
extern thread_local char t_LastSourceName[64];
extern thread_local char t_LastAttemptTrace[768];
extern thread_local int t_CachedThermalSourceId;
extern thread_local bool t_ProbeInitialized;

// WMI connections
extern thread_local bool t_WmiComReady;
extern thread_local IWbemServices* t_CachedWmiRootWmi;
extern thread_local IWbemServices* t_CachedWmiRootCimv2;

// GPU metrics state
extern thread_local float t_LastGpuLoad;
extern thread_local float t_LastGpuClockCurrentMhz;
extern thread_local float t_LastGpuClockMaxMhz;
extern thread_local float t_LastGpuPowerWatts;
extern thread_local char t_LastGpuMetricsSource[64];
extern thread_local bool t_GpuProbeInitialized;
extern thread_local bool t_GpuProbeSucceeded;



// ============================================================
// Shared helper functions (defined in GiantArmyWindowsThermalBridge.cpp)
// ============================================================

void SetDiagnostic(int code, HRESULT hr, const char* message);
void SetSourceName(const char* sourceName);
void ResetAttemptTrace();
void AppendAttemptTrace(const char* source, bool success);
float ConvertRawToCelsius(float raw);

// WMI helpers
IWbemServices* GetCachedWmiServices(const wchar_t* wmiNamespace);
void InvalidateWmiCache(const wchar_t* wmiNamespace);

bool TryReadMaxThermalFromWmi(
    const wchar_t* wmiNamespace,
    const wchar_t* queryText,
    const wchar_t* propertyName,
    int unitMode,
    float& outCelsius,
    const char* noValueMessage,
    const char* successSourceName = nullptr);

// ============================================================
// Module function declarations
// ============================================================

// Nvidia (ThermalNvidia.cpp)
bool TryReadThermalFromNvApi(float& outCelsius);
bool ReadGpuMetricsFromNvml();

// AMD (ThermalAmd.cpp)
bool TryReadThermalFromAdl(float& outCelsius);
bool ReadGpuMetricsFromAdl();

// Intel (ThermalIntel.cpp)
bool TryReadThermalFromIntelDptf(float& outCelsius);
bool TryReadThermalFromIgcl(float& outCelsius);
bool ReadGpuMetricsFromIgcl();

// Fallbacks (PDH) (ThermalFallbacks.cpp)
bool TryReadThermalFromPdhCounter(float& outCelsius);
bool TryReadThermalFromPowerInformation(float& outCelsius);
bool ReadGpuMetricsFromPdhGpuEngine();
