#include "ThermalCommon.h"

// ============================================================
// Intel Thermal (IGCL/DPTF) + GPU Metrics (IGCL)
// ============================================================

namespace
{
    constexpr const wchar_t kIgclDllName[] = L"ControlLib.dll";
    constexpr int IGCL_SUCCESS = 0;
    constexpr uint32_t kIgclAppVersion = (1 << 16) | 1;
    constexpr uint32_t kIgclInitFlagUseLevelZero = 1;

    typedef struct _ctl_api_handle_t* IgclApiHandle;
    typedef struct _ctl_device_adapter_handle_t* IgclDeviceHandle;
    typedef struct _ctl_freq_handle_t* IgclFreqHandle;
    typedef struct _ctl_engine_handle_t* IgclEngineHandle;
    typedef struct _ctl_temp_handle_t* IgclTempHandle;

    struct IgclAppId { uint32_t Data1; uint16_t Data2; uint16_t Data3; uint8_t Data4[8]; };

    struct IgclInitArgs {
        uint32_t Size;
        uint8_t  Version;
        uint32_t AppVersion;
        uint32_t Flags;
        uint32_t SupportedVersion;
        IgclAppId ApplicationUID;
    };

    enum IgclFreqDomain { IGCL_FREQ_GPU = 0, IGCL_FREQ_MEM = 1 };

    struct IgclFreqProperties {
        uint32_t Size; uint8_t Version;
        int type; bool canControl;
        double min; double max;
    };

    struct IgclFreqState {
        uint32_t Size; uint8_t Version;
        double currentVoltage; double request; double tdp; double efficient; double actual;
        uint32_t throttleReasons;
    };

    enum IgclEngineGroup { IGCL_ENGINE_GT = 0, IGCL_ENGINE_RENDER = 1, IGCL_ENGINE_MEDIA = 2 };

    struct IgclEngineProperties { uint32_t Size; uint8_t Version; int type; };

    struct IgclEngineStats { uint32_t Size; uint8_t Version; uint64_t activeTime; uint64_t timestamp; };

    enum IgclTempSensor { IGCL_TEMP_GLOBAL = 0, IGCL_TEMP_GPU = 1, IGCL_TEMP_MEMORY = 2 };

    struct IgclTempProperties { uint32_t Size; uint8_t Version; int type; double maxTemperature; };

    struct IgclOcTelemetryItem
    {
        bool bSupported;
        int32_t units;
        int32_t type;
        union {
            int8_t data8; uint8_t datau8;
            int16_t data16; uint16_t datau16;
            int32_t data32; uint32_t datau32;
            int64_t data64; uint64_t datau64;
            float datafloat;
            double datadouble;
        } value;
    };

    struct IgclPowerTelemetry
    {
        uint32_t Size;
        uint8_t Version;
        uint8_t _pad[3];
        IgclOcTelemetryItem timeStamp;
        IgclOcTelemetryItem gpuEnergyCounter;
        IgclOcTelemetryItem gpuVoltage;
        IgclOcTelemetryItem gpuCurrentClockFrequency;
        IgclOcTelemetryItem gpuCurrentTemperature;
    };

    static constexpr size_t kIgclPowerTelemetryFullSize = 1024;

    typedef int (__cdecl* IgclInit_t)(IgclInitArgs*, IgclApiHandle*);
    typedef int (__cdecl* IgclClose_t)(IgclApiHandle);
    typedef int (__cdecl* IgclEnumDevices_t)(IgclApiHandle, uint32_t*, IgclDeviceHandle*);
    typedef int (__cdecl* IgclEnumFreqDomains_t)(IgclDeviceHandle, uint32_t*, IgclFreqHandle*);
    typedef int (__cdecl* IgclFreqGetProps_t)(IgclFreqHandle, IgclFreqProperties*);
    typedef int (__cdecl* IgclFreqGetState_t)(IgclFreqHandle, IgclFreqState*);
    typedef int (__cdecl* IgclEnumEngines_t)(IgclDeviceHandle, uint32_t*, IgclEngineHandle*);
    typedef int (__cdecl* IgclEngineGetProps_t)(IgclEngineHandle, IgclEngineProperties*);
    typedef int (__cdecl* IgclEngineGetActivity_t)(IgclEngineHandle, IgclEngineStats*);
    typedef int (__cdecl* IgclEnumTempSensors_t)(IgclDeviceHandle, uint32_t*, IgclTempHandle*);
    typedef int (__cdecl* IgclTempGetProps_t)(IgclTempHandle, IgclTempProperties*);
    typedef int (__cdecl* IgclTempGetState_t)(IgclTempHandle, double*);
    typedef int (__cdecl* IgclPowerTelemetryGet_t)(IgclDeviceHandle, IgclPowerTelemetry*);

