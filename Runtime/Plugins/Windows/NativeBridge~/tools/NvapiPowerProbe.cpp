// NvapiPowerProbe - Diagnostic tool to identify the correct NVAPI power interface ID
// Tries known/suspected power-related interface IDs and reports results.
// Compare output against: nvidia-smi --query-gpu=power.draw --format=csv

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef int NvAPI_Status;
typedef void* NvPhysicalGpuHandle;
typedef void* (__cdecl* NvAPI_QueryInterfaceFn)(unsigned int);
typedef NvAPI_Status (__cdecl* NvAPI_InitializeFn)();
typedef NvAPI_Status (__cdecl* NvAPI_UnloadFn)();
typedef NvAPI_Status (__cdecl* NvAPI_EnumPhysicalGpusFn)(NvPhysicalGpuHandle handles[64], int* count);

static const int kMaxPhysicalGpus = 64;

struct ProbeCandidate
{
    unsigned int interfaceId;
    const char* name;
    const char* description;
};

static const ProbeCandidate kCandidates[] =
{
    { 0xEDCF624E, "ClientPowerTopologyGetStatus",  "Real-time power topology (LibreHardwareMonitor)" },
    { 0x70916171, "GetPowerPoliciesStatus",         "Power policy / limits" },
    { 0x3D82D537, "GetPowerUsage (original)",       "Original ID tried by Codex" },
    { 0x927DA4F6, "GetCurrentPstate",               "Current performance state" },
    { 0x6FF81213, "GetPstates20",                   "P-state table with TDP info" },
    { 0xB7B2ABD5, "ClientPowerPoliciesGetStatus",   "Client power policies" },
    { 0x34206D86, "ClientPowerPoliciesGetInfo",     "Client power policy info" },
    { 0x2DDFB66E, "GetMemoryInfo",                  "Memory info (control reference)" },
    { 0x60DED2ED, "GetPerfDecreaseInfo",            "Performance decrease info" },
};

static const int kCandidateCount = sizeof(kCandidates) / sizeof(kCandidates[0]);

// ClientPowerTopologyGetStatus structure (from LibreHardwareMonitor reverse engineering)
// Version 1: header (8 bytes) + entries
struct NV_POWER_TOPOLOGY_ENTRY
{
    unsigned int domain;        // power rail domain
    unsigned int unknown1;
    unsigned int unknown2;
    unsigned int powerUsage;    // power in mW
};

struct NV_POWER_TOPOLOGY_STATUS_V1
{
    unsigned int version;
    unsigned int count;
    NV_POWER_TOPOLOGY_ENTRY entries[4];
};

// Power policies status structure
struct NV_GPU_POWER_STATUS_ENTRY
{
    unsigned int unknown1;
    unsigned int unknown2;
    unsigned int power;       // could be mW or percentage
    unsigned int unknown3;
};

struct NV_GPU_POWER_POLICIES_STATUS
{
    unsigned int version;
    unsigned int count;
    NV_GPU_POWER_STATUS_ENTRY entries[4];
};

typedef NvAPI_Status (__cdecl* GenericGpuFn)(NvPhysicalGpuHandle handle, void* data);
typedef NvAPI_Status (__cdecl* GenericGpuFn2)(NvPhysicalGpuHandle handle, unsigned int param, void* data);
typedef NvAPI_Status (__cdecl* GenericGpuFn3)(NvPhysicalGpuHandle handle, void* data1, void* data2);

