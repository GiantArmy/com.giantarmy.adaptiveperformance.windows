#include "ThermalCommon.h"

#pragma comment(lib, "wbemuuid.lib")

// ============================================================
// Thread-local state definitions (shared across all modules)
// ============================================================

thread_local int t_LastDiagnosticCode = GAWT_DiagnosticCode_None;
thread_local int t_LastHResult = 0;
thread_local char t_LastDiagnosticMessage[256] = "ok";
thread_local char t_LastSourceName[64] = "none";
thread_local char t_LastAttemptTrace[768] = "";
thread_local int t_CachedThermalSourceId = -1;
thread_local bool t_ProbeInitialized = false;

thread_local bool t_WmiComReady = false;
thread_local IWbemServices* t_CachedWmiRootWmi = nullptr;
thread_local IWbemServices* t_CachedWmiRootCimv2 = nullptr;

thread_local float t_LastGpuLoad = 0.0f;
thread_local float t_LastGpuClockCurrentMhz = 0.0f;
thread_local float t_LastGpuClockMaxMhz = 0.0f;
thread_local float t_LastGpuPowerWatts = 0.0f;
thread_local char t_LastGpuMetricsSource[64] = "none";
thread_local bool t_GpuProbeInitialized = false;
thread_local bool t_GpuProbeSucceeded = false;

// ============================================================
// Helper function implementations
// ============================================================

void SetDiagnostic(int code, HRESULT hr, const char* message)
{
    t_LastDiagnosticCode = code;
    t_LastHResult = static_cast<int>(hr);

    if (message == nullptr)
    {
        std::snprintf(t_LastDiagnosticMessage, sizeof(t_LastDiagnosticMessage), "(null)");
        return;
    }

    std::snprintf(t_LastDiagnosticMessage, sizeof(t_LastDiagnosticMessage), "%s", message);
}

void SetSourceName(const char* sourceName)
{
    if (sourceName == nullptr)
    {
        std::snprintf(t_LastSourceName, sizeof(t_LastSourceName), "none");
        return;
    }

    std::snprintf(t_LastSourceName, sizeof(t_LastSourceName), "%s", sourceName);
}

void ResetAttemptTrace()
{
    t_LastAttemptTrace[0] = '\0';
}

void AppendAttemptTrace(const char* source, bool success)
{
    const char* sourceName = (source == nullptr || source[0] == '\0') ? "unknown" : source;
    char entry[128] = {};

    if (success)
        std::snprintf(entry, sizeof(entry), "%s=ok", sourceName);
    else
        std::snprintf(entry, sizeof(entry), "%s=fail", sourceName);

    const size_t currentLen = std::strlen(t_LastAttemptTrace);
    const size_t maxLen = sizeof(t_LastAttemptTrace) - 1;

    if (currentLen >= maxLen)
        return;

    if (currentLen > 0)
        std::snprintf(t_LastAttemptTrace + currentLen, maxLen - currentLen + 1, " | %s", entry);
    else
        std::snprintf(t_LastAttemptTrace, sizeof(t_LastAttemptTrace), "%s", entry);
}

float ConvertRawToCelsius(float raw)
{
    if (raw > 2000.0f)
        return (raw / 10.0f) - 273.15f;

    if (raw > 200.0f)
        return raw - 273.15f;

    return raw;
}

// ============================================================
// WMI helper implementations
// ============================================================

IWbemServices* GetCachedWmiServices(const wchar_t* wmiNamespace)
{
    IWbemServices*& cached = (wmiNamespace == kWmiFallbackNamespace)
        ? t_CachedWmiRootCimv2
        : t_CachedWmiRootWmi;

    if (cached != nullptr)
        return cached;

    if (!t_WmiComReady)
    {
        HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE)
        {
            SetDiagnostic(GAWT_DiagnosticCode_ComInitFailed, comHr, "COM initialization failed");
            return nullptr;
        }

        HRESULT secHr = CoInitializeSecurity(
            nullptr, -1, nullptr, nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr, EOAC_NONE, nullptr);

        if (secHr != RPC_E_TOO_LATE && FAILED(secHr))
        {
            SetDiagnostic(GAWT_DiagnosticCode_ComInitFailed, secHr, "COM security initialization failed");
            return nullptr;
        }

        t_WmiComReady = true;
    }

    IWbemLocator* locator = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, reinterpret_cast<void**>(&locator));

    if (FAILED(hr) || locator == nullptr)
    {
        SetDiagnostic(GAWT_DiagnosticCode_WbemLocatorCreateFailed, hr, "Failed to create IWbemLocator");
        return nullptr;
    }

    IWbemServices* services = nullptr;
    BSTR ns = SysAllocString(wmiNamespace);
    hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
    SysFreeString(ns);
    locator->Release();

    if (FAILED(hr) || services == nullptr)
    {
        SetDiagnostic(GAWT_DiagnosticCode_WmiConnectFailed, hr, "Failed to connect to WMI namespace");
        return nullptr;
    }

    hr = CoSetProxyBlanket(
        services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE);

    if (FAILED(hr))
    {
        services->Release();
        SetDiagnostic(GAWT_DiagnosticCode_ProxyBlanketFailed, hr, "Failed to set WMI proxy blanket");
        return nullptr;
    }

    cached = services;
    return cached;
}

