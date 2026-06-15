#include "ThermalCommon.h"

// ============================================================
// NVIDIA Thermal (NVAPI) + GPU Metrics (NVML)
// ============================================================

namespace
{
    constexpr wchar_t kNvapiDllName[] = L"nvapi64.dll";
    constexpr unsigned int kNvapiInterfaceInitialize = 0x0150E828;
    constexpr unsigned int kNvapiInterfaceUnload = 0xD22BDD7E;
    constexpr unsigned int kNvapiInterfaceEnumPhysicalGpus = 0xE5AC921F;
    constexpr unsigned int kNvapiInterfaceGetThermalSettings = 0xE3640A56;
    constexpr int kNvapiMaxPhysicalGpus = 64;
    constexpr int kNvapiMaxThermalSensorsPerGpu = 3;
    constexpr int kNvapiThermalTargetAll = 15;

    typedef int NvAPI_Status;
    typedef void* NvPhysicalGpuHandle;

    struct NV_GPU_THERMAL_SETTINGS_SENSOR
    {
        int controller;
        unsigned int defaultMinTemp;
        unsigned int defaultMaxTemp;
        unsigned int currentTemp;
        int target;
    };

    struct NV_GPU_THERMAL_SETTINGS
    {
        unsigned int version;
        unsigned int count;
        NV_GPU_THERMAL_SETTINGS_SENSOR sensor[kNvapiMaxThermalSensorsPerGpu];
    };

    constexpr unsigned int NVAPI_MAKE_VERSION(unsigned int typeSize, unsigned int version)
    {
        return typeSize | (version << 16);
    }

    typedef void* (__cdecl* NvAPI_QueryInterfaceFn)(unsigned int);
    typedef NvAPI_Status (__cdecl* NvAPI_InitializeFn)();
    typedef NvAPI_Status (__cdecl* NvAPI_UnloadFn)();
    typedef NvAPI_Status (__cdecl* NvAPI_EnumPhysicalGpusFn)(NvPhysicalGpuHandle handles[kNvapiMaxPhysicalGpus], int* count);
    typedef NvAPI_Status (__cdecl* NvAPI_GPU_GetThermalSettingsFn)(NvPhysicalGpuHandle handle, int sensorIndex, NV_GPU_THERMAL_SETTINGS* settings);

    // NVML types
    constexpr const wchar_t kNvmlDllName[] = L"nvml.dll";
    constexpr unsigned int NVML_CLOCK_GRAPHICS = 0;

    typedef int (*NvmlInit_t)();
    typedef int (*NvmlShutdown_t)();
    typedef int (*NvmlDeviceGetHandleByIndex_t)(unsigned int index, void** device);
    typedef int (*NvmlDeviceGetUtilizationRates_t)(void* device, unsigned int* utilization);
    typedef int (*NvmlDeviceGetClockInfo_t)(void* device, unsigned int clockType, unsigned int* clockMHz);
    typedef int (*NvmlDeviceGetMaxClockInfo_t)(void* device, unsigned int clockType, unsigned int* clockMHz);
    typedef int (*NvmlDeviceGetPowerUsage_t)(void* device, unsigned int* powerMilliWatts);
}

