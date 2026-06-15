#pragma once

#if defined(_WIN32)
#if defined(GAWT_BRIDGE_EXPORTS)
#define GAWT_API __declspec(dllexport)
#else
#define GAWT_API __declspec(dllimport)
#endif
#else
#define GAWT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum GAWT_DiagnosticCode
{
	GAWT_DiagnosticCode_None = 0,
	GAWT_DiagnosticCode_ComInitFailed = 1,
	GAWT_DiagnosticCode_WbemLocatorCreateFailed = 2,
	GAWT_DiagnosticCode_WmiConnectFailed = 3,
	GAWT_DiagnosticCode_ProxyBlanketFailed = 4,
	GAWT_DiagnosticCode_QueryFailed = 5,
	GAWT_DiagnosticCode_EnumeratorReadFailed = 6,
	GAWT_DiagnosticCode_NoUsableTemperature = 7,
	GAWT_DiagnosticCode_NullOutParameter = 8,
	GAWT_DiagnosticCode_PowrProfUnavailable = 9,
	GAWT_DiagnosticCode_PowrProfCallFailed = 10,
	GAWT_DiagnosticCode_PdhUnavailable = 11,
	GAWT_DiagnosticCode_PdhQueryFailed = 12,
	GAWT_DiagnosticCode_NvapiUnavailable = 13,
	GAWT_DiagnosticCode_NvapiInitializeFailed = 14,
	GAWT_DiagnosticCode_NvapiNoGpus = 15,
	GAWT_DiagnosticCode_NvapiThermalQueryFailed = 16,
	GAWT_DiagnosticCode_IgclUnavailable = 17,
	GAWT_DiagnosticCode_IgclInitFailed = 18,
	GAWT_DiagnosticCode_IgclNoSensors = 19
};

// ============================================================
// Thermal API
// ============================================================

/**
 * Checks whether any thermal data source is available on this system.
 * Probes all sources (NVAPI, ADL, Intel DPTF, IGCL, PDH, WMI, PowrProf) in
 * priority order and caches the first successful source for subsequent calls.
 * @return 1 if a thermal reading was obtained, 0 if no source is available.
 */
GAWT_API int GAWT_IsThermalApiAvailable();

/**
 * Reads the current thermal temperature in degrees Celsius.
 * On first call, probes all sources and caches the best one.
 * Subsequent calls use the cached source for efficiency.
 * @param celsius  Output pointer receiving the temperature value.
 * @return 1 on success, 0 on failure (check diagnostics for details).
 */
GAWT_API int GAWT_TryGetThermalCelsius(float* celsius);

/**
 * Returns the diagnostic code from the last GAWT call on this thread.
 * @return A value from the GAWT_DiagnosticCode enum.
 */
GAWT_API int GAWT_GetLastDiagnosticCode();

/**
 * Returns the HRESULT (as int) from the last failed Windows API call.
 * Only meaningful when GAWT_GetLastDiagnosticCode() != GAWT_DiagnosticCode_None.
 * @return The HRESULT cast to int, or 0 if no error occurred.
 */
GAWT_API int GAWT_GetLastHResult();

/**
 * Returns a human-readable description of the last diagnostic event.
 * The returned pointer is thread-local and valid until the next GAWT call.
 * @return Null-terminated string (never NULL).
 */
GAWT_API const char* GAWT_GetLastDiagnosticMessage();

/**
 * Returns the name of the thermal source that provided the last reading.
 * Possible values: "nvapi", "adl", "intel-dptf", "igcl-gpu-temperature",
 * "pdh-thermal-zone", "wmi-cimv2-perf-counter", "wmi-acpi-thermal-zone",
 * "powrprof", "none".
 * @return Null-terminated string (never NULL).
 */
GAWT_API const char* GAWT_GetLastSourceName();

/**
 * Returns a trace of all thermal sources attempted during the initial probe.
 * Format: "source1=ok|fail | source2=ok|fail | ...".
 * Useful for diagnostics to understand which sources were tried and their result.
 * @return Null-terminated string (never NULL, may be empty after cache hit).
 */
GAWT_API const char* GAWT_GetLastAttemptTrace();

// ============================================================
// GPU Monitoring API
// ============================================================

/**
 * Reads the current GPU utilization as a percentage (0-100).
 * Sources probed in order: NVML, ADL, IGCL, PDH GPU Engine.
 * The first successful source is cached for subsequent calls.
 * @param outLoadPercent  Output pointer receiving GPU load (0.0 - 100.0).
 * @return 1 on success, 0 if no GPU metrics source is available.
 */
GAWT_API int GAWT_TryGetGpuLoad(float* outLoadPercent);

/**
 * Reads the current and maximum GPU clock speeds in MHz.
 * @param outCurrentMhz  Output pointer receiving current clock speed.
 * @param outMaxMhz      Output pointer receiving maximum clock speed.
 * @return 1 on success, 0 if GPU clock data is unavailable.
 */
GAWT_API int GAWT_TryGetGpuClocks(float* outCurrentMhz, float* outMaxMhz);

/**
 * Reads the current GPU power consumption in Watts.
 * @param outPowerWatts  Output pointer receiving power draw in Watts.
 * @return 1 on success, 0 if GPU power data is unavailable.
 */
GAWT_API int GAWT_TryGetGpuPowerUsage(float* outPowerWatts);

/**
 * Returns the name of the GPU metrics source currently in use.
 * Possible values: "nvml-gpu", "adl-gpu", "igcl-gpu", "pdh-gpu-engine",
 * "unavailable".
 * @return Null-terminated string (never NULL).
 */
GAWT_API const char* GAWT_GetLastGpuMetricsSource();

#ifdef __cplusplus
}
#endif