void InvalidateWmiCache(const wchar_t* wmiNamespace)
{
    IWbemServices*& cached = (wmiNamespace == kWmiFallbackNamespace)
        ? t_CachedWmiRootCimv2
        : t_CachedWmiRootWmi;

    if (cached != nullptr)
    {
        cached->Release();
        cached = nullptr;
    }
}

bool TryReadMaxThermalFromWmi(
    const wchar_t* wmiNamespace,
    const wchar_t* queryText,
    const wchar_t* propertyName,
    int unitMode,
    float& outCelsius,
    const char* noValueMessage,
    const char* successSourceName)
{
    IWbemServices* services = GetCachedWmiServices(wmiNamespace);
    if (services == nullptr)
        return false;

    IEnumWbemClassObject* enumerator = nullptr;
    BSTR queryLang = SysAllocString(kWmiQueryLanguage);
    BSTR query = SysAllocString(queryText);

    HRESULT hr = services->ExecQuery(
        queryLang, query,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &enumerator);

    SysFreeString(queryLang);
    SysFreeString(query);

    if (FAILED(hr) || enumerator == nullptr)
    {
        InvalidateWmiCache(wmiNamespace);
        SetDiagnostic(GAWT_DiagnosticCode_QueryFailed, hr, "WMI ExecQuery failed");
        return false;
    }

    bool found = false;
    float hottestCelsius = -273.15f;

    while (true)
    {
        IWbemClassObject* object = nullptr;
        ULONG returned = 0;
        hr = enumerator->Next(kWmiEnumTimeoutMs, 1, &object, &returned);

        if (hr == WBEM_S_TIMEDOUT)
        {
            SetDiagnostic(GAWT_DiagnosticCode_EnumeratorReadFailed, hr, "WMI enumerator timed out");
            break;
        }

        if (FAILED(hr))
        {
            SetDiagnostic(GAWT_DiagnosticCode_EnumeratorReadFailed, hr, "WMI enumerator read failed");
            break;
        }

        if (returned == 0 || object == nullptr)
            break;

        VARIANT value;
        VariantInit(&value);
        HRESULT valueHr = object->Get(propertyName, 0, &value, nullptr, nullptr);

        if (SUCCEEDED(valueHr) && value.vt != VT_NULL && value.vt != VT_EMPTY)
        {
            float raw = 0.0f;
            bool convertible = false;

            switch (value.vt)
            {
            case VT_I4:
                raw = static_cast<float>(value.lVal);
                convertible = true;
                break;
            case VT_UI4:
                raw = static_cast<float>(value.ulVal);
                convertible = true;
                break;
            case VT_R4:
                raw = value.fltVal;
                convertible = true;
                break;
            case VT_R8:
                raw = static_cast<float>(value.dblVal);
                convertible = true;
                break;
            default:
                break;
            }

            if (convertible && raw > 0.0f)
            {
                float celsius = raw;
                if (unitMode == TemperatureUnitModeTenthsKelvin)
                    celsius = (raw / 10.0f) - 273.15f;
                else if (unitMode == TemperatureUnitModeKelvin)
                    celsius = raw - 273.15f;

                if (!found || celsius > hottestCelsius)
                {
                    hottestCelsius = celsius;
                    found = true;
                }
            }
        }

        VariantClear(&value);
        object->Release();
    }

    enumerator->Release();

    if (!found)
    {
        SetDiagnostic(GAWT_DiagnosticCode_NoUsableTemperature, S_FALSE, noValueMessage);
        return false;
    }

    // Reject static ACPI dummy values (skip for Intel DPTF queries)
    if (successSourceName == nullptr || std::strncmp(successSourceName, "intel", 5) != 0)
    {
        float kelvinApprox = hottestCelsius + 273.15f;
        int kelvinInt = static_cast<int>(kelvinApprox + 0.5f);
        float fracPart = kelvinApprox - static_cast<float>(static_cast<int>(kelvinApprox));
        if ((fracPart < 0.1f || fracPart > 0.9f) && kelvinInt >= 295 && kelvinInt <= 310)
        {
            char msg[256];
            std::snprintf(msg, sizeof(msg),
                "WMI reports static dummy value (%dK / %.2fC) - rejecting",
                kelvinInt, hottestCelsius);
            SetDiagnostic(GAWT_DiagnosticCode_NoUsableTemperature, S_FALSE, msg);
            return false;
        }
    }

    outCelsius = hottestCelsius;
    SetDiagnostic(GAWT_DiagnosticCode_None, S_OK, "ok");
    if (successSourceName != nullptr)
        SetSourceName(successSourceName);
    else if (wmiNamespace == kWmiFallbackNamespace)
        SetSourceName("wmi-cimv2-perf-counter");
    else
        SetSourceName("wmi-acpi-thermal-zone");
    return true;
}