bool TryReadThermalFromNvApi(float& outCelsius)
{
    HMODULE nvapiModule = LoadLibraryW(kNvapiDllName);
    if (nvapiModule == nullptr)
    {
        SetDiagnostic(GAWT_DiagnosticCode_NvapiUnavailable, HRESULT_FROM_WIN32(GetLastError()), "nvapi64.dll not found");
        return false;
    }

    auto queryInterface = reinterpret_cast<NvAPI_QueryInterfaceFn>(GetProcAddress(nvapiModule, "nvapi_QueryInterface"));
    if (queryInterface == nullptr)
    {
        FreeLibrary(nvapiModule);
        SetDiagnostic(GAWT_DiagnosticCode_NvapiUnavailable, HRESULT_FROM_WIN32(GetLastError()), "nvapi_QueryInterface not found");
        return false;
    }

    auto nvInitialize = reinterpret_cast<NvAPI_InitializeFn>(queryInterface(kNvapiInterfaceInitialize));
    auto nvUnload = reinterpret_cast<NvAPI_UnloadFn>(queryInterface(kNvapiInterfaceUnload));
    auto nvEnumPhysicalGpus = reinterpret_cast<NvAPI_EnumPhysicalGpusFn>(queryInterface(kNvapiInterfaceEnumPhysicalGpus));
    auto nvGetThermalSettings = reinterpret_cast<NvAPI_GPU_GetThermalSettingsFn>(queryInterface(kNvapiInterfaceGetThermalSettings));

    if (nvInitialize == nullptr || nvEnumPhysicalGpus == nullptr || nvGetThermalSettings == nullptr)
    {
        FreeLibrary(nvapiModule);
        SetDiagnostic(GAWT_DiagnosticCode_NvapiUnavailable, E_POINTER, "required NVAPI interfaces unavailable");
        return false;
    }

    NvAPI_Status initStatus = nvInitialize();
    if (initStatus != 0)
    {
        if (nvUnload != nullptr) nvUnload();
        FreeLibrary(nvapiModule);
        SetDiagnostic(GAWT_DiagnosticCode_NvapiInitializeFailed, static_cast<HRESULT>(initStatus), "NvAPI_Initialize failed");
        return false;
    }

    NvPhysicalGpuHandle gpuHandles[kNvapiMaxPhysicalGpus] = {};
    int gpuCount = 0;
    NvAPI_Status enumStatus = nvEnumPhysicalGpus(gpuHandles, &gpuCount);
    if (enumStatus != 0 || gpuCount <= 0)
    {
        if (nvUnload != nullptr) nvUnload();
        FreeLibrary(nvapiModule);
        SetDiagnostic(GAWT_DiagnosticCode_NvapiNoGpus, static_cast<HRESULT>(enumStatus), "no NVIDIA physical GPUs available");
        return false;
    }

    bool found = false;
    float hottestCelsius = -273.15f;

    for (int i = 0; i < gpuCount; ++i)
    {
        NV_GPU_THERMAL_SETTINGS thermalSettings = {};
        thermalSettings.version = NVAPI_MAKE_VERSION(static_cast<unsigned int>(sizeof(NV_GPU_THERMAL_SETTINGS)), 2);

        NvAPI_Status thermalStatus = nvGetThermalSettings(gpuHandles[i], kNvapiThermalTargetAll, &thermalSettings);
        if (thermalStatus != 0)
            continue;

        const unsigned int sensorCount = (thermalSettings.count > static_cast<unsigned int>(kNvapiMaxThermalSensorsPerGpu))
            ? static_cast<unsigned int>(kNvapiMaxThermalSensorsPerGpu)
            : thermalSettings.count;

        for (unsigned int s = 0; s < sensorCount; ++s)
        {
            const float celsius = static_cast<float>(thermalSettings.sensor[s].currentTemp);
            if (celsius <= -273.15f || celsius > 150.0f)
                continue;
            if (!found || celsius > hottestCelsius)
            {
                hottestCelsius = celsius;
                found = true;
            }
        }
    }

    if (nvUnload != nullptr) nvUnload();
    FreeLibrary(nvapiModule);

    if (!found)
    {
        SetDiagnostic(GAWT_DiagnosticCode_NvapiThermalQueryFailed, S_FALSE, "NVAPI returned no usable thermal sensor values");
        return false;
    }

    outCelsius = hottestCelsius;
    SetDiagnostic(GAWT_DiagnosticCode_None, S_OK, "ok");
    SetSourceName("nvapi-gpu-thermal");
    return true;
}