    // Module-private IGCL cached state
    thread_local HMODULE t_IgclModule = nullptr;
    thread_local IgclApiHandle t_IgclApi = nullptr;
    thread_local IgclFreqHandle t_IgclGpuFreq = nullptr;
    thread_local IgclEngineHandle t_IgclGtEngine = nullptr;
    thread_local double t_IgclMaxFreqMhz = 0.0;
    thread_local uint64_t t_IgclPrevActiveUs = 0;
    thread_local uint64_t t_IgclPrevTimestampUs = 0;
    thread_local bool t_IgclReady = false;
    thread_local IgclFreqGetState_t t_igclFreqGetState = nullptr;
    thread_local IgclEngineGetActivity_t t_igclEngineGetActivity = nullptr;

    // Thermal-specific IGCL state
    thread_local IgclTempHandle t_IgclTempSensor = nullptr;
    thread_local bool t_IgclTempReady = false;
    thread_local IgclTempGetState_t t_igclTempGetState = nullptr;
    thread_local IgclPowerTelemetryGet_t t_igclPowerTelemetryGet = nullptr;
    thread_local IgclDeviceHandle t_IgclTempDevice = nullptr;
    thread_local bool t_IgclTempUseTelemetry = false;
}

bool TryReadThermalFromIgcl(float& outCelsius)
{
    // One-time initialization
    if (!t_IgclTempReady)
    {
        t_IgclTempReady = true;

        if (t_IgclApi == nullptr)
        {
            HMODULE igcl = LoadLibraryW(kIgclDllName);
            if (igcl == nullptr)
            {
                SetDiagnostic(GAWT_DiagnosticCode_IgclUnavailable, static_cast<HRESULT>(GetLastError()),
                    "IGCL thermal: ControlLib.dll not found");
                return false;
            }
            t_IgclModule = igcl;

            auto initFn = reinterpret_cast<IgclInit_t>(GetProcAddress(igcl, "ctlInit"));
            auto enumDevFn = reinterpret_cast<IgclEnumDevices_t>(GetProcAddress(igcl, "ctlEnumerateDevices"));
            if (!initFn || !enumDevFn)
            {
                SetDiagnostic(GAWT_DiagnosticCode_IgclUnavailable, E_FAIL,
                    "IGCL thermal: missing ctlInit/ctlEnumerateDevices");
                FreeLibrary(igcl);
                t_IgclModule = nullptr;
                return false;
            }

            IgclInitArgs initArgs = {};
            initArgs.Size = sizeof(initArgs);
            initArgs.Version = 0;
            initArgs.AppVersion = kIgclAppVersion;
            initArgs.Flags = kIgclInitFlagUseLevelZero;

            if (initFn(&initArgs, &t_IgclApi) != IGCL_SUCCESS || t_IgclApi == nullptr)
            {
                SetDiagnostic(GAWT_DiagnosticCode_IgclInitFailed, E_FAIL,
                    "IGCL thermal: ctlInit failed");
                FreeLibrary(igcl);
                t_IgclModule = nullptr;
                return false;
            }
        }

        HMODULE igcl = t_IgclModule ? t_IgclModule : GetModuleHandleW(kIgclDllName);
        if (igcl == nullptr)
            return false;

        auto enumDevFn = reinterpret_cast<IgclEnumDevices_t>(GetProcAddress(igcl, "ctlEnumerateDevices"));
        if (!enumDevFn)
            return false;

        uint32_t devCount = 0;
        enumDevFn(t_IgclApi, &devCount, nullptr);
        if (devCount == 0)
            return false;

        std::vector<IgclDeviceHandle> devices(devCount);
        enumDevFn(t_IgclApi, &devCount, devices.data());

        // Try Power Telemetry first (ctlPowerTelemetryGet)
        t_igclPowerTelemetryGet = reinterpret_cast<IgclPowerTelemetryGet_t>(
            GetProcAddress(igcl, "ctlPowerTelemetryGet"));

        if (t_igclPowerTelemetryGet)
        {
            for (uint32_t d = 0; d < devCount; ++d)
            {
                alignas(8) uint8_t buf[kIgclPowerTelemetryFullSize] = {};
                auto* telemetry = reinterpret_cast<IgclPowerTelemetry*>(buf);
                telemetry->Size = kIgclPowerTelemetryFullSize;
                telemetry->Version = 0;

                int result = t_igclPowerTelemetryGet(devices[d], telemetry);
                if (result == IGCL_SUCCESS)
                {
                    auto& item = telemetry->gpuCurrentTemperature;
                    double tempC = 0.0;
                    if (item.type == 8)
                        tempC = static_cast<double>(item.value.datafloat);
                    else
                        tempC = item.value.datadouble;

                    if (item.bSupported && tempC > 0.0 && tempC < 150.0)
                    {
                        t_IgclTempDevice = devices[d];
                        t_IgclTempUseTelemetry = true;
                        break;
                    }
                }
            }
        }

        // Fallback: discrete temperature sensor handles
        if (!t_IgclTempUseTelemetry)
        {
            auto enumTempFn = reinterpret_cast<IgclEnumTempSensors_t>(GetProcAddress(igcl, "ctlEnumTemperatureSensors"));
            auto tempPropFn = reinterpret_cast<IgclTempGetProps_t>(GetProcAddress(igcl, "ctlTemperatureGetProperties"));
            t_igclTempGetState = reinterpret_cast<IgclTempGetState_t>(GetProcAddress(igcl, "ctlTemperatureGetState"));

            if (enumTempFn && tempPropFn && t_igclTempGetState)
            {
                IgclTempHandle bestSensor = nullptr;
                for (uint32_t d = 0; d < devCount && bestSensor == nullptr; ++d)
                {
                    uint32_t tempCount = 0;
                    if (enumTempFn(devices[d], &tempCount, nullptr) != IGCL_SUCCESS || tempCount == 0)
                        continue;

                    std::vector<IgclTempHandle> sensors(tempCount);
                    if (enumTempFn(devices[d], &tempCount, sensors.data()) != IGCL_SUCCESS)
                        continue;

                    IgclTempHandle globalSensor = nullptr;
                    for (uint32_t i = 0; i < tempCount; ++i)
                    {
                        IgclTempProperties tp = {};
                        tp.Size = sizeof(tp);
                        if (tempPropFn(sensors[i], &tp) == IGCL_SUCCESS)
                        {
                            if (tp.type == IGCL_TEMP_GPU) { bestSensor = sensors[i]; break; }
                            if (tp.type == IGCL_TEMP_GLOBAL && globalSensor == nullptr)
                                globalSensor = sensors[i];
                        }
                    }
                    if (bestSensor == nullptr)
                        bestSensor = globalSensor;
                }

                if (bestSensor != nullptr)
                    t_IgclTempSensor = bestSensor;
            }

            if (t_IgclTempSensor == nullptr && !t_IgclTempUseTelemetry)
            {
                SetDiagnostic(GAWT_DiagnosticCode_IgclNoSensors, E_FAIL,
                    "IGCL: no temperature sensors or telemetry available");
                return false;
            }
        }
    }

    // Subsequent calls: read temperature

    // Path A: Power Telemetry
    if (t_IgclTempUseTelemetry && t_igclPowerTelemetryGet && t_IgclTempDevice)
    {
        alignas(8) uint8_t buf[kIgclPowerTelemetryFullSize] = {};
        auto* telemetry = reinterpret_cast<IgclPowerTelemetry*>(buf);
        telemetry->Size = kIgclPowerTelemetryFullSize;
        telemetry->Version = 0;

        if (t_igclPowerTelemetryGet(t_IgclTempDevice, telemetry) == IGCL_SUCCESS
            && telemetry->gpuCurrentTemperature.bSupported)
        {
            auto& item = telemetry->gpuCurrentTemperature;
            double tempC = (item.type == 8) ? static_cast<double>(item.value.datafloat) : item.value.datadouble;
            if (tempC > 0.0 && tempC < 150.0)
            {
                outCelsius = static_cast<float>(tempC);
                return true;
            }
        }
        return false;
    }

    // Path B: Discrete sensor handle
    if (t_IgclTempSensor == nullptr || t_igclTempGetState == nullptr)
        return false;

    double tempC = 0.0;
    if (t_igclTempGetState(t_IgclTempSensor, &tempC) != IGCL_SUCCESS)
        return false;

    if (tempC < 0.0 || tempC > 150.0)
        return false;

    outCelsius = static_cast<float>(tempC);
    return true;
}