int main()
{
    printf("=== NVAPI Power Interface Probe ===\n\n");

    // Get nvidia-smi reference value
    printf("--- Reference: nvidia-smi power reading ---\n");
    FILE* pipe = _popen("nvidia-smi --query-gpu=power.draw --format=csv,noheader,nounits 2>NUL", "r");
    float smiPowerW = 0.0f;
    if (pipe != nullptr)
    {
        char buf[128] = {};
        if (fgets(buf, sizeof(buf), pipe) != nullptr)
            smiPowerW = static_cast<float>(atof(buf));
        _pclose(pipe);
    }
    if (smiPowerW > 0.0f)
        printf("nvidia-smi reports: %.2f W\n\n", smiPowerW);
    else
        printf("nvidia-smi not available or returned 0\n\n");

    // Load NVAPI
    HMODULE nvapiModule = LoadLibraryW(L"nvapi64.dll");
    if (nvapiModule == nullptr)
    {
        printf("ERROR: Failed to load nvapi64.dll\n");
        return 1;
    }

    auto queryInterface = reinterpret_cast<NvAPI_QueryInterfaceFn>(GetProcAddress(nvapiModule, "nvapi_QueryInterface"));
    if (queryInterface == nullptr)
    {
        printf("ERROR: nvapi_QueryInterface not found\n");
        FreeLibrary(nvapiModule);
        return 1;
    }

    auto nvInitialize = reinterpret_cast<NvAPI_InitializeFn>(queryInterface(0x0150E828));
    auto nvUnload = reinterpret_cast<NvAPI_UnloadFn>(queryInterface(0xD22BDD7E));
    auto nvEnumGpus = reinterpret_cast<NvAPI_EnumPhysicalGpusFn>(queryInterface(0xE5AC921F));

    if (nvInitialize == nullptr || nvEnumGpus == nullptr)
    {
        printf("ERROR: Core NVAPI interfaces unavailable\n");
        FreeLibrary(nvapiModule);
        return 1;
    }

    if (nvInitialize() != 0)
    {
        printf("ERROR: NvAPI_Initialize failed\n");
        FreeLibrary(nvapiModule);
        return 1;
    }

    NvPhysicalGpuHandle gpuHandles[kMaxPhysicalGpus] = {};
    int gpuCount = 0;
    if (nvEnumGpus(gpuHandles, &gpuCount) != 0 || gpuCount <= 0)
    {
        printf("ERROR: No GPUs enumerated\n");
        if (nvUnload) nvUnload();
        FreeLibrary(nvapiModule);
        return 1;
    }

    printf("GPUs found: %d\n\n", gpuCount);

    // Phase 1: Check which interface IDs resolve to non-null
    printf("--- Phase 1: Interface resolution ---\n");
    for (int c = 0; c < kCandidateCount; ++c)
    {
        void* fn = queryInterface(kCandidates[c].interfaceId);
        printf("  0x%08X %-38s -> %s\n",
            kCandidates[c].interfaceId,
            kCandidates[c].name,
            fn != nullptr ? "RESOLVED" : "null");
    }

    printf("\n--- Phase 2: Attempting calls on resolved interfaces ---\n\n");

    // Try ClientPowerTopologyGetStatus (0xEDCF624E)
    // Structure is: version(4) + count(4) + entries[4] where each entry is 16 bytes
    // Total = 72 bytes. Some APIs require pre-filling domain IDs.
    {
        auto fn = reinterpret_cast<GenericGpuFn>(queryInterface(0xEDCF624E));
        if (fn != nullptr)
        {
            printf(">> ClientPowerTopologyGetStatus (0xEDCF624E)\n");

            // Basic call
            unsigned char buf[256] = {};
            *reinterpret_cast<unsigned int*>(buf) = 72u | (1u << 16);
            NvAPI_Status status = fn(gpuHandles[0], buf);
            printf("   plain 72|v1 -> status=%d, all zeros=%s\n", status,
                (memcmp(buf + 4, buf + 4, 68) == 0) ? "checking..." : "no");
            {
                unsigned int* d = reinterpret_cast<unsigned int*>(buf);
                bool allZeroAfterVer = true;
                for (int i = 1; i < 18; ++i) if (d[i] != 0) { allZeroAfterVer = false; break; }
                printf("   data present: %s\n", allZeroAfterVer ? "NO (all zero)" : "YES");
                if (!allZeroAfterVer)
                {
                    printf("   raw: ");
                    for (int i = 0; i < 18; ++i) printf("%u ", d[i]);
                    printf("\n");
                }
            }

            // Try pre-filling count=1, domain=0 (total power)
            memset(buf, 0, sizeof(buf));
            *reinterpret_cast<unsigned int*>(buf) = 72u | (1u << 16);  // version
            *reinterpret_cast<unsigned int*>(buf + 4) = 1;              // count=1
            *reinterpret_cast<unsigned int*>(buf + 8) = 0;              // domain=0
            status = fn(gpuHandles[0], buf);
            printf("   count=1,domain=0 -> status=%d\n", status);
            if (status == 0)
            {
                unsigned int* d = reinterpret_cast<unsigned int*>(buf);
                printf("   raw: ");
                for (int i = 0; i < 18; ++i) printf("%u ", d[i]);
                printf("\n");
            }

            // Try count=1 with various domain values
            for (unsigned int dom = 0; dom <= 8; ++dom)
            {
                memset(buf, 0, sizeof(buf));
                *reinterpret_cast<unsigned int*>(buf) = 72u | (1u << 16);
                *reinterpret_cast<unsigned int*>(buf + 4) = 1;
                *reinterpret_cast<unsigned int*>(buf + 8) = dom;
                status = fn(gpuHandles[0], buf);
                if (status == 0)
                {
                    unsigned int powerVal = *reinterpret_cast<unsigned int*>(buf + 20);  // offset 20 = entry[0].powerUsage
                    unsigned int alt1 = *reinterpret_cast<unsigned int*>(buf + 12);
                    unsigned int alt2 = *reinterpret_cast<unsigned int*>(buf + 16);
                    if (powerVal != 0 || alt1 != 0 || alt2 != 0)
                    {
                        printf("   domain=%u: [8]=%u [12]=%u [16]=%u [20]=%u\n",
                            dom,
                            *reinterpret_cast<unsigned int*>(buf + 8),
                            alt1, alt2, powerVal);
                    }
                }
            }

            printf("\n");
        }
    }

    // Try GetPowerPoliciesStatus (0x70916171) — policies (max TDP info)
    {
        auto fn = reinterpret_cast<GenericGpuFn>(queryInterface(0x70916171));
        if (fn != nullptr)
        {
            printf(">> GetPowerPoliciesStatus (0x70916171)\n");

            unsigned char buf[256] = {};
            *reinterpret_cast<unsigned int*>(buf) = 72u | (1u << 16);
            NvAPI_Status status = fn(gpuHandles[0], buf);
            printf("   72|v1 -> status=%d\n", status);
            unsigned int* d = reinterpret_cast<unsigned int*>(buf);
            bool hasData = false;
            for (int i = 1; i < 18; ++i) if (d[i] != 0) { hasData = true; break; }
            if (hasData)
            {
                printf("   raw: ");
                for (int i = 0; i < 18; ++i) printf("%u ", d[i]);
                printf("\n");
            }
            else
                printf("   all zero\n");

            // Try with count pre-set to 1
            memset(buf, 0, sizeof(buf));
            *reinterpret_cast<unsigned int*>(buf) = 72u | (1u << 16);
            *reinterpret_cast<unsigned int*>(buf + 4) = 1;
            status = fn(gpuHandles[0], buf);
            printf("   count=1 -> status=%d\n", status);
            if (status == 0)
            {
                d = reinterpret_cast<unsigned int*>(buf);
                hasData = false;
                for (int i = 1; i < 18; ++i) if (d[i] != 0) { hasData = true; break; }
                if (hasData)
                {
                    printf("   raw: ");
                    for (int i = 0; i < 18; ++i) printf("%u ", d[i]);
                    printf("\n");
                }
            }
            printf("\n");
        }
    }

    // Try NVML as fallback for power (nvidia-smi uses this)
    {
        printf(">> NVML (nvml.dll) power check\n");
        HMODULE nvmlModule = LoadLibraryW(L"nvml.dll");
        if (nvmlModule == nullptr)
        {
            // Try common install path
            nvmlModule = LoadLibraryW(L"C:\\Windows\\System32\\nvml.dll");
        }
        if (nvmlModule != nullptr)
        {
            typedef int (*nvmlInit_t)();
            typedef int (*nvmlShutdown_t)();
            typedef int (*nvmlDeviceGetHandleByIndex_t)(unsigned int, void**);
            typedef int (*nvmlDeviceGetPowerUsage_t)(void*, unsigned int*);
            typedef int (*nvmlDeviceGetEnforcedPowerLimit_t)(void*, unsigned int*);

            auto nvmlInit = reinterpret_cast<nvmlInit_t>(GetProcAddress(nvmlModule, "nvmlInit_v2"));
            auto nvmlShutdown = reinterpret_cast<nvmlShutdown_t>(GetProcAddress(nvmlModule, "nvmlShutdown"));
            auto nvmlGetHandle = reinterpret_cast<nvmlDeviceGetHandleByIndex_t>(GetProcAddress(nvmlModule, "nvmlDeviceGetHandleByIndex_v2"));
            auto nvmlGetPower = reinterpret_cast<nvmlDeviceGetPowerUsage_t>(GetProcAddress(nvmlModule, "nvmlDeviceGetPowerUsage"));
            auto nvmlGetPowerLimit = reinterpret_cast<nvmlDeviceGetEnforcedPowerLimit_t>(GetProcAddress(nvmlModule, "nvmlDeviceGetEnforcedPowerLimit"));

            if (nvmlInit == nullptr)
                nvmlInit = reinterpret_cast<nvmlInit_t>(GetProcAddress(nvmlModule, "nvmlInit"));
            if (nvmlGetHandle == nullptr)
                nvmlGetHandle = reinterpret_cast<nvmlDeviceGetHandleByIndex_t>(GetProcAddress(nvmlModule, "nvmlDeviceGetHandleByIndex"));

            printf("   nvmlInit=%s, nvmlGetHandle=%s, nvmlGetPower=%s, nvmlGetPowerLimit=%s\n",
                nvmlInit ? "yes" : "no",
                nvmlGetHandle ? "yes" : "no",
                nvmlGetPower ? "yes" : "no",
                nvmlGetPowerLimit ? "yes" : "no");

            if (nvmlInit != nullptr && nvmlGetHandle != nullptr && nvmlGetPower != nullptr)
            {
                int initResult = nvmlInit();
                printf("   nvmlInit -> %d\n", initResult);

                if (initResult == 0)
                {
                    void* device = nullptr;
                    int handleResult = nvmlGetHandle(0, &device);
                    printf("   nvmlDeviceGetHandleByIndex(0) -> %d\n", handleResult);

                    if (handleResult == 0 && device != nullptr)
                    {
                        unsigned int powerMw = 0;
                        int powerResult = nvmlGetPower(device, &powerMw);
                        printf("   nvmlDeviceGetPowerUsage -> %d, power=%u mW (%.2f W)\n",
                            powerResult, powerMw, static_cast<float>(powerMw) / 1000.0f);

                        if (nvmlGetPowerLimit != nullptr)
                        {
                            unsigned int limitMw = 0;
                            int limitResult = nvmlGetPowerLimit(device, &limitMw);
                            printf("   nvmlDeviceGetEnforcedPowerLimit -> %d, limit=%u mW (%.2f W)\n",
                                limitResult, limitMw, static_cast<float>(limitMw) / 1000.0f);
                        }
                    }

                    if (nvmlShutdown) nvmlShutdown();
                }
            }

            FreeLibrary(nvmlModule);
        }
        else
        {
            printf("   nvml.dll not found\n");
        }
        printf("\n");
    }

    // Try original 0x3D82D537
    {
        auto fn = reinterpret_cast<GenericGpuFn>(queryInterface(0x3D82D537));
        if (fn != nullptr)
        {
            printf(">> GetPowerUsage original (0x3D82D537)\n");
            unsigned char buf[256] = {};
            for (unsigned int ver = 1; ver <= 2; ++ver)
            {
                memset(buf, 0, sizeof(buf));
                *reinterpret_cast<unsigned int*>(buf) = 256u | (ver << 16);

                NvAPI_Status status = fn(gpuHandles[0], buf);
                printf("   ver=%u -> status=%d\n", ver, status);
                if (status == 0)
                {
                    printf("     first 8 DWORDs: ");
                    unsigned int* d = reinterpret_cast<unsigned int*>(buf);
                    for (int i = 0; i < 8; ++i)
                        printf("%u ", d[i]);
                    printf("\n");
                }
            }
            printf("\n");
        }
    }

    // Try ClientPowerPoliciesGetStatus (0xB7B2ABD5)
    {
        auto fn = reinterpret_cast<GenericGpuFn>(queryInterface(0xB7B2ABD5));
        if (fn != nullptr)
        {
            printf(">> ClientPowerPoliciesGetStatus (0xB7B2ABD5)\n");
            unsigned char buf[512] = {};
            for (unsigned int ver = 1; ver <= 2; ++ver)
            {
                memset(buf, 0, sizeof(buf));
                *reinterpret_cast<unsigned int*>(buf) = 512u | (ver << 16);

                NvAPI_Status status = fn(gpuHandles[0], buf);
                printf("   ver=%u -> status=%d\n", ver, status);
                if (status == 0)
                {
                    printf("     first 12 DWORDs: ");
                    unsigned int* d = reinterpret_cast<unsigned int*>(buf);
                    for (int i = 0; i < 12; ++i)
                        printf("%u ", d[i]);
                    printf("\n");
                }
            }
            printf("\n");
        }
    }

    // Try GetPerfDecreaseInfo (0x60DED2ED)
    {
        auto fn = reinterpret_cast<GenericGpuFn>(queryInterface(0x60DED2ED));
        if (fn != nullptr)
        {
            printf(">> GetPerfDecreaseInfo (0x60DED2ED)\n");
            unsigned int flags = 0;
            NvAPI_Status status = fn(gpuHandles[0], &flags);
            printf("   status=%d, flags=0x%08X\n", status, flags);
            printf("\n");
        }
    }

    // Summary
    printf("--- Summary ---\n");
    if (smiPowerW > 0.0f)
        printf("Look for a value near %.0f mW (%.2f W) in the output above.\n", smiPowerW * 1000.0f, smiPowerW);
    else
        printf("Run 'nvidia-smi --query-gpu=power.draw --format=csv' separately to get a reference value.\n");

    if (nvUnload) nvUnload();
    FreeLibrary(nvapiModule);
    return 0;
}
