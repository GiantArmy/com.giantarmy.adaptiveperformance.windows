#include "ThermalCommon.h"

// ============================================================
// Fallback Thermal (PDH, PowrProf) + GPU Metrics (PDH GPU Engine)
// ============================================================

namespace
{
    constexpr ULONG kSystemThermalInformationLevel = 12;

    typedef LONG (WINAPI* CallNtPowerInformationFn)(ULONG, PVOID, ULONG, PVOID, ULONG);

    struct SystemThermalInformation
    {
        ULONG ThermalStamp;
        ULONG ThermalConstant1;
        ULONG ThermalConstant2;
        ULONG Processors;
        ULONG SamplingPeriod;
        ULONG CurrentTemperature;
        ULONG PassiveTripPoint;
        ULONG CriticalTripPoint;
        UCHAR ActiveTripPointCount;
        ULONG ActiveTripPoint[10];
        UCHAR S4TransitionTripPoint;
    };

    // Module-private cached PDH GPU engine state
    thread_local PDH_HQUERY t_CachedPdhGpuQuery = nullptr;
    thread_local PDH_HCOUNTER t_CachedPdhGpuCounter = nullptr;
}

bool TryReadThermalFromPowerInformation(float& outCelsius)
{
    HMODULE powrProfModule = LoadLibraryW(L"powrprof.dll");
    if (powrProfModule == nullptr)
    {
        SetDiagnostic(GAWT_DiagnosticCode_PowrProfUnavailable, HRESULT_FROM_WIN32(GetLastError()), "powrprof.dll not available");
        return false;
    }

    auto callNtPowerInformation = reinterpret_cast<CallNtPowerInformationFn>(
        GetProcAddress(powrProfModule, "CallNtPowerInformation"));

    if (callNtPowerInformation == nullptr)
    {
        FreeLibrary(powrProfModule);
        SetDiagnostic(GAWT_DiagnosticCode_PowrProfUnavailable, HRESULT_FROM_WIN32(GetLastError()), "CallNtPowerInformation export missing");
        return false;
    }

    SystemThermalInformation thermalInfo = {};
    LONG ntStatus = callNtPowerInformation(
        kSystemThermalInformationLevel,
        nullptr,
        0,
        &thermalInfo,
        static_cast<ULONG>(sizeof(thermalInfo)));

    FreeLibrary(powrProfModule);

    if (ntStatus != 0)
    {
        SetDiagnostic(GAWT_DiagnosticCode_PowrProfCallFailed, static_cast<HRESULT>(ntStatus), "CallNtPowerInformation(SystemThermalInformation) failed");
        return false;
    }

    if (thermalInfo.CurrentTemperature == 0)
    {
        SetDiagnostic(GAWT_DiagnosticCode_PowrProfCallFailed, S_FALSE, "SystemThermalInformation returned zero temperature");
        return false;
    }

    outCelsius = (static_cast<float>(thermalInfo.CurrentTemperature) / 10.0f) - 273.15f;
    SetDiagnostic(GAWT_DiagnosticCode_None, S_OK, "ok");
    SetSourceName("powrprof-system-thermal");
    return true;
}