bool TryReadThermalFromIntelDptf(float& outCelsius)
{
    return TryReadMaxThermalFromWmi(
        kWmiNamespace,
        kWmiIntelDptfQuery,
        kWmiPropertyCurrentTemperature,
        TemperatureUnitModeTenthsKelvin,
        outCelsius,
        "No usable Intel DPTF thermal zone values in ROOT\\WMI results",
        "intel-dptf-acpi");
}

bool ReadGpuMetricsFromIgcl()
{
    // One-time initialization
    if (!t_IgclReady)
    {
        t_IgclReady = true;

        HMODULE igcl = LoadLibraryW(kIgclDllName);
        if (igcl == nullptr)
            return false;
        t_IgclModule = igcl;

        auto initFn     = reinterpret_cast<IgclInit_t>(GetProcAddress(igcl, "ctlInit"));
        auto enumDevFn  = reinterpret_cast<IgclEnumDevices_t>(GetProcAddress(igcl, "ctlEnumerateDevices"));
        auto enumFreqFn = reinterpret_cast<IgclEnumFreqDomains_t>(GetProcAddress(igcl, "ctlEnumFrequencyDomains"));
        auto freqPropFn = reinterpret_cast<IgclFreqGetProps_t>(GetProcAddress(igcl, "ctlFrequencyGetProperties"));
        t_igclFreqGetState = reinterpret_cast<IgclFreqGetState_t>(GetProcAddress(igcl, "ctlFrequencyGetState"));
        auto enumEngFn  = reinterpret_cast<IgclEnumEngines_t>(GetProcAddress(igcl, "ctlEnumEngineGroups"));
        auto engPropFn  = reinterpret_cast<IgclEngineGetProps_t>(GetProcAddress(igcl, "ctlEngineGetProperties"));
        t_igclEngineGetActivity = reinterpret_cast<IgclEngineGetActivity_t>(GetProcAddress(igcl, "ctlEngineGetActivity"));

        if (!initFn || !enumDevFn || !enumFreqFn || !freqPropFn || !t_igclFreqGetState ||
            !enumEngFn || !engPropFn || !t_igclEngineGetActivity)
        {
            FreeLibrary(igcl);
            t_IgclModule = nullptr;
            return false;
        }

        IgclInitArgs initArgs = {};
        initArgs.Size = sizeof(initArgs);
        initArgs.Version = 0;
        initArgs.AppVersion = kIgclAppVersion;
        initArgs.Flags = kIgclInitFlagUseLevelZero;

        if (initFn(&initArgs, &t_IgclApi) != IGCL_SUCCESS || t_IgclApi == nullptr)
        {
            FreeLibrary(igcl);
            t_IgclModule = nullptr;
            return false;
        }

        uint32_t devCount = 0;
        enumDevFn(t_IgclApi, &devCount, nullptr);
        if (devCount == 0)
            return false;

        std::vector<IgclDeviceHandle> devices(devCount);
        enumDevFn(t_IgclApi, &devCount, devices.data());
        IgclDeviceHandle device = devices[0];

        // Find GPU frequency domain
        uint32_t freqCount = 0;
        enumFreqFn(device, &freqCount, nullptr);
        if (freqCount > 0)
        {
            std::vector<IgclFreqHandle> freqs(freqCount);
            enumFreqFn(device, &freqCount, freqs.data());
            for (uint32_t i = 0; i < freqCount; ++i)
            {
                IgclFreqProperties props = {};
                props.Size = sizeof(props);
                if (freqPropFn(freqs[i], &props) == IGCL_SUCCESS && props.type == IGCL_FREQ_GPU)
                {
                    t_IgclGpuFreq = freqs[i];
                    t_IgclMaxFreqMhz = props.max;
                    break;
                }
            }
        }

        // Find render engine group (or GT fallback)
        uint32_t engCount = 0;
        enumEngFn(device, &engCount, nullptr);
        if (engCount > 0)
        {
            std::vector<IgclEngineHandle> engines(engCount);
            enumEngFn(device, &engCount, engines.data());
            for (uint32_t i = 0; i < engCount; ++i)
            {
                IgclEngineProperties ep = {};
                ep.Size = sizeof(ep);
                if (engPropFn(engines[i], &ep) == IGCL_SUCCESS)
                {
                    if (ep.type == IGCL_ENGINE_RENDER) { t_IgclGtEngine = engines[i]; break; }
                    if (ep.type == IGCL_ENGINE_GT && t_IgclGtEngine == nullptr)
                        t_IgclGtEngine = engines[i];
                }
            }
        }

        if (t_IgclGpuFreq == nullptr && t_IgclGtEngine == nullptr)
            return false;

        // Baseline engine activity snapshot
        if (t_IgclGtEngine != nullptr)
        {
            IgclEngineStats stats = {};
            stats.Size = sizeof(stats);
            if (t_igclEngineGetActivity(t_IgclGtEngine, &stats) == IGCL_SUCCESS)
            {
                t_IgclPrevActiveUs = stats.activeTime;
                t_IgclPrevTimestampUs = stats.timestamp;
            }
        }

        // Set initial clock values
        if (t_IgclGpuFreq != nullptr)
        {
            IgclFreqState fs = {};
            fs.Size = sizeof(fs);
            if (t_igclFreqGetState(t_IgclGpuFreq, &fs) == IGCL_SUCCESS && fs.actual >= 0.0)
            {
                t_LastGpuClockCurrentMhz = static_cast<float>(fs.actual);
                t_LastGpuClockMaxMhz = static_cast<float>(t_IgclMaxFreqMhz);
            }
        }

        t_LastGpuLoad = 0.0f;
        t_LastGpuPowerWatts = 0.0f;
        return true;
    }

    // Subsequent calls: refresh from cached handles
    if (t_IgclApi == nullptr)
        return false;

    bool gotAnything = false;

    if (t_IgclGpuFreq != nullptr)
    {
        IgclFreqState fs = {};
        fs.Size = sizeof(fs);
        if (t_igclFreqGetState(t_IgclGpuFreq, &fs) == IGCL_SUCCESS && fs.actual >= 0.0)
        {
            t_LastGpuClockCurrentMhz = static_cast<float>(fs.actual);
            t_LastGpuClockMaxMhz = static_cast<float>(t_IgclMaxFreqMhz);
            gotAnything = true;
        }
    }

    if (t_IgclGtEngine != nullptr)
    {
        IgclEngineStats stats = {};
        stats.Size = sizeof(stats);
        if (t_igclEngineGetActivity(t_IgclGtEngine, &stats) == IGCL_SUCCESS)
        {
            if (t_IgclPrevTimestampUs > 0 && stats.timestamp > t_IgclPrevTimestampUs)
            {
                double deltaActive = static_cast<double>(stats.activeTime - t_IgclPrevActiveUs);
                double deltaTime   = static_cast<double>(stats.timestamp - t_IgclPrevTimestampUs);
                double util = (deltaActive / deltaTime) * 100.0;
                if (util > 100.0) util = 100.0;
                if (util < 0.0) util = 0.0;
                t_LastGpuLoad = static_cast<float>(util);
                gotAnything = true;
            }
            t_IgclPrevActiveUs = stats.activeTime;
            t_IgclPrevTimestampUs = stats.timestamp;
        }
    }

    t_LastGpuPowerWatts = 0.0f;
    return gotAnything;
}