bool ReadGpuMetricsFromNvml()
{
    HMODULE nvmlModule = LoadLibraryW(kNvmlDllName);
    if (nvmlModule == nullptr)
        return false;

    auto nvmlInit = reinterpret_cast<NvmlInit_t>(GetProcAddress(nvmlModule, "nvmlInit_v2"));
    if (nvmlInit == nullptr)
        nvmlInit = reinterpret_cast<NvmlInit_t>(GetProcAddress(nvmlModule, "nvmlInit"));

    auto nvmlShutdown = reinterpret_cast<NvmlShutdown_t>(GetProcAddress(nvmlModule, "nvmlShutdown"));

    auto nvmlGetHandle = reinterpret_cast<NvmlDeviceGetHandleByIndex_t>(GetProcAddress(nvmlModule, "nvmlDeviceGetHandleByIndex_v2"));
    if (nvmlGetHandle == nullptr)
        nvmlGetHandle = reinterpret_cast<NvmlDeviceGetHandleByIndex_t>(GetProcAddress(nvmlModule, "nvmlDeviceGetHandleByIndex"));

    auto nvmlGetUtil = reinterpret_cast<NvmlDeviceGetUtilizationRates_t>(GetProcAddress(nvmlModule, "nvmlDeviceGetUtilizationRates"));
    auto nvmlGetClock = reinterpret_cast<NvmlDeviceGetClockInfo_t>(GetProcAddress(nvmlModule, "nvmlDeviceGetClockInfo"));
    auto nvmlGetMaxClock = reinterpret_cast<NvmlDeviceGetMaxClockInfo_t>(GetProcAddress(nvmlModule, "nvmlDeviceGetMaxClockInfo"));
    auto nvmlGetPower = reinterpret_cast<NvmlDeviceGetPowerUsage_t>(GetProcAddress(nvmlModule, "nvmlDeviceGetPowerUsage"));

    if (nvmlInit == nullptr || nvmlGetHandle == nullptr)
    {
        FreeLibrary(nvmlModule);
        return false;
    }

    if (nvmlInit() != 0)
    {
        FreeLibrary(nvmlModule);
        return false;
    }

    void* device = nullptr;
    if (nvmlGetHandle(0, &device) != 0 || device == nullptr)
    {
        if (nvmlShutdown) nvmlShutdown();
        FreeLibrary(nvmlModule);
        return false;
    }

    bool gotAnything = false;

    if (nvmlGetUtil != nullptr)
    {
        unsigned int utilization[2] = {};
        if (nvmlGetUtil(device, utilization) == 0)
        {
            t_LastGpuLoad = static_cast<float>(utilization[0]);
            gotAnything = true;
        }
    }

    if (nvmlGetClock != nullptr)
    {
        unsigned int clockMhz = 0;
        if (nvmlGetClock(device, NVML_CLOCK_GRAPHICS, &clockMhz) == 0)
        {
            t_LastGpuClockCurrentMhz = static_cast<float>(clockMhz);
            gotAnything = true;
        }
    }

    if (nvmlGetMaxClock != nullptr)
    {
        unsigned int maxClockMhz = 0;
        if (nvmlGetMaxClock(device, NVML_CLOCK_GRAPHICS, &maxClockMhz) == 0)
            t_LastGpuClockMaxMhz = static_cast<float>(maxClockMhz);
        else
            t_LastGpuClockMaxMhz = t_LastGpuClockCurrentMhz;
    }
    else
    {
        t_LastGpuClockMaxMhz = t_LastGpuClockCurrentMhz;
    }

    if (nvmlGetPower != nullptr)
    {
        unsigned int powerMw = 0;
        if (nvmlGetPower(device, &powerMw) == 0)
        {
            t_LastGpuPowerWatts = static_cast<float>(powerMw) / 1000.0f;
            gotAnything = true;
        }
    }

    if (nvmlShutdown) nvmlShutdown();
    FreeLibrary(nvmlModule);
    return gotAnything;
}