// ============================================================
// Thermal probe chain orchestrator
// ============================================================

namespace
{
    enum ThermalSourceId
    {
        SOURCE_NONE = -1,
        SOURCE_NVAPI = 0,
        SOURCE_ADL = 1,
        SOURCE_INTEL = 2,
        SOURCE_IGCL = 3,
        SOURCE_PDH = 4,
        SOURCE_WMI_CIMV2 = 5,
        SOURCE_WMI_ACPI = 6,
        SOURCE_POWRPROF = 7
    };

    bool TryReadThermalCelsiusInternal(float& outCelsius)
    {
        if (!t_ProbeInitialized)
        {
            ResetAttemptTrace();
        }

        bool hasFirstFailure = false;
        int firstFailureCode = GAWT_DiagnosticCode_None;
        int firstFailureHr = 0;
        char firstFailureMessage[256] = "";

        if (t_ProbeInitialized && t_CachedThermalSourceId == SOURCE_NVAPI)
        {
            if (TryReadThermalFromNvApi(outCelsius))
                return true;
            t_CachedThermalSourceId = SOURCE_NONE;
        }

        if (!t_ProbeInitialized)
        {
            if (TryReadThermalFromNvApi(outCelsius))
            {
                AppendAttemptTrace("nvapi", true);
                t_ProbeInitialized = true;
                t_CachedThermalSourceId = SOURCE_NVAPI;
                return true;
            }
            AppendAttemptTrace("nvapi", false);
        }
        else if (t_CachedThermalSourceId == SOURCE_NONE)
        {
            return false;
        }

        if (!hasFirstFailure &&
            t_LastDiagnosticCode != GAWT_DiagnosticCode_None &&
            t_LastDiagnosticCode != GAWT_DiagnosticCode_NvapiUnavailable)
        {
            hasFirstFailure = true;
            firstFailureCode = t_LastDiagnosticCode;
            firstFailureHr = t_LastHResult;
            std::snprintf(firstFailureMessage, sizeof(firstFailureMessage), "%s", t_LastDiagnosticMessage);
        }

        if (!t_ProbeInitialized && t_CachedThermalSourceId < SOURCE_ADL)
        {
            if (TryReadThermalFromAdl(outCelsius))
            {
                AppendAttemptTrace("adl", true);
                t_ProbeInitialized = true;
                t_CachedThermalSourceId = SOURCE_ADL;
                return true;
            }
            AppendAttemptTrace("adl", false);
        }

        if (t_ProbeInitialized && t_CachedThermalSourceId == SOURCE_ADL)
        {
            if (TryReadThermalFromAdl(outCelsius))
                return true;
            t_CachedThermalSourceId = SOURCE_NONE;
            return false;
        }
        if (!hasFirstFailure && t_LastDiagnosticCode != GAWT_DiagnosticCode_None)
        {
            hasFirstFailure = true;
            firstFailureCode = t_LastDiagnosticCode;
            firstFailureHr = t_LastHResult;
            std::snprintf(firstFailureMessage, sizeof(firstFailureMessage), "%s", t_LastDiagnosticMessage);
        }

        if (!t_ProbeInitialized && t_CachedThermalSourceId < SOURCE_INTEL)
        {
            if (TryReadThermalFromIntelDptf(outCelsius))
            {
                AppendAttemptTrace("intel", true);
                t_ProbeInitialized = true;
                t_CachedThermalSourceId = SOURCE_INTEL;
                return true;
            }
            AppendAttemptTrace("intel", false);
        }

        if (t_ProbeInitialized && t_CachedThermalSourceId == SOURCE_INTEL)
        {
            if (TryReadThermalFromIntelDptf(outCelsius))
                return true;
            t_CachedThermalSourceId = SOURCE_NONE;
            return false;
        }
        if (!hasFirstFailure && t_LastDiagnosticCode != GAWT_DiagnosticCode_None)
        {
            hasFirstFailure = true;
            firstFailureCode = t_LastDiagnosticCode;
            firstFailureHr = t_LastHResult;
            std::snprintf(firstFailureMessage, sizeof(firstFailureMessage), "%s", t_LastDiagnosticMessage);
        }

        // IGCL Temperature (Intel GPU die sensor)
        if (!t_ProbeInitialized && t_CachedThermalSourceId < SOURCE_IGCL)
        {
            if (TryReadThermalFromIgcl(outCelsius))
            {
                AppendAttemptTrace("igcl", true);
                t_ProbeInitialized = true;
                t_CachedThermalSourceId = SOURCE_IGCL;
                SetSourceName("igcl-gpu-temperature");
                return true;
            }
            AppendAttemptTrace("igcl", false);
        }

        if (t_ProbeInitialized && t_CachedThermalSourceId == SOURCE_IGCL)
        {
            if (TryReadThermalFromIgcl(outCelsius))
                return true;
            t_CachedThermalSourceId = SOURCE_NONE;
            return false;
        }
        if (!hasFirstFailure && t_LastDiagnosticCode != GAWT_DiagnosticCode_None)
        {
            hasFirstFailure = true;
            firstFailureCode = t_LastDiagnosticCode;
            firstFailureHr = t_LastHResult;
            std::snprintf(firstFailureMessage, sizeof(firstFailureMessage), "%s", t_LastDiagnosticMessage);
        }

        // PDH Thermal Zone
        if (!t_ProbeInitialized && t_CachedThermalSourceId < SOURCE_PDH)
        {
            if (TryReadThermalFromPdhCounter(outCelsius))
            {
                AppendAttemptTrace("pdh", true);
                t_ProbeInitialized = true;
                t_CachedThermalSourceId = SOURCE_PDH;
                return true;
            }
            AppendAttemptTrace("pdh", false);
        }

        if (t_ProbeInitialized && t_CachedThermalSourceId == SOURCE_PDH)
        {
            if (TryReadThermalFromPdhCounter(outCelsius))
                return true;
            t_CachedThermalSourceId = SOURCE_NONE;
            return false;
        }
        if (!hasFirstFailure && t_LastDiagnosticCode != GAWT_DiagnosticCode_None)
        {
            hasFirstFailure = true;
            firstFailureCode = t_LastDiagnosticCode;
            firstFailureHr = t_LastHResult;
            std::snprintf(firstFailureMessage, sizeof(firstFailureMessage), "%s", t_LastDiagnosticMessage);
        }

        // WMI CIMV2
        if (!t_ProbeInitialized && t_CachedThermalSourceId < SOURCE_WMI_CIMV2)
        {
            if (TryReadMaxThermalFromWmi(
                kWmiFallbackNamespace,
                kWmiFallbackQuery,
                kWmiFallbackPropertyTemperature,
                TemperatureUnitModeKelvin,
                outCelsius,
                "No usable Temperature values in ROOT\\CIMV2 perf counter results"))
            {
                AppendAttemptTrace("wmi-cimv2", true);
                t_ProbeInitialized = true;
                t_CachedThermalSourceId = SOURCE_WMI_CIMV2;
                return true;
            }
            AppendAttemptTrace("wmi-cimv2", false);
        }

        if (t_ProbeInitialized && t_CachedThermalSourceId == SOURCE_WMI_CIMV2)
        {
            if (TryReadMaxThermalFromWmi(
                kWmiFallbackNamespace,
                kWmiFallbackQuery,
                kWmiFallbackPropertyTemperature,
                TemperatureUnitModeKelvin,
                outCelsius,
                "No usable Temperature values in ROOT\\CIMV2 perf counter results"))
                return true;
            t_CachedThermalSourceId = SOURCE_NONE;
            return false;
        }
        if (!hasFirstFailure && t_LastDiagnosticCode != GAWT_DiagnosticCode_None)
        {
            hasFirstFailure = true;
            firstFailureCode = t_LastDiagnosticCode;
            firstFailureHr = t_LastHResult;
            std::snprintf(firstFailureMessage, sizeof(firstFailureMessage), "%s", t_LastDiagnosticMessage);
        }

        // WMI ACPI
        if (!t_ProbeInitialized && t_CachedThermalSourceId < SOURCE_WMI_ACPI)
        {
            if (TryReadMaxThermalFromWmi(
                kWmiNamespace,
                kWmiQuery,
                kWmiPropertyCurrentTemperature,
                TemperatureUnitModeTenthsKelvin,
                outCelsius,
                "No usable CurrentTemperature values in ROOT\\WMI results"))
            {
                AppendAttemptTrace("wmi-acpi", true);
                t_ProbeInitialized = true;
                t_CachedThermalSourceId = SOURCE_WMI_ACPI;
                return true;
            }
            AppendAttemptTrace("wmi-acpi", false);
        }

        if (t_ProbeInitialized && t_CachedThermalSourceId == SOURCE_WMI_ACPI)
        {
            if (TryReadMaxThermalFromWmi(
                kWmiNamespace,
                kWmiQuery,
                kWmiPropertyCurrentTemperature,
                TemperatureUnitModeTenthsKelvin,
                outCelsius,
                "No usable CurrentTemperature values in ROOT\\WMI results"))
                return true;
            t_CachedThermalSourceId = SOURCE_NONE;
            return false;
        }
        if (!hasFirstFailure && t_LastDiagnosticCode != GAWT_DiagnosticCode_None)
        {
            hasFirstFailure = true;
            firstFailureCode = t_LastDiagnosticCode;
            firstFailureHr = t_LastHResult;
            std::snprintf(firstFailureMessage, sizeof(firstFailureMessage), "%s", t_LastDiagnosticMessage);
        }

        // PowrProf
        if (!t_ProbeInitialized && t_CachedThermalSourceId < SOURCE_POWRPROF)
        {
            if (TryReadThermalFromPowerInformation(outCelsius))
            {
                AppendAttemptTrace("powrprof", true);
                t_ProbeInitialized = true;
                t_CachedThermalSourceId = SOURCE_POWRPROF;
                return true;
            }
            AppendAttemptTrace("powrprof", false);
        }

        if (t_ProbeInitialized && t_CachedThermalSourceId == SOURCE_POWRPROF)
        {
            if (TryReadThermalFromPowerInformation(outCelsius))
                return true;
            t_CachedThermalSourceId = SOURCE_NONE;
            return false;
        }

        if (!hasFirstFailure && t_LastDiagnosticCode != GAWT_DiagnosticCode_None)
        {
            hasFirstFailure = true;
            firstFailureCode = t_LastDiagnosticCode;
            firstFailureHr = t_LastHResult;
            std::snprintf(firstFailureMessage, sizeof(firstFailureMessage), "%s", t_LastDiagnosticMessage);
        }

        if (hasFirstFailure)
        {
            SetDiagnostic(firstFailureCode, static_cast<HRESULT>(firstFailureHr), firstFailureMessage);
        }
        else if (t_LastDiagnosticCode == GAWT_DiagnosticCode_None)
            SetDiagnostic(GAWT_DiagnosticCode_NoUsableTemperature, S_FALSE, "No usable temperature source found");

        SetSourceName("none");

        t_ProbeInitialized = true;
        return false;
    }

