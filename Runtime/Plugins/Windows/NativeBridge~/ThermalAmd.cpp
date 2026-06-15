#include "ThermalCommon.h"

// Define to enable verbose debug output for ADL GPU metrics diagnostics.
// Undefine or comment out when done debugging.
#define ADL_METRICS_DEBUG

#ifdef ADL_METRICS_DEBUG
#define ADL_DBG(fmt, ...) std::printf("[ADL-METRICS] " fmt "\n", ##__VA_ARGS__)
#else
#define ADL_DBG(fmt, ...) ((void)0)
#endif

// ============================================================
// AMD Thermal (ADL/ADL2) + GPU Metrics (ADL2 PMLog)
// ============================================================

namespace
{
    constexpr const wchar_t kAdlDllName[] = L"atiadlxx.dll";
    constexpr const wchar_t kAdlAltDllName[] = L"atiadlxy.dll";
    constexpr int ADL_OK = 0;
    constexpr int ADL_MAX_ADAPTERS = 64;
    constexpr int ADL_MAX_PATH = 256;
    constexpr int ADL_PMLOG_MAX_SENSORS = 256;
    constexpr int ADL_VENDOR_ID_AMD = 0x1002;
    constexpr int ADL_VENDOR_ID_AMD_DECIMAL = 1002;
    constexpr int ADL_ODN_TEMPERATURE_TYPE_EDGE = 1;
    constexpr int ADL_PMLOG_TEMPERATURE_EDGE = 8;
    constexpr int ADL_PMLOG_TEMPERATURE_HOTSPOT = 27;
    constexpr int ADL_PMLOG_TEMPERATURE_GFX = 28;
    constexpr int ADL_PMLOG_TEMPERATURE_SOC = 29;
    constexpr int ADL_PMLOG_TEMPERATURE_HOTSPOT_GCD = 50;
    constexpr int ADL_PMLOG_TEMPERATURE_HOTSPOT_MCD = 51;

    constexpr int ADL_PMLOG_INFO_ACTIVITY_GFX = 7;
    constexpr int ADL_PMLOG_CLK_GFXCLK = 0;
    constexpr int ADL_PMLOG_CLK_MEMCLK = 1;
    constexpr int ADL_PMLOG_ASIC_POWER = 45;

    typedef int ADL_Status;
    typedef void* ADL_CONTEXT_HANDLE;
    typedef void* (*ADL_MAIN_MALLOC_CALLBACK)(int);

    struct ADL_ADAPTER_INFO
    {
        int iSize;
        int iAdapterIndex;
        char strUDID[ADL_MAX_PATH];
        int iBusNumber;
        int iDeviceNumber;
        int iFunctionNumber;
        int iVendorID;
        char strAdapterName[ADL_MAX_PATH];
        char strDisplayName[ADL_MAX_PATH];
        int iPresent;
        int iExist;
        char strDriverPath[ADL_MAX_PATH];
        char strDriverPathExt[ADL_MAX_PATH];
        char strPNPString[ADL_MAX_PATH];
        int iOSDisplayIndex;
    };

    struct ADL_TEMPERATURE
    {
        int iSize;
        int iTemperature;
    };

    struct ADL_SINGLE_SENSOR_DATA
    {
        int supported;
        int value;
    };

    struct ADL_PMLOG_DATA_OUTPUT
    {
        int size;
        ADL_SINGLE_SENSOR_DATA sensors[ADL_PMLOG_MAX_SENSORS];
    };

    typedef ADL_Status (WINAPI* ADL_Main_Control_CreateFn)(ADL_MAIN_MALLOC_CALLBACK, int);
    typedef ADL_Status (WINAPI* ADL_Main_Control_DestroyFn)(void);
    typedef ADL_Status (WINAPI* ADL_Adapter_NumberOfAdapters_GetFn)(int*);
    typedef ADL_Status (WINAPI* ADL_Adapter_AdapterInfo_GetFn)(ADL_ADAPTER_INFO*, int);
    typedef ADL_Status (WINAPI* ADL_Adapter_ID_GetFn)(int, int*);
    typedef ADL_Status (WINAPI* ADL_PM_Temperature_GetFn)(int, int, ADL_TEMPERATURE*);
    typedef ADL_Status (WINAPI* ADL_Overdrive5_Temperature_GetFn)(int, int, ADL_TEMPERATURE*);
    typedef ADL_Status (WINAPI* ADL2_Main_Control_CreateFn)(ADL_MAIN_MALLOC_CALLBACK, int, ADL_CONTEXT_HANDLE*);
    typedef ADL_Status (WINAPI* ADL2_Main_Control_DestroyFn)(ADL_CONTEXT_HANDLE);
    typedef ADL_Status (WINAPI* ADL2_Adapter_NumberOfAdapters_GetFn)(ADL_CONTEXT_HANDLE, int*);
    typedef ADL_Status (WINAPI* ADL2_Adapter_AdapterInfo_GetFn)(ADL_CONTEXT_HANDLE, ADL_ADAPTER_INFO*, int);
    typedef ADL_Status (WINAPI* ADL2_Adapter_ID_GetFn)(ADL_CONTEXT_HANDLE, int, int*);
    typedef ADL_Status (WINAPI* ADL2_PM_Temperature_GetFn)(ADL_CONTEXT_HANDLE, int, int, ADL_TEMPERATURE*);
    typedef ADL_Status (WINAPI* ADL2_Overdrive_CapsFn)(ADL_CONTEXT_HANDLE, int, int*, int*, int*);
    typedef ADL_Status (WINAPI* ADL2_OverdriveN_Temperature_GetFn)(ADL_CONTEXT_HANDLE, int, int, int*);
    typedef ADL_Status (WINAPI* ADL2_Overdrive5_Temperature_GetFn)(ADL_CONTEXT_HANDLE, int, int, ADL_TEMPERATURE*);
    typedef ADL_Status (WINAPI* ADL2_New_QueryPMLogData_GetFn)(ADL_CONTEXT_HANDLE, int, ADL_PMLOG_DATA_OUTPUT*);

