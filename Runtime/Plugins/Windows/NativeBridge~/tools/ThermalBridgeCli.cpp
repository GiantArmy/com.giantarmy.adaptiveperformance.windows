#include <cstdlib>
#include <iostream>
#include <cstring>
#include <windows.h>
#include "GiantArmyWindowsThermalBridge.h"

static void PrintDiagnostics()
{
    std::cout << "Diagnostic code: " << GAWT_GetLastDiagnosticCode() << std::endl;
    std::cout << "HRESULT: 0x" << std::hex << static_cast<unsigned int>(GAWT_GetLastHResult()) << std::dec << std::endl;
    const char* message = GAWT_GetLastDiagnosticMessage();
    std::cout << "Message: " << (message != nullptr ? message : "(null)") << std::endl;
    const char* trace = GAWT_GetLastAttemptTrace();
    std::cout << "Attempt trace: " << (trace != nullptr && trace[0] != '\0' ? trace : "(empty)") << std::endl;
}

static const char* ClassifyThermalSource(const char* source)
{
    if (source == nullptr || strlen(source) == 0 || strcmp(source, "none") == 0)
        return "unavailable";
    
    if (strncmp(source, "nvapi", 5) == 0 || 
        strncmp(source, "adl", 3) == 0 || 
        strncmp(source, "intel", 5) == 0)
        return "vendor-gpu";
    
    return "ambient-fallback";
}

static const char* GetThresholdLevel(float celsius, const char* source, const char* classification)
{
    if (classification == nullptr || strcmp(classification, "unavailable") == 0)
        return "unavailable";
    
    if (source != nullptr && strncmp(source, "nvapi", 5) == 0)
    {
        if (celsius >= 90.0f)
            return "Throttling";
        if (celsius >= 86.0f)
            return "Warning Imminent";
        if (celsius >= 70.0f)
            return "Medium-Heavy Load";
        if (celsius >= 50.0f)
            return "Warm";
        return "Idle-Light";
    }

    if (source != nullptr && strncmp(source, "adl", 3) == 0)
    {
        if (celsius >= 110.0f)
            return "Throttling";
        if (celsius >= 95.0f)
            return "Junction/Hotspot";
        if (celsius >= 88.0f)
            return "Maximum Safe (Edge)";
        if (celsius >= 70.0f)
            return "Heavy Load (Gaming)";
        if (celsius >= 50.0f)
            return "Light Load";
        return "Idle";
    }

    if (strcmp(classification, "vendor-gpu") == 0)
    {
        if (celsius >= 85.0f)
            return "Throttling";
        if (celsius >= 75.0f)
            return "Warning Imminent";
        if (celsius >= 60.0f)
            return "Hot";
        if (celsius >= 50.0f)
            return "Warm";
        return "Cool";
    }
    
    // ambient-fallback
    if (celsius >= 95.0f)
        return "Throttling";
    if (celsius >= 85.0f)
        return "Warning Imminent";
    if (celsius >= 65.0f)
        return "Hot";
    if (celsius >= 50.0f)
        return "Warm";
    return "Cool";
}

int main()
{
    std::cout << "Thermal source order: NVAPI -> ADL (AMD) -> Intel DPTF ACPI -> IGCL GPU sensor -> PDH Thermal Zone -> CIMV2 perf counters -> WMI ACPI zone -> CallNtPowerInformation" << std::endl;

    const int available = GAWT_IsThermalApiAvailable();
    std::cout << "Bridge availability: " << (available != 0 ? "available" : "unavailable") << std::endl;
    if (available == 0)
        PrintDiagnostics();

    float celsius = 0.0f;
    const int gotTemp = GAWT_TryGetThermalCelsius(&celsius);
    const char* trace = GAWT_GetLastAttemptTrace();
    std::cout << "Attempt trace: " << (trace != nullptr && trace[0] != '\0' ? trace : "(empty)") << std::endl;

    if (gotTemp != 0)
    {
        const char* source = GAWT_GetLastSourceName();
        const char* classification = ClassifyThermalSource(source);
        const char* thresholdLevel = GetThresholdLevel(celsius, source, classification);
        std::cout << "Thermal source: " << (source != nullptr ? source : "none") << std::endl;
        std::cout << "Thermal classification: " << classification << std::endl;
        std::cout << "Thermal reading (C): " << celsius << std::endl;
        std::cout << "Threshold level: " << thresholdLevel << std::endl;
    }
    else
    {
        std::cout << "Thermal reading unavailable." << std::endl;
        PrintDiagnostics();
    }

    // GPU Metrics
    std::cout << std::endl << "=== GPU Metrics ===" << std::endl;
    float gpuLoad = 0.0f;
    float gpuClockCur = 0.0f, gpuClockMax = 0.0f;

    float gpuPower = 0.0f;

    // First call probes and establishes baseline (rate counters return 0 initially)
    int gotGpuLoad = GAWT_TryGetGpuLoad(&gpuLoad);
    int gotGpuClocks = GAWT_TryGetGpuClocks(&gpuClockCur, &gpuClockMax);
    int gotGpuPower = GAWT_TryGetGpuPowerUsage(&gpuPower);
    const char* gpuSource = GAWT_GetLastGpuMetricsSource();

    // For delta-based sources (PDH/IGCL), a second collect after a short delay is needed for real data
    if (gpuSource != nullptr && gotGpuLoad != 0 &&
        (std::strncmp(gpuSource, "pdh", 3) == 0 || std::strncmp(gpuSource, "igcl", 4) == 0))
    {
        Sleep(200);
        gotGpuLoad = GAWT_TryGetGpuLoad(&gpuLoad);
        gotGpuClocks = GAWT_TryGetGpuClocks(&gpuClockCur, &gpuClockMax);
        gotGpuPower = GAWT_TryGetGpuPowerUsage(&gpuPower);
    }

    std::cout << "GPU metrics source: " << (gpuSource != nullptr ? gpuSource : "unavailable") << std::endl;

    if (gotGpuLoad != 0)
        std::cout << "GPU load: " << gpuLoad << "%" << std::endl;

    if (gotGpuClocks != 0 && gpuClockCur > 0.0f)
        std::cout << "GPU clock (current): " << gpuClockCur << " MHz, (max): " << gpuClockMax << " MHz" << std::endl;

    if (gotGpuPower != 0 && gpuPower > 0.0f)
        std::cout << "GPU power: " << gpuPower << " W" << std::endl;

    if (gotGpuLoad == 0 && gotGpuClocks == 0 && gotGpuPower == 0)
        std::cout << "GPU metrics unavailable." << std::endl;

    return EXIT_SUCCESS;
}