    // ============================================================
    // GPU metrics probe/refresh
    // ============================================================

    void ProbeGpuMetrics()
    {
        t_GpuProbeInitialized = true;
        t_GpuProbeSucceeded = false;

        if (ReadGpuMetricsFromNvml())
        {
            t_GpuProbeSucceeded = true;
            std::snprintf(t_LastGpuMetricsSource, sizeof(t_LastGpuMetricsSource), "nvml-gpu");
            return;
        }

        if (ReadGpuMetricsFromAdl())
        {
            t_GpuProbeSucceeded = true;
            std::snprintf(t_LastGpuMetricsSource, sizeof(t_LastGpuMetricsSource), "adl-gpu");
            return;
        }

        if (ReadGpuMetricsFromIgcl())
        {
            t_GpuProbeSucceeded = true;
            std::snprintf(t_LastGpuMetricsSource, sizeof(t_LastGpuMetricsSource), "igcl-gpu");
            return;
        }

        if (ReadGpuMetricsFromPdhGpuEngine())
        {
            t_GpuProbeSucceeded = true;
            std::snprintf(t_LastGpuMetricsSource, sizeof(t_LastGpuMetricsSource), "pdh-gpu-engine");
            return;
        }

        std::snprintf(t_LastGpuMetricsSource, sizeof(t_LastGpuMetricsSource), "unavailable");
    }