    // ADL Overdrive5 Activity — widely supported fallback for GPU load/clocks
    struct ADL_PM_ACTIVITY
    {
        int iSize;
        int iEngineClock;        // GPU clock in 10 kHz units (divide by 100 for MHz)
        int iMemoryClock;        // Memory clock in 10 kHz units
        int iVddc;               // GPU voltage in mV
        int iActivityPercent;    // GPU utilization 0-100
        int iCurrentPerformanceLevel;
        int iCurrentBusSpeed;
        int iCurrentBusLanes;
        int iMaximumBusLanes;
        int iReserved;
    };

    typedef ADL_Status (WINAPI* ADL_Overdrive5_CurrentActivity_GetFn)(int, ADL_PM_ACTIVITY*);
    typedef ADL_Status (WINAPI* ADL2_Overdrive5_CurrentActivity_GetFn)(ADL_CONTEXT_HANDLE, int, ADL_PM_ACTIVITY*);

    // ADL Overdrive6 CurrentStatus — common on GCN-era GPUs and some newer drivers
    struct ADL_OD6_CURRENT_STATUS
    {
        int iEngineClock;        // Current engine clock in 10 kHz units
        int iMemoryClock;        // Current memory clock in 10 kHz units
        int iActivityPercent;    // Current GPU activity 0-100
        int iCurrentPerformanceLevel;
        int iCurrentBusSpeed;
        int iCurrentBusLanes;
        int iMaximumBusLanes;
        int iExtValue;
        int iExtMask;
    };

    typedef ADL_Status (WINAPI* ADL_Overdrive6_CurrentStatus_GetFn)(int, ADL_OD6_CURRENT_STATUS*);
    typedef ADL_Status (WINAPI* ADL2_Overdrive6_CurrentStatus_GetFn)(ADL_CONTEXT_HANDLE, int, ADL_OD6_CURRENT_STATUS*);

    static void* adl_malloc(int size)
    {
        return malloc(size);
    }

    bool StringContains(const char* haystack, const char* needle)
    {
        return haystack != nullptr && needle != nullptr && std::strstr(haystack, needle) != nullptr;
    }

    bool IsAmdVendorId(int vendorId)
    {
        const unsigned int maskedVendor = static_cast<unsigned int>(vendorId) & 0xFFFFu;
        return maskedVendor == static_cast<unsigned int>(ADL_VENDOR_ID_AMD) ||
            vendorId == ADL_VENDOR_ID_AMD ||
            vendorId == ADL_VENDOR_ID_AMD_DECIMAL;
    }

    bool IsAmdAdapter(const ADL_ADAPTER_INFO& adapter)
    {
        if (IsAmdVendorId(adapter.iVendorID))
            return true;

        if (StringContains(adapter.strUDID, "VEN_1002") ||
            StringContains(adapter.strUDID, "ven_1002") ||
            StringContains(adapter.strPNPString, "VEN_1002") ||
            StringContains(adapter.strPNPString, "ven_1002"))
            return true;

        if (StringContains(adapter.strAdapterName, "AMD") ||
            StringContains(adapter.strAdapterName, "Radeon") ||
            StringContains(adapter.strDisplayName, "AMD") ||
            StringContains(adapter.strDisplayName, "Radeon"))
            return true;

        return false;
    }
}