bool TryReadThermalFromPdhCounter(float& outCelsius)
{
    PDH_HQUERY query = nullptr;
    PDH_STATUS status = PdhOpenQueryW(nullptr, 0, &query);
    if (status != ERROR_SUCCESS || query == nullptr)
    {
        SetDiagnostic(GAWT_DiagnosticCode_PdhUnavailable, static_cast<HRESULT>(status), "PdhOpenQuery failed");
        return false;
    }

    PDH_HCOUNTER counter = nullptr;
    status = PdhAddEnglishCounterW(query, kPdhThermalCounterPath, 0, &counter);
    if (status != ERROR_SUCCESS || counter == nullptr)
    {
        PdhCloseQuery(query);
        SetDiagnostic(GAWT_DiagnosticCode_PdhUnavailable, static_cast<HRESULT>(status), "PdhAddEnglishCounter for Thermal Zone Information failed");
        return false;
    }

    status = PdhCollectQueryData(query);
    if (status != ERROR_SUCCESS)
    {
        PdhCloseQuery(query);
        SetDiagnostic(GAWT_DiagnosticCode_PdhQueryFailed, static_cast<HRESULT>(status), "PdhCollectQueryData failed");
        return false;
    }

    DWORD bufferSize = 0;
    DWORD itemCount = 0;
    status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
    if (status != PDH_MORE_DATA)
    {
        PdhCloseQuery(query);
        SetDiagnostic(GAWT_DiagnosticCode_PdhQueryFailed, static_cast<HRESULT>(status), "PdhGetFormattedCounterArray size query failed");
        return false;
    }

    std::vector<unsigned char> buffer(bufferSize);
    auto items = reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(buffer.data());
    status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
    PdhCloseQuery(query);

    if (status != ERROR_SUCCESS)
    {
        SetDiagnostic(GAWT_DiagnosticCode_PdhQueryFailed, static_cast<HRESULT>(status), "PdhGetFormattedCounterArray read failed");
        return false;
    }

    bool found = false;
    float maxCelsius = -273.15f;
    bool allIdentical = true;
    double firstValue = 0.0;
    int validCount = 0;

    for (DWORD i = 0; i < itemCount; ++i)
    {
        const auto& item = items[i];
        if (item.FmtValue.CStatus != ERROR_SUCCESS)
            continue;

        const double rawValue = item.FmtValue.doubleValue;
        if (rawValue <= 0.0)
            continue;

        if (validCount == 0)
            firstValue = rawValue;
        else if (rawValue != firstValue)
            allIdentical = false;
        ++validCount;

        float celsius = ConvertRawToCelsius(static_cast<float>(rawValue));
        if (!found || celsius > maxCelsius)
        {
            maxCelsius = celsius;
            found = true;
        }
    }

    if (!found)
    {
        SetDiagnostic(GAWT_DiagnosticCode_PdhQueryFailed, S_FALSE, "PDH Thermal Zone Information returned no usable values");
        return false;
    }

    // Reject static ACPI dummy values
    if (allIdentical && validCount >= 1)
    {
        float roundedKelvin = static_cast<float>(firstValue);
        float fracPart = roundedKelvin - static_cast<float>(static_cast<int>(roundedKelvin));
        if (fracPart < 0.1f || fracPart > 0.9f)
        {
            int kelvinInt = static_cast<int>(roundedKelvin + 0.5f);
            if (kelvinInt >= 295 && kelvinInt <= 310)
            {
                char msg[256];
                std::snprintf(msg, sizeof(msg),
                    "PDH Thermal Zone reports static value (%dK / %.2fC) - rejecting",
                    kelvinInt, maxCelsius);
                SetDiagnostic(GAWT_DiagnosticCode_PdhQueryFailed, S_FALSE, msg);
                return false;
            }
        }
    }

    outCelsius = maxCelsius;
    SetDiagnostic(GAWT_DiagnosticCode_None, S_OK, "ok");
    SetSourceName("pdh-thermal-zone-information");
    return true;
}

bool ReadGpuMetricsFromPdhGpuEngine()
{
    // Initialize cached PDH query on first call
    if (t_CachedPdhGpuQuery == nullptr)
    {
        PDH_STATUS status = PdhOpenQueryW(nullptr, 0, &t_CachedPdhGpuQuery);
        if (status != ERROR_SUCCESS)
        {
            t_CachedPdhGpuQuery = nullptr;
            return false;
        }

        status = PdhAddEnglishCounterW(t_CachedPdhGpuQuery, kPdhGpuEngineCounterPath, 0, &t_CachedPdhGpuCounter);
        if (status != ERROR_SUCCESS)
        {
            PdhCloseQuery(t_CachedPdhGpuQuery);
            t_CachedPdhGpuQuery = nullptr;
            t_CachedPdhGpuCounter = nullptr;
            return false;
        }

        // First collect establishes baseline for rate counters
        PdhCollectQueryData(t_CachedPdhGpuQuery);
        t_LastGpuLoad = 0.0f;
        t_LastGpuClockCurrentMhz = 0.0f;
        t_LastGpuClockMaxMhz = 0.0f;
        t_LastGpuPowerWatts = 0.0f;
        return true;
    }

    // Subsequent calls: collect and compute delta
    PDH_STATUS status = PdhCollectQueryData(t_CachedPdhGpuQuery);
    if (status != ERROR_SUCCESS)
        return true;

    DWORD bufferSize = 0, itemCount = 0;
    status = PdhGetFormattedCounterArrayW(t_CachedPdhGpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
    if (status != PDH_MORE_DATA || bufferSize == 0)
        return true;

    std::vector<unsigned char> buffer(bufferSize);
    auto items = reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(buffer.data());
    status = PdhGetFormattedCounterArrayW(t_CachedPdhGpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
    if (status != ERROR_SUCCESS || itemCount == 0)
        return true;

    // Sum utilization across all 3D GPU engine instances
    double total3D = 0.0;
    for (DWORD i = 0; i < itemCount; ++i)
    {
        if (items[i].FmtValue.CStatus != ERROR_SUCCESS)
            continue;
        if (wcsstr(items[i].szName, L"engtype_3D") != nullptr)
            total3D += items[i].FmtValue.doubleValue;
    }
    if (total3D > 100.0)
        total3D = 100.0;

    t_LastGpuLoad = static_cast<float>(total3D);
    t_LastGpuClockCurrentMhz = 0.0f;
    t_LastGpuClockMaxMhz = 0.0f;
    t_LastGpuPowerWatts = 0.0f;
    return true;
}