    void RefreshGpuMetrics()
    {
        if (!t_GpuProbeSucceeded)
            return;

        if (std::strncmp(t_LastGpuMetricsSource, "nvml", 4) == 0)
            ReadGpuMetricsFromNvml();
        else if (std::strncmp(t_LastGpuMetricsSource, "adl", 3) == 0)
            ReadGpuMetricsFromAdl();
        else if (std::strncmp(t_LastGpuMetricsSource, "igcl", 4) == 0)
            ReadGpuMetricsFromIgcl();
        else if (std::strncmp(t_LastGpuMetricsSource, "pdh", 3) == 0)
            ReadGpuMetricsFromPdhGpuEngine();
    }
}

// ============================================================
// Exported C API
// ============================================================

extern "C"
{
    GAWT_API int GAWT_IsThermalApiAvailable()
    {
        float ignored = 0.0f;
        return TryReadThermalCelsiusInternal(ignored) ? 1 : 0;
    }

    GAWT_API int GAWT_TryGetThermalCelsius(float* celsius)
    {
        if (celsius == nullptr)
        {
            SetDiagnostic(GAWT_DiagnosticCode_NullOutParameter, E_POINTER, "Output pointer is null");
            return 0;
        }

        float value = 0.0f;
        if (!TryReadThermalCelsiusInternal(value))
            return 0;

        *celsius = value;
        return 1;
    }

    GAWT_API int GAWT_GetLastDiagnosticCode()
    {
        return t_LastDiagnosticCode;
    }

    GAWT_API int GAWT_GetLastHResult()
    {
        return t_LastHResult;
    }

    GAWT_API const char* GAWT_GetLastDiagnosticMessage()
    {
        return t_LastDiagnosticMessage;
    }

    GAWT_API const char* GAWT_GetLastSourceName()
    {
        return t_LastSourceName;
    }

    GAWT_API const char* GAWT_GetLastAttemptTrace()
    {
        return t_LastAttemptTrace;
    }

    GAWT_API int GAWT_TryGetGpuLoad(float* outLoadPercent)
    {
        if (outLoadPercent == nullptr)
            return 0;

        if (!t_GpuProbeInitialized)
            ProbeGpuMetrics();
        else
            RefreshGpuMetrics();

        if (!t_GpuProbeSucceeded)
            return 0;

        *outLoadPercent = t_LastGpuLoad;
        return 1;
    }

    GAWT_API int GAWT_TryGetGpuClocks(float* outCurrentMhz, float* outMaxMhz)
    {
        if (outCurrentMhz == nullptr || outMaxMhz == nullptr)
            return 0;

        if (!t_GpuProbeInitialized)
            ProbeGpuMetrics();
        else
            RefreshGpuMetrics();

        if (!t_GpuProbeSucceeded)
            return 0;

        *outCurrentMhz = t_LastGpuClockCurrentMhz;
        *outMaxMhz = t_LastGpuClockMaxMhz;
        return 1;
    }

    GAWT_API int GAWT_TryGetGpuPowerUsage(float* outPowerWatts)
    {
        if (outPowerWatts == nullptr)
            return 0;

        if (!t_GpuProbeInitialized)
            ProbeGpuMetrics();
        else
            RefreshGpuMetrics();

        if (!t_GpuProbeSucceeded)
            return 0;

        *outPowerWatts = t_LastGpuPowerWatts;
        return 1;
    }

    GAWT_API const char* GAWT_GetLastGpuMetricsSource()
    {
        if (!t_GpuProbeInitialized)
            ProbeGpuMetrics();
        return t_LastGpuMetricsSource;
    }
}