bool TryReadThermalFromAdl(float& outCelsius)
{
    HMODULE adlModule = LoadLibraryW(kAdlDllName);
    if (adlModule == nullptr)
        adlModule = LoadLibraryW(kAdlAltDllName);

    if (adlModule == nullptr)
    {
        SetDiagnostic(GAWT_DiagnosticCode_NvapiUnavailable, HRESULT_FROM_WIN32(GetLastError()), "ADL runtime not found (atiadlxx/atiadlxy)");
        return false;
    }

    auto adl2CreateFn = reinterpret_cast<ADL2_Main_Control_CreateFn>(GetProcAddress(adlModule, "ADL2_Main_Control_Create"));
    auto adl2DestroyFn = reinterpret_cast<ADL2_Main_Control_DestroyFn>(GetProcAddress(adlModule, "ADL2_Main_Control_Destroy"));
    auto adl2AdapterCountFn = reinterpret_cast<ADL2_Adapter_NumberOfAdapters_GetFn>(GetProcAddress(adlModule, "ADL2_Adapter_NumberOfAdapters_Get"));
    auto adl2AdapterInfoFn = reinterpret_cast<ADL2_Adapter_AdapterInfo_GetFn>(GetProcAddress(adlModule, "ADL2_Adapter_AdapterInfo_Get"));
    auto adl2AdapterIdFn = reinterpret_cast<ADL2_Adapter_ID_GetFn>(GetProcAddress(adlModule, "ADL2_Adapter_ID_Get"));
    auto adl2PmThermalFn = reinterpret_cast<ADL2_PM_Temperature_GetFn>(GetProcAddress(adlModule, "ADL2_PM_Temperature_Get"));
    auto adl2PmLogFn = reinterpret_cast<ADL2_New_QueryPMLogData_GetFn>(GetProcAddress(adlModule, "ADL2_New_QueryPMLogData_Get"));
    auto adl2OverdriveCapsFn = reinterpret_cast<ADL2_Overdrive_CapsFn>(GetProcAddress(adlModule, "ADL2_Overdrive_Caps"));
    auto adl2OverdriveNThermalFn = reinterpret_cast<ADL2_OverdriveN_Temperature_GetFn>(GetProcAddress(adlModule, "ADL2_OverdriveN_Temperature_Get"));
    auto adl2Overdrive5ThermalFn = reinterpret_cast<ADL2_Overdrive5_Temperature_GetFn>(GetProcAddress(adlModule, "ADL2_Overdrive5_Temperature_Get"));

    auto adlCreateFn = reinterpret_cast<ADL_Main_Control_CreateFn>(GetProcAddress(adlModule, "ADL_Main_Control_Create"));
    auto adlDestroyFn = reinterpret_cast<ADL_Main_Control_DestroyFn>(GetProcAddress(adlModule, "ADL_Main_Control_Destroy"));
    auto adlAdapterCountFn = reinterpret_cast<ADL_Adapter_NumberOfAdapters_GetFn>(GetProcAddress(adlModule, "ADL_Adapter_NumberOfAdapters_Get"));
    auto adlAdapterInfoFn = reinterpret_cast<ADL_Adapter_AdapterInfo_GetFn>(GetProcAddress(adlModule, "ADL_Adapter_AdapterInfo_Get"));
    auto adlAdapterIdFn = reinterpret_cast<ADL_Adapter_ID_GetFn>(GetProcAddress(adlModule, "ADL_Adapter_ID_Get"));
    auto adlPmThermalFn = reinterpret_cast<ADL_PM_Temperature_GetFn>(GetProcAddress(adlModule, "ADL_PM_Temperature_Get"));
    auto adlLegacyOverdrive5ThermalFn = reinterpret_cast<ADL_Overdrive5_Temperature_GetFn>(GetProcAddress(adlModule, "ADL_Overdrive5_Temperature_Get"));

    const bool hasAdl2Core =
        adl2CreateFn != nullptr &&
        adl2DestroyFn != nullptr &&
        adl2AdapterCountFn != nullptr &&
        adl2AdapterInfoFn != nullptr;

    const bool hasAdlCore =
        adlCreateFn != nullptr &&
        adlDestroyFn != nullptr &&
        adlAdapterCountFn != nullptr &&
        adlAdapterInfoFn != nullptr;

    if (!hasAdl2Core && !hasAdlCore)
    {
        FreeLibrary(adlModule);
        SetDiagnostic(GAWT_DiagnosticCode_NvapiUnavailable, E_POINTER, "required ADL functions unavailable");
        return false;
    }

    ADL_CONTEXT_HANDLE adlContext = nullptr;
    const bool useAdl2 = hasAdl2Core;

    ADL_Status initStatus = useAdl2
        ? adl2CreateFn(adl_malloc, 1, &adlContext)
        : adlCreateFn(adl_malloc, 1);

    if (initStatus != ADL_OK)
    {
        FreeLibrary(adlModule);
        SetDiagnostic(GAWT_DiagnosticCode_NvapiInitializeFailed, static_cast<HRESULT>(initStatus), useAdl2 ? "ADL2_Main_Control_Create failed" : "ADL_Main_Control_Create failed");
        return false;
    }

    int adapterCount = 0;
    ADL_Status countStatus = useAdl2
        ? adl2AdapterCountFn(adlContext, &adapterCount)
        : adlAdapterCountFn(&adapterCount);

    if (countStatus != ADL_OK)
    {
        if (useAdl2) adl2DestroyFn(adlContext);
        else adlDestroyFn();
        FreeLibrary(adlModule);
        SetDiagnostic(GAWT_DiagnosticCode_NvapiThermalQueryFailed, static_cast<HRESULT>(countStatus), "ADL_Adapter_NumberOfAdapters_Get failed");
        return false;
    }

    if (adapterCount <= 0)
    {
        if (useAdl2) adl2DestroyFn(adlContext);
        else adlDestroyFn();
        FreeLibrary(adlModule);
        SetDiagnostic(GAWT_DiagnosticCode_NvapiNoGpus, S_FALSE, "ADL reported zero adapters");
        return false;
    }

    if (adapterCount > ADL_MAX_ADAPTERS)
        adapterCount = ADL_MAX_ADAPTERS;

    std::vector<ADL_ADAPTER_INFO> adapters(static_cast<size_t>(adapterCount));
    for (int i = 0; i < adapterCount; ++i)
        adapters[i].iSize = sizeof(ADL_ADAPTER_INFO);

    ADL_Status adapterInfoStatus = useAdl2
        ? adl2AdapterInfoFn(adlContext, adapters.data(), static_cast<int>(sizeof(ADL_ADAPTER_INFO) * adapters.size()))
        : adlAdapterInfoFn(adapters.data(), static_cast<int>(sizeof(ADL_ADAPTER_INFO) * adapters.size()));

    if (adapterInfoStatus != ADL_OK)
    {
        if (useAdl2) adl2DestroyFn(adlContext);
        else adlDestroyFn();
        FreeLibrary(adlModule);
        SetDiagnostic(GAWT_DiagnosticCode_NvapiThermalQueryFailed, static_cast<HRESULT>(adapterInfoStatus), "ADL_Adapter_AdapterInfo_Get failed");
        return false;
    }

    bool found = false;
    float hottestCelsius = -273.15f;
    ADL_Status lastThermalStatus = ADL_OK;
    int amdAdapterCount = 0;
    std::vector<int> seenPhysicalAdapterIds;

    for (int i = 0; i < adapterCount; ++i)
    {
        if (IsAmdAdapter(adapters[static_cast<size_t>(i)]))
            ++amdAdapterCount;
    }

    const bool probeAllAdapters = (amdAdapterCount == 0);

    for (int i = 0; i < adapterCount; ++i)
    {
        const ADL_ADAPTER_INFO& adapter = adapters[static_cast<size_t>(i)];
        if (!probeAllAdapters && adapter.iPresent == 0 && adapter.iExist == 0)
            continue;

        if (!probeAllAdapters && !IsAmdAdapter(adapter))
            continue;

        // Deduplicate physical adapters
        int physicalAdapterId = 0;
        bool hasPhysicalAdapterId = false;

        if (useAdl2 && adl2AdapterIdFn != nullptr)
        {
            if (adl2AdapterIdFn(adlContext, adapter.iAdapterIndex, &physicalAdapterId) == ADL_OK)
                hasPhysicalAdapterId = true;
        }
        else if (adlAdapterIdFn != nullptr)
        {
            if (adlAdapterIdFn(adapter.iAdapterIndex, &physicalAdapterId) == ADL_OK)
                hasPhysicalAdapterId = true;
        }

        if (hasPhysicalAdapterId)
        {
            bool duplicatePhysical = false;
            for (size_t seenIndex = 0; seenIndex < seenPhysicalAdapterIds.size(); ++seenIndex)
            {
                if (seenPhysicalAdapterIds[seenIndex] == physicalAdapterId)
                {
                    duplicatePhysical = true;
                    break;
                }
            }
            if (duplicatePhysical)
                continue;
            seenPhysicalAdapterIds.push_back(physicalAdapterId);
        }

        bool gotAdapterTemp = false;
        float celsius = -273.15f;

        if (useAdl2)
        {
            int odSupported = 0, odEnabled = 0, odVersion = 0;
            ADL_Status capsStatus = ADL_OK;

            if (adl2OverdriveCapsFn != nullptr)
                capsStatus = adl2OverdriveCapsFn(adlContext, adapter.iAdapterIndex, &odSupported, &odEnabled, &odVersion);

            bool shouldTryOverdriveN = adl2OverdriveNThermalFn != nullptr;
            if (capsStatus == ADL_OK)
                shouldTryOverdriveN = shouldTryOverdriveN && (odSupported != 0 && odEnabled != 0);

            if (shouldTryOverdriveN)
            {
                int tempMilliCelsius = 0;
                const int temperatureTypes[] = { ADL_ODN_TEMPERATURE_TYPE_EDGE, 0, 2 };

                for (int t = 0; t < 3 && !gotAdapterTemp; ++t)
                {
                    ADL_Status odNStatus = adl2OverdriveNThermalFn(adlContext, adapter.iAdapterIndex, temperatureTypes[t], &tempMilliCelsius);
                    if (odNStatus != ADL_OK)
                    {
                        lastThermalStatus = odNStatus;
                        continue;
                    }

                    celsius = tempMilliCelsius > 1000
                        ? static_cast<float>(tempMilliCelsius) / 1000.0f
                        : static_cast<float>(tempMilliCelsius);
                    gotAdapterTemp = true;
                }
            }

            if (!gotAdapterTemp && adl2Overdrive5ThermalFn != nullptr)
            {
                ADL_TEMPERATURE tempInfo = {};
                tempInfo.iSize = sizeof(ADL_TEMPERATURE);

                ADL_Status od5Status = adl2Overdrive5ThermalFn(adlContext, adapter.iAdapterIndex, 0, &tempInfo);
                if (od5Status == ADL_OK)
                {
                    celsius = static_cast<float>(tempInfo.iTemperature) / 1000.0f;
                    gotAdapterTemp = true;
                }
                else
                    lastThermalStatus = od5Status;
            }

            if (!gotAdapterTemp && adl2PmThermalFn != nullptr)
            {
                ADL_TEMPERATURE tempInfo = {};
                tempInfo.iSize = sizeof(ADL_TEMPERATURE);

                ADL_Status pmStatus = adl2PmThermalFn(adlContext, adapter.iAdapterIndex, 0, &tempInfo);
                if (pmStatus == ADL_OK)
                {
                    celsius = static_cast<float>(tempInfo.iTemperature) / 1000.0f;
                    gotAdapterTemp = true;
                }
                else
                    lastThermalStatus = pmStatus;
            }

            if (!gotAdapterTemp && adl2PmLogFn != nullptr)
            {
                ADL_PMLOG_DATA_OUTPUT pmLogData = {};
                pmLogData.size = sizeof(ADL_PMLOG_DATA_OUTPUT);

                ADL_Status pmLogStatus = adl2PmLogFn(adlContext, adapter.iAdapterIndex, &pmLogData);
                if (pmLogStatus == ADL_OK)
                {
                    const int sensorCandidates[] =
                    {
                        ADL_PMLOG_TEMPERATURE_HOTSPOT,
                        ADL_PMLOG_TEMPERATURE_EDGE,
                        ADL_PMLOG_TEMPERATURE_GFX,
                        ADL_PMLOG_TEMPERATURE_SOC,
                        ADL_PMLOG_TEMPERATURE_HOTSPOT_GCD,
                        ADL_PMLOG_TEMPERATURE_HOTSPOT_MCD
                    };

                    for (int s = 0; s < 6 && !gotAdapterTemp; ++s)
                    {
                        const int sensorIndex = sensorCandidates[s];
                        if (sensorIndex < 0 || sensorIndex >= ADL_PMLOG_MAX_SENSORS)
                            continue;

                        const ADL_SINGLE_SENSOR_DATA sensor = pmLogData.sensors[sensorIndex];
                        if (sensor.supported == 0)
                            continue;

                        celsius = sensor.value > 1000
                            ? static_cast<float>(sensor.value) / 1000.0f
                            : static_cast<float>(sensor.value);
                        gotAdapterTemp = true;
                    }
                }
                else
                    lastThermalStatus = pmLogStatus;
            }
        }

        if (!gotAdapterTemp && adlPmThermalFn != nullptr)
        {
            ADL_TEMPERATURE tempInfo = {};
            tempInfo.iSize = sizeof(ADL_TEMPERATURE);

            ADL_Status pmStatus = adlPmThermalFn(adapter.iAdapterIndex, 0, &tempInfo);
            if (pmStatus == ADL_OK)
            {
                celsius = static_cast<float>(tempInfo.iTemperature) / 1000.0f;
                gotAdapterTemp = true;
            }
            else
                lastThermalStatus = pmStatus;
        }

        if (!gotAdapterTemp && adlLegacyOverdrive5ThermalFn != nullptr)
        {
            ADL_TEMPERATURE tempInfo = {};
            tempInfo.iSize = sizeof(ADL_TEMPERATURE);

            ADL_Status thermalStatus = adlLegacyOverdrive5ThermalFn(adapter.iAdapterIndex, 0, &tempInfo);
            if (thermalStatus == ADL_OK)
            {
                celsius = static_cast<float>(tempInfo.iTemperature) / 1000.0f;
                gotAdapterTemp = true;
            }
            else
                lastThermalStatus = thermalStatus;
        }

        if (!gotAdapterTemp)
            continue;

        if (celsius <= -273.15f || celsius > 150.0f)
            continue;

        if (!found || celsius > hottestCelsius)
        {
            hottestCelsius = celsius;
            found = true;
        }
    }

    if (useAdl2) adl2DestroyFn(adlContext);
    else adlDestroyFn();
    FreeLibrary(adlModule);

    if (!found)
    {
        HRESULT failureHr = (lastThermalStatus == ADL_OK)
            ? static_cast<HRESULT>(S_FALSE)
            : static_cast<HRESULT>(lastThermalStatus);

        SetDiagnostic(GAWT_DiagnosticCode_NvapiThermalQueryFailed, failureHr, "ADL temperature APIs returned no usable thermal values");
        return false;
    }

    outCelsius = hottestCelsius;
    SetDiagnostic(GAWT_DiagnosticCode_None, S_OK, "ok");
    SetSourceName("adl-gpu-thermal");
    return true;
}

bool ReadGpuMetricsFromAdl()
{
    ADL_DBG("=== ReadGpuMetricsFromAdl START ===");

    HMODULE adlModule = LoadLibraryW(kAdlDllName);
    if (adlModule == nullptr)
    {
        ADL_DBG("LoadLibrary(atiadlxx.dll) failed (err=%lu), trying atiadlxy.dll", GetLastError());
        adlModule = LoadLibraryW(kAdlAltDllName);
    }
    if (adlModule == nullptr)
    {
        ADL_DBG("LoadLibrary(atiadlxy.dll) also failed (err=%lu) - no ADL available", GetLastError());
        return false;
    }
    ADL_DBG("ADL DLL loaded successfully");

    // ADL2 functions
    auto adl2CreateFn = reinterpret_cast<ADL2_Main_Control_CreateFn>(GetProcAddress(adlModule, "ADL2_Main_Control_Create"));
    auto adl2DestroyFn = reinterpret_cast<ADL2_Main_Control_DestroyFn>(GetProcAddress(adlModule, "ADL2_Main_Control_Destroy"));
    auto adl2AdapterCountFn = reinterpret_cast<ADL2_Adapter_NumberOfAdapters_GetFn>(GetProcAddress(adlModule, "ADL2_Adapter_NumberOfAdapters_Get"));
    auto adl2AdapterInfoFn = reinterpret_cast<ADL2_Adapter_AdapterInfo_GetFn>(GetProcAddress(adlModule, "ADL2_Adapter_AdapterInfo_Get"));
    auto adl2PmLogFn = reinterpret_cast<ADL2_New_QueryPMLogData_GetFn>(GetProcAddress(adlModule, "ADL2_New_QueryPMLogData_Get"));
    auto adl2Od5ActivityFn = reinterpret_cast<ADL2_Overdrive5_CurrentActivity_GetFn>(GetProcAddress(adlModule, "ADL2_Overdrive5_CurrentActivity_Get"));
    auto adl2Od6StatusFn = reinterpret_cast<ADL2_Overdrive6_CurrentStatus_GetFn>(GetProcAddress(adlModule, "ADL2_Overdrive6_CurrentStatus_Get"));

    ADL_DBG("ADL2 exports: Create=%p Destroy=%p Count=%p Info=%p PMLog=%p OD5=%p OD6=%p",
        (void*)adl2CreateFn, (void*)adl2DestroyFn, (void*)adl2AdapterCountFn,
        (void*)adl2AdapterInfoFn, (void*)adl2PmLogFn, (void*)adl2Od5ActivityFn, (void*)adl2Od6StatusFn);

    // ADL1 fallback functions
    auto adlCreateFn = reinterpret_cast<ADL_Main_Control_CreateFn>(GetProcAddress(adlModule, "ADL_Main_Control_Create"));
    auto adlDestroyFn = reinterpret_cast<ADL_Main_Control_DestroyFn>(GetProcAddress(adlModule, "ADL_Main_Control_Destroy"));
    auto adlAdapterCountFn = reinterpret_cast<ADL_Adapter_NumberOfAdapters_GetFn>(GetProcAddress(adlModule, "ADL_Adapter_NumberOfAdapters_Get"));
    auto adlAdapterInfoFn = reinterpret_cast<ADL_Adapter_AdapterInfo_GetFn>(GetProcAddress(adlModule, "ADL_Adapter_AdapterInfo_Get"));
    auto adlOd5ActivityFn = reinterpret_cast<ADL_Overdrive5_CurrentActivity_GetFn>(GetProcAddress(adlModule, "ADL_Overdrive5_CurrentActivity_Get"));
    auto adlOd6StatusFn = reinterpret_cast<ADL_Overdrive6_CurrentStatus_GetFn>(GetProcAddress(adlModule, "ADL_Overdrive6_CurrentStatus_Get"));

    ADL_DBG("ADL1 exports: Create=%p Destroy=%p Count=%p Info=%p OD5=%p OD6=%p",
        (void*)adlCreateFn, (void*)adlDestroyFn, (void*)adlAdapterCountFn,
        (void*)adlAdapterInfoFn, (void*)adlOd5ActivityFn, (void*)adlOd6StatusFn);

    const bool hasAdl2 = adl2CreateFn != nullptr && adl2DestroyFn != nullptr &&
                         adl2AdapterCountFn != nullptr && adl2AdapterInfoFn != nullptr;
    const bool hasAdl1 = adlCreateFn != nullptr && adlDestroyFn != nullptr &&
                         adlAdapterCountFn != nullptr && adlAdapterInfoFn != nullptr;

    ADL_DBG("hasAdl2=%d hasAdl1=%d", hasAdl2, hasAdl1);

    if (!hasAdl2 && !hasAdl1)
    {
        ADL_DBG("Neither ADL2 nor ADL1 core functions available - FAIL");
        FreeLibrary(adlModule);
        return false;
    }

    // Initialize ADL2 (preferred) or ADL1
    ADL_CONTEXT_HANDLE adlContext = nullptr;
    const bool useAdl2 = hasAdl2;

    ADL_Status initStatus = useAdl2
        ? adl2CreateFn(adl_malloc, 1, &adlContext)
        : adlCreateFn(adl_malloc, 1);

    ADL_DBG("ADL init (useAdl2=%d): status=%d context=%p", useAdl2, initStatus, (void*)adlContext);

    if (initStatus != ADL_OK)
    {
        ADL_DBG("ADL init FAILED - FAIL");
        FreeLibrary(adlModule);
        return false;
    }

    // Get adapter info to filter to AMD adapters
    int adapterCount = 0;
    ADL_Status countStatus = useAdl2
        ? adl2AdapterCountFn(adlContext, &adapterCount)
        : adlAdapterCountFn(&adapterCount);

    ADL_DBG("Adapter count: status=%d count=%d", countStatus, adapterCount);

    if (countStatus != ADL_OK || adapterCount <= 0)
    {
        ADL_DBG("Adapter count failed or zero - FAIL");
        if (useAdl2) adl2DestroyFn(adlContext);
        else adlDestroyFn();
        FreeLibrary(adlModule);
        return false;
    }

    if (adapterCount > ADL_MAX_ADAPTERS)
        adapterCount = ADL_MAX_ADAPTERS;

    std::vector<ADL_ADAPTER_INFO> adapters(static_cast<size_t>(adapterCount));
    for (int i = 0; i < adapterCount; ++i)
        adapters[i].iSize = sizeof(ADL_ADAPTER_INFO);

    ADL_Status infoStatus = useAdl2
        ? adl2AdapterInfoFn(adlContext, adapters.data(), static_cast<int>(sizeof(ADL_ADAPTER_INFO) * adapters.size()))
        : adlAdapterInfoFn(adapters.data(), static_cast<int>(sizeof(ADL_ADAPTER_INFO) * adapters.size()));

    ADL_DBG("AdapterInfo_Get: status=%d", infoStatus);

    if (infoStatus != ADL_OK)
    {
        ADL_DBG("AdapterInfo_Get FAILED - FAIL");
        if (useAdl2) adl2DestroyFn(adlContext);
        else adlDestroyFn();
        FreeLibrary(adlModule);
        return false;
    }

    // Log all adapters
    ADL_DBG("--- All adapters (%d total) ---", adapterCount);
    for (int i = 0; i < adapterCount; ++i)
    {
        const ADL_ADAPTER_INFO& a = adapters[static_cast<size_t>(i)];
        ADL_DBG("  [%d] idx=%d vendorId=0x%04X bus=%d present=%d exist=%d name='%s' display='%s'",
            i, a.iAdapterIndex, a.iVendorID, a.iBusNumber, a.iPresent, a.iExist,
            a.strAdapterName, a.strDisplayName);
        ADL_DBG("       UDID='%.60s'", a.strUDID);
        ADL_DBG("       isAmd=%d", IsAmdAdapter(a));
    }

    // Collect AMD adapter indices (deduplicated by bus number)
    std::vector<int> amdAdapterIndices;
    std::vector<int> seenBusNumbers;

    for (int i = 0; i < adapterCount; ++i)
    {
        const ADL_ADAPTER_INFO& adapter = adapters[static_cast<size_t>(i)];
        if (!IsAmdAdapter(adapter))
            continue;

        bool duplicate = false;
        for (size_t s = 0; s < seenBusNumbers.size(); ++s)
        {
            if (seenBusNumbers[s] == adapter.iBusNumber)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;
        seenBusNumbers.push_back(adapter.iBusNumber);
        amdAdapterIndices.push_back(adapter.iAdapterIndex);
    }

    ADL_DBG("AMD adapters found: %zu (dedup by bus)", amdAdapterIndices.size());
    for (size_t i = 0; i < amdAdapterIndices.size(); ++i)
        ADL_DBG("  AMD adapter index: %d (bus %d)", amdAdapterIndices[i], seenBusNumbers[i]);

    // If no AMD adapters found by vendor check, try all adapters as fallback
    if (amdAdapterIndices.empty())
    {
        ADL_DBG("No AMD adapters by vendor - falling back to ALL adapters");
        for (int i = 0; i < adapterCount; ++i)
            amdAdapterIndices.push_back(adapters[static_cast<size_t>(i)].iAdapterIndex);
    }

    bool gotAnything = false;

    // Strategy 1: ADL2 PMLog (newer Adrenalin drivers — full metrics)
    ADL_DBG("--- Strategy 1: PMLog (adl2PmLogFn=%p) ---", (void*)adl2PmLogFn);
    if (useAdl2 && adl2PmLogFn != nullptr)
    {
        for (size_t idx = 0; idx < amdAdapterIndices.size() && !gotAnything; ++idx)
        {
            const int adapterIndex = amdAdapterIndices[idx];
            ADL_PMLOG_DATA_OUTPUT pmLogData = {};
            pmLogData.size = sizeof(ADL_PMLOG_DATA_OUTPUT);

            ADL_Status pmLogStatus = adl2PmLogFn(adlContext, adapterIndex, &pmLogData);
            ADL_DBG("  PMLog adapter[%d]: status=%d", adapterIndex, pmLogStatus);

            if (pmLogStatus != ADL_OK)
                continue;

            ADL_DBG("  PMLog sensors: activity[%d] sup=%d val=%d | clk[%d] sup=%d val=%d | power[%d] sup=%d val=%d",
                ADL_PMLOG_INFO_ACTIVITY_GFX,
                pmLogData.sensors[ADL_PMLOG_INFO_ACTIVITY_GFX].supported,
                pmLogData.sensors[ADL_PMLOG_INFO_ACTIVITY_GFX].value,
                ADL_PMLOG_CLK_GFXCLK,
                pmLogData.sensors[ADL_PMLOG_CLK_GFXCLK].supported,
                pmLogData.sensors[ADL_PMLOG_CLK_GFXCLK].value,
                ADL_PMLOG_ASIC_POWER,
                pmLogData.sensors[ADL_PMLOG_ASIC_POWER].supported,
                pmLogData.sensors[ADL_PMLOG_ASIC_POWER].value);

            if (ADL_PMLOG_INFO_ACTIVITY_GFX < ADL_PMLOG_MAX_SENSORS &&
                pmLogData.sensors[ADL_PMLOG_INFO_ACTIVITY_GFX].supported != 0)
            {
                t_LastGpuLoad = static_cast<float>(pmLogData.sensors[ADL_PMLOG_INFO_ACTIVITY_GFX].value);
                gotAnything = true;
            }

            if (ADL_PMLOG_CLK_GFXCLK < ADL_PMLOG_MAX_SENSORS &&
                pmLogData.sensors[ADL_PMLOG_CLK_GFXCLK].supported != 0)
            {
                t_LastGpuClockCurrentMhz = static_cast<float>(pmLogData.sensors[ADL_PMLOG_CLK_GFXCLK].value);
                t_LastGpuClockMaxMhz = t_LastGpuClockCurrentMhz;
                gotAnything = true;
            }

            if (ADL_PMLOG_ASIC_POWER < ADL_PMLOG_MAX_SENSORS &&
                pmLogData.sensors[ADL_PMLOG_ASIC_POWER].supported != 0)
            {
                t_LastGpuPowerWatts = static_cast<float>(pmLogData.sensors[ADL_PMLOG_ASIC_POWER].value);
                gotAnything = true;
            }

            ADL_DBG("  PMLog result: gotAnything=%d", gotAnything);
        }
    }
    else
    {
        ADL_DBG("  PMLog SKIPPED (useAdl2=%d, fn=%p)", useAdl2, (void*)adl2PmLogFn);
    }

    // Strategy 2: ADL2 Overdrive5 CurrentActivity
    ADL_DBG("--- Strategy 2: OD5 Activity (adl2Od5ActivityFn=%p) gotAnything=%d ---", (void*)adl2Od5ActivityFn, gotAnything);
    if (!gotAnything && useAdl2 && adl2Od5ActivityFn != nullptr)
    {
        for (size_t idx = 0; idx < amdAdapterIndices.size() && !gotAnything; ++idx)
        {
            const int adapterIndex = amdAdapterIndices[idx];
            ADL_PM_ACTIVITY activity = {};
            activity.iSize = sizeof(ADL_PM_ACTIVITY);

            ADL_Status actStatus = adl2Od5ActivityFn(adlContext, adapterIndex, &activity);
            ADL_DBG("  OD5 adapter[%d]: status=%d activity=%d%% clock=%d memclk=%d vddc=%d",
                adapterIndex, actStatus, activity.iActivityPercent,
                activity.iEngineClock, activity.iMemoryClock, activity.iVddc);

            if (actStatus != ADL_OK)
                continue;

            if (activity.iActivityPercent >= 0 && activity.iActivityPercent <= 100)
            {
                t_LastGpuLoad = static_cast<float>(activity.iActivityPercent);
                gotAnything = true;
            }

            if (activity.iEngineClock > 0)
            {
                t_LastGpuClockCurrentMhz = static_cast<float>(activity.iEngineClock) / 100.0f;
                t_LastGpuClockMaxMhz = t_LastGpuClockCurrentMhz;
                gotAnything = true;
            }
        }
    }

    // Strategy 3: ADL2 Overdrive6 CurrentStatus (GCN-era GPUs)
    ADL_DBG("--- Strategy 3: OD6 Status (adl2Od6StatusFn=%p) gotAnything=%d ---", (void*)adl2Od6StatusFn, gotAnything);
    if (!gotAnything && useAdl2 && adl2Od6StatusFn != nullptr)
    {
        for (size_t idx = 0; idx < amdAdapterIndices.size() && !gotAnything; ++idx)
        {
            const int adapterIndex = amdAdapterIndices[idx];
            ADL_OD6_CURRENT_STATUS status = {};

            ADL_Status od6Status = adl2Od6StatusFn(adlContext, adapterIndex, &status);
            ADL_DBG("  OD6 adapter[%d]: status=%d activity=%d%% clock=%d memclk=%d",
                adapterIndex, od6Status, status.iActivityPercent,
                status.iEngineClock, status.iMemoryClock);

            if (od6Status != ADL_OK)
                continue;

            if (status.iActivityPercent >= 0 && status.iActivityPercent <= 100)
            {
                t_LastGpuLoad = static_cast<float>(status.iActivityPercent);
                gotAnything = true;
            }

            if (status.iEngineClock > 0)
            {
                t_LastGpuClockCurrentMhz = static_cast<float>(status.iEngineClock) / 100.0f;
                t_LastGpuClockMaxMhz = t_LastGpuClockCurrentMhz;
                gotAnything = true;
            }
        }
    }

    // Done with ADL2 context
    if (useAdl2) adl2DestroyFn(adlContext);
    else adlDestroyFn();

    // Strategy 4: ADL1 Overdrive5/6 CurrentActivity (separate init required)
    ADL_DBG("--- Strategy 4: ADL1 OD5/OD6 (hasAdl1=%d od5=%p od6=%p) gotAnything=%d ---",
        hasAdl1, (void*)adlOd5ActivityFn, (void*)adlOd6StatusFn, gotAnything);
    if (!gotAnything && hasAdl1 && (adlOd5ActivityFn != nullptr || adlOd6StatusFn != nullptr))
    {
        ADL_Status adl1InitStatus = adlCreateFn(adl_malloc, 1);
        ADL_DBG("  ADL1 init: status=%d", adl1InitStatus);

        if (adl1InitStatus == ADL_OK)
        {
            int adl1AdapterCount = 0;
            ADL_Status adl1CountStatus = adlAdapterCountFn(&adl1AdapterCount);
            ADL_DBG("  ADL1 adapter count: status=%d count=%d", adl1CountStatus, adl1AdapterCount);

            if (adl1CountStatus == ADL_OK && adl1AdapterCount > 0)
            {
                // Try OD5 Activity first
                if (adlOd5ActivityFn != nullptr)
                {
                    ADL_DBG("  ADL1 trying OD5 Activity...");
                    for (size_t idx = 0; idx < amdAdapterIndices.size() && !gotAnything; ++idx)
                    {
                        const int adapterIndex = amdAdapterIndices[idx];
                        if (adapterIndex >= adl1AdapterCount)
                        {
                            ADL_DBG("    adapter[%d] >= count(%d), skip", adapterIndex, adl1AdapterCount);
                            continue;
                        }

                        ADL_PM_ACTIVITY activity = {};
                        activity.iSize = sizeof(ADL_PM_ACTIVITY);

                        ADL_Status actStatus = adlOd5ActivityFn(adapterIndex, &activity);
                        ADL_DBG("    ADL1 OD5 adapter[%d]: status=%d activity=%d%% clock=%d",
                            adapterIndex, actStatus, activity.iActivityPercent, activity.iEngineClock);

                        if (actStatus != ADL_OK)
                            continue;

                        if (activity.iActivityPercent >= 0 && activity.iActivityPercent <= 100)
                        {
                            t_LastGpuLoad = static_cast<float>(activity.iActivityPercent);
                            gotAnything = true;
                        }

                        if (activity.iEngineClock > 0)
                        {
                            t_LastGpuClockCurrentMhz = static_cast<float>(activity.iEngineClock) / 100.0f;
                            t_LastGpuClockMaxMhz = t_LastGpuClockCurrentMhz;
                            gotAnything = true;
                        }
                    }
                }

                // Try OD6 Status
                if (!gotAnything && adlOd6StatusFn != nullptr)
                {
                    ADL_DBG("  ADL1 trying OD6 Status...");
                    for (size_t idx = 0; idx < amdAdapterIndices.size() && !gotAnything; ++idx)
                    {
                        const int adapterIndex = amdAdapterIndices[idx];
                        if (adapterIndex >= adl1AdapterCount)
                        {
                            ADL_DBG("    adapter[%d] >= count(%d), skip", adapterIndex, adl1AdapterCount);
                            continue;
                        }

                        ADL_OD6_CURRENT_STATUS status = {};

                        ADL_Status od6Status = adlOd6StatusFn(adapterIndex, &status);
                        ADL_DBG("    ADL1 OD6 adapter[%d]: status=%d activity=%d%% clock=%d",
                            adapterIndex, od6Status, status.iActivityPercent, status.iEngineClock);

                        if (od6Status != ADL_OK)
                            continue;

                        if (status.iActivityPercent >= 0 && status.iActivityPercent <= 100)
                        {
                            t_LastGpuLoad = static_cast<float>(status.iActivityPercent);
                            gotAnything = true;
                        }

                        if (status.iEngineClock > 0)
                        {
                            t_LastGpuClockCurrentMhz = static_cast<float>(status.iEngineClock) / 100.0f;
                            t_LastGpuClockMaxMhz = t_LastGpuClockCurrentMhz;
                            gotAnything = true;
                        }
                    }
                }
            }
            adlDestroyFn();
        }
    }

    ADL_DBG("=== ReadGpuMetricsFromAdl END: gotAnything=%d ===", gotAnything);
    FreeLibrary(adlModule);
    return gotAnything;
}
