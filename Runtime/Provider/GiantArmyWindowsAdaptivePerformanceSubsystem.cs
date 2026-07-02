using System;
using System.Collections;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Reflection;
using System.Threading;
using UnityEngine.AdaptivePerformance.Provider;
using UnityEngine.Scripting;
using GiantArmy.Windows;
using static UnityEngine.AdaptivePerformance.GiantArmy.Windows.GiantArmyWindowsAdaptivePerformanceSubsystem.GiantArmyWindowsAdaptivePerformanceSubsystemProvider;

namespace UnityEngine.AdaptivePerformance.GiantArmy.Windows
{
    public struct WindowsProviderDiagnosticSnapshot
    {
        public bool thermalProbeEnabled;
        public bool thermalAvailable;
        public bool lowPowerModeEnabled;
        public float temperatureCelsius;
        public float temperatureLevel;
        public WarningLevel warningLevel;
        public PerformanceMode performanceMode;
        public string thermalSource;

        // GPU metrics
        public bool gpuMetricsAvailable;
        public string gpuMetricsSource;
        public float gpuLoadPercent;
        public float gpuClockCurrentMhz;
        public float gpuClockMaxMhz;
        public float gpuPowerWatts;
        public int gpuPerformanceLevel;

        // System memory (OS-wide, across all processes — from GlobalMemoryStatusEx)
        public bool systemMemoryAvailable;
        public ulong systemTotalPhysicalBytes;
        public ulong systemAvailablePhysicalBytes;
        public float systemMemoryLoadPercent;
    }

    internal static class WindowsProviderLog
    {
        public static void Debug(string format, params object[] args)
        {
            var settings = GiantArmyWindowsProviderSettings.GetSettings();
            if (settings != null && settings.windowsProviderLogging)
                UnityEngine.Debug.Log(string.Format("[AP Windows] " + format, args));
        }
    }

    [Preserve]
    public class GiantArmyWindowsAdaptivePerformanceSubsystem : AdaptivePerformanceSubsystem
    {
        public static bool TryGetDiagnosticSnapshot(out WindowsProviderDiagnosticSnapshot snapshot)
        {
            snapshot = new WindowsProviderDiagnosticSnapshot();

            var settings = GiantArmyWindowsProviderSettings.GetSettings();
            snapshot.thermalProbeEnabled = settings == null || settings.windowsWmiThermalProbeEnabled;
            snapshot.thermalAvailable = snapshot.thermalProbeEnabled && NativeApi.IsThermalApiAvailable();

            if (NativeApi.TryGetLowPowerModeEnabled(out var lowPowerModeEnabled))
                snapshot.lowPowerModeEnabled = lowPowerModeEnabled;

            snapshot.performanceMode = snapshot.lowPowerModeEnabled
                ? PerformanceMode.Battery
                : Application.targetFrameRate >= 60
                    ? PerformanceMode.Optimize
                    : PerformanceMode.Standard;

            if (snapshot.thermalAvailable)
            {
                snapshot.temperatureCelsius = NativeApi.GetLastThermalCelsius();

                if (NativeApi.TryGetThermalLevel(out var temperatureLevel))
                    snapshot.temperatureLevel = temperatureLevel;

                snapshot.warningLevel = NativeApi.GetThermalWarningLevel();
                    snapshot.thermalSource = NativeApi.GetLastThermalSourceName();
            }
            else
            {
                snapshot.temperatureCelsius = 0.0f;
                snapshot.temperatureLevel = 0.0f;
                snapshot.warningLevel = WarningLevel.NoWarning;
                    snapshot.thermalSource = snapshot.thermalProbeEnabled
                        ? NativeApi.GetUnavailableThermalDiagnosticSummary()
                        : "Disabled";
            }

            // GPU metrics
            snapshot.gpuMetricsSource = NativeApi.GetLastGpuMetricsSource();
            snapshot.gpuMetricsAvailable = !string.IsNullOrEmpty(snapshot.gpuMetricsSource)
                && snapshot.gpuMetricsSource != "unavailable"
                && snapshot.gpuMetricsSource != "none";

            if (snapshot.gpuMetricsAvailable)
            {
                NativeApi.TryGetGpuLoad(out snapshot.gpuLoadPercent);
                NativeApi.TryGetGpuClocks(out snapshot.gpuClockCurrentMhz, out snapshot.gpuClockMaxMhz);
                NativeApi.TryGetGpuPowerUsage(out snapshot.gpuPowerWatts);

                if (snapshot.gpuClockMaxMhz > 0f)
                    snapshot.gpuPerformanceLevel = Mathf.RoundToInt(Mathf.Clamp01(snapshot.gpuClockCurrentMhz / snapshot.gpuClockMaxMhz) * 6);
            }

            // System memory
            snapshot.systemMemoryAvailable = NativeApi.TryGetSystemMemoryStatus(
                out snapshot.systemTotalPhysicalBytes,
                out snapshot.systemAvailablePhysicalBytes,
                out var memoryLoadPercent);
            snapshot.systemMemoryLoadPercent = memoryLoadPercent;

            return true;
        }

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        static AdaptivePerformanceSubsystemDescriptor RegisterDescriptor()
        {
            return AdaptivePerformanceSubsystemDescriptor.RegisterDescriptor(new AdaptivePerformanceSubsystemDescriptor.Cinfo
            {
                id = "GiantArmyWindows",
                providerType = typeof(GiantArmyWindowsAdaptivePerformanceSubsystemProvider),
                subsystemTypeOverride = typeof(GiantArmyWindowsAdaptivePerformanceSubsystem)
            });
        }

        public class GiantArmyWindowsAdaptivePerformanceSubsystemProvider : APProvider, IApplicationLifecycle, IDevicePerformanceLevelControl
        {
            readonly object m_DataLock = new object();
            PerformanceDataRecord m_Data = new PerformanceDataRecord();

            Version m_Version = new Version(18, 0, 0);
            PerformanceMode m_PerformanceMode = PerformanceMode.Unknown;
            float m_Temperature = 0.0f;
            WarningLevel m_WarningLevel = WarningLevel.NoWarning;
            bool m_ThermalListenerActive;
            bool m_LowPowerListenerActive;
            bool m_LowPowerModeEnabled;
            int m_GpuPerformanceLevel = Constants.UnknownPerformanceLevel;
            bool m_GpuMetricsAvailable;

            const int k_MaxGpuLevel = 6;

            public override IApplicationLifecycle ApplicationLifecycle => this;
            public override IDevicePerformanceLevelControl PerformanceLevelControl => this;

            public int MaxCpuPerformanceLevel { get; set; }
            public int MaxGpuPerformanceLevel { get; set; }

            public override Feature Capabilities { get; set; }
            public override bool Initialized { get; set; }
            public override Version Version => m_Version;
            public override string Stats => $"Temp={m_Temperature} PerformanceMode={m_PerformanceMode} GpuLevel={m_GpuPerformanceLevel}";

            protected override bool TryInitialize()
            {
                if (Initialized)
                    return true;

                if (!base.TryInitialize())
                    return false;

                MaxCpuPerformanceLevel = -1;
                MaxGpuPerformanceLevel = -1;
                Capabilities = Feature.PerformanceMode;

                if (NativeApi.IsThermalApiAvailable())
                    Capabilities |= Feature.WarningLevel | Feature.TemperatureLevel;

                // Probe GPU metrics to determine availability
                NativeApi.TryGetGpuLoad(out _);
                string gpuSource = NativeApi.GetLastGpuMetricsSource();
                m_GpuMetricsAvailable = !string.IsNullOrEmpty(gpuSource)
                    && gpuSource != "unavailable" && gpuSource != "none";

                if (m_GpuMetricsAvailable)
                {
                    MaxGpuPerformanceLevel = k_MaxGpuLevel;
                    Capabilities |= Feature.GpuPerformanceLevel;
                }

                m_Data.PerformanceLevelControlAvailable = false;
                Initialized = true;
                return true;
            }

            public override void Start()
            {
                if (!Initialized || m_Running)
                    return;

                if (Capabilities.HasFlag(Feature.WarningLevel) || Capabilities.HasFlag(Feature.TemperatureLevel))
                    m_ThermalListenerActive = NativeApi.SetupThermalListener();

                m_LowPowerListenerActive = NativeApi.SetupPowerStateListener();

                ImmediateRefresh(true);

                if (TryGetDiagnosticSnapshot(out var snapshot))
                {
                    UnityEngine.Debug.Log(string.Format(
                        "[AP Windows] Startup diagnostics: thermalAvailable={0}, thermalSource={1}, temperatureLevel={2:0.00}, warningLevel={3}, lowPowerModeEnabled={4}, performanceMode={5}",
                        snapshot.thermalAvailable,
                        snapshot.thermalSource,
                        snapshot.temperatureLevel,
                        snapshot.warningLevel,
                        snapshot.lowPowerModeEnabled,
                        snapshot.performanceMode));

                    if (snapshot.thermalAvailable)
                    {
                        var profile = NativeApi.GetCalibrationProfileForSource(snapshot.thermalSource);
                        UnityEngine.Debug.Log(string.Format(
                            "[AP Windows] Thermal profile active: source={0}, noWarningUpper={1:0.0}C, imminentThreshold={2:0.0}C, throttlingThreshold={3:0.0}C",
                            snapshot.thermalSource,
                            profile.noWarningUpperBound,
                            profile.imminentThreshold,
                            profile.throttlingThreshold));
                    }
                }

                m_Running = true;
            }

            public override void Stop()
            {
                if (m_ThermalListenerActive)
                {
                    NativeApi.TeardownThermalListener();
                    m_ThermalListenerActive = false;
                }

                if (m_LowPowerListenerActive)
                {
                    NativeApi.TeardownPowerStateListener();
                    m_LowPowerListenerActive = false;
                }

                m_Running = false;
            }

            public override void Destroy()
            {
                if (m_Running)
                    Stop();

                Initialized = false;
            }

            public override PerformanceDataRecord Update()
            {
                if (!m_Running)
                    return m_Data;

                ImmediateRefresh(false);

                lock (m_DataLock)
                {
                    PerformanceDataRecord result = m_Data;
                    m_Data.ChangeFlags = Feature.None;
                    return result;
                }
            }

            void ImmediateRefresh(bool forceThermalUpdate)
            {
                bool thermalChanged = forceThermalUpdate;
                if (!thermalChanged && m_ThermalListenerActive)
                    thermalChanged = NativeApi.ConsumeThermalStateDirty();

                bool powerChanged = forceThermalUpdate;
                if (!powerChanged && m_LowPowerListenerActive)
                    powerChanged = NativeApi.ConsumeLowPowerStateDirty();

                if (powerChanged)
                {
                    if (!NativeApi.TryGetLowPowerModeEnabled(out m_LowPowerModeEnabled))
                        m_LowPowerModeEnabled = false;
                }

                if (thermalChanged && Capabilities.HasFlag(Feature.WarningLevel))
                    m_WarningLevel = NativeApi.GetThermalWarningLevel();

                if (thermalChanged && Capabilities.HasFlag(Feature.TemperatureLevel))
                {
                    if (NativeApi.TryGetThermalLevel(out var thermalLevel))
                        m_Temperature = thermalLevel;
                }

                // GPU performance level: derive from clock ratio
                int previousGpuLevel = m_GpuPerformanceLevel;
                if (m_GpuMetricsAvailable && Capabilities.HasFlag(Feature.GpuPerformanceLevel))
                {
                    if (NativeApi.TryGetGpuClocks(out float curMhz, out float maxMhz) && maxMhz > 0f)
                    {
                        float ratio = Mathf.Clamp01(curMhz / maxMhz);
                        m_GpuPerformanceLevel = Mathf.RoundToInt(ratio * k_MaxGpuLevel);
                    }
                }

                var previousMode = m_PerformanceMode;
                if (m_LowPowerModeEnabled)
                    m_PerformanceMode = PerformanceMode.Battery;
                else if (Application.targetFrameRate >= 60)
                    m_PerformanceMode = PerformanceMode.Optimize;
                else
                    m_PerformanceMode = PerformanceMode.Standard;

                lock (m_DataLock)
                {
                    if (previousMode != m_PerformanceMode)
                    {
                        m_Data.ChangeFlags |= Feature.PerformanceMode;
                        m_Data.PerformanceMode = m_PerformanceMode;
                    }

                    if (thermalChanged && Capabilities.HasFlag(Feature.WarningLevel))
                    {
                        m_Data.ChangeFlags |= Feature.WarningLevel;
                        m_Data.WarningLevel = m_WarningLevel;
                    }

                    if (thermalChanged && Capabilities.HasFlag(Feature.TemperatureLevel))
                    {
                        m_Data.ChangeFlags |= Feature.TemperatureLevel;
                        m_Data.TemperatureLevel = m_Temperature;
                    }

                    if (previousGpuLevel != m_GpuPerformanceLevel && Capabilities.HasFlag(Feature.GpuPerformanceLevel))
                    {
                        m_Data.ChangeFlags |= Feature.GpuPerformanceLevel;
                        m_Data.GpuPerformanceLevel = m_GpuPerformanceLevel;
                    }

                }
            }

            public bool SetPerformanceLevel(ref int cpuLevel, ref int gpuLevel)
            {
                return false;
            }

            public bool EnableCpuBoost()
            {
                return false;
            }

            public bool EnableGpuBoost()
            {
                return false;
            }

            public void ApplicationPause() { }

            public void ApplicationResume()
            {
                ImmediateRefresh(true);
            }

            internal static class NativeApi
            {
                [StructLayout(LayoutKind.Sequential)]
                struct SYSTEM_POWER_STATUS
                {
                    public byte ACLineStatus;
                    public byte BatteryFlag;
                    public byte BatteryLifePercent;
                    public byte SystemStatusFlag;
                    public uint BatteryLifeTime;
                    public uint BatteryFullLifeTime;
                }

                [DllImport("kernel32.dll", SetLastError = true)]
                static extern bool GetSystemPowerStatus(out SYSTEM_POWER_STATUS systemPowerStatus);

                [StructLayout(LayoutKind.Sequential)]
                struct MEMORYSTATUSEX
                {
                    public uint dwLength;
                    public uint dwMemoryLoad;
                    public ulong ullTotalPhys;
                    public ulong ullAvailPhys;
                    public ulong ullTotalPageFile;
                    public ulong ullAvailPageFile;
                    public ulong ullTotalVirtual;
                    public ulong ullAvailVirtual;
                    public ulong ullAvailExtendedVirtual;
                }

                // OS-wide physical memory status across ALL processes (not just this application).
                // A single cheap kernel32 syscall — no perf-counter service dependency, no
                // first-call warm-up cost, safe to call every poll tick without caching.
                [DllImport("kernel32.dll", SetLastError = true)]
                [return: MarshalAs(UnmanagedType.Bool)]
                static extern bool GlobalMemoryStatusEx(ref MEMORYSTATUSEX lpBuffer);

                [DllImport("GiantArmyWindowsThermalBridge", EntryPoint = "GAWT_IsThermalApiAvailable", CallingConvention = CallingConvention.Cdecl)]
                static extern int GAWT_IsThermalApiAvailable();

                [DllImport("GiantArmyWindowsThermalBridge", EntryPoint = "GAWT_TryGetThermalCelsius", CallingConvention = CallingConvention.Cdecl)]
                static extern int GAWT_TryGetThermalCelsius(out float celsius);

                [DllImport("GiantArmyWindowsThermalBridge", EntryPoint = "GAWT_GetLastDiagnosticCode", CallingConvention = CallingConvention.Cdecl)]
                static extern int GAWT_GetLastDiagnosticCode();

                [DllImport("GiantArmyWindowsThermalBridge", EntryPoint = "GAWT_GetLastHResult", CallingConvention = CallingConvention.Cdecl)]
                static extern int GAWT_GetLastHResult();

                [DllImport("GiantArmyWindowsThermalBridge", EntryPoint = "GAWT_GetLastDiagnosticMessage", CallingConvention = CallingConvention.Cdecl)]
                static extern IntPtr GAWT_GetLastDiagnosticMessage();

                [DllImport("GiantArmyWindowsThermalBridge", EntryPoint = "GAWT_GetLastSourceName", CallingConvention = CallingConvention.Cdecl)]
                static extern IntPtr GAWT_GetLastSourceName();

                [DllImport("GiantArmyWindowsThermalBridge", EntryPoint = "GAWT_TryGetGpuLoad", CallingConvention = CallingConvention.Cdecl)]
                static extern int GAWT_TryGetGpuLoad(out float outLoadPercent);

                [DllImport("GiantArmyWindowsThermalBridge", EntryPoint = "GAWT_TryGetGpuClocks", CallingConvention = CallingConvention.Cdecl)]
                static extern int GAWT_TryGetGpuClocks(out float outCurrentMhz, out float outMaxMhz);

                [DllImport("GiantArmyWindowsThermalBridge", EntryPoint = "GAWT_TryGetGpuPowerUsage", CallingConvention = CallingConvention.Cdecl)]
                static extern int GAWT_TryGetGpuPowerUsage(out float outPowerWatts);

                [DllImport("GiantArmyWindowsThermalBridge", EntryPoint = "GAWT_GetLastGpuMetricsSource", CallingConvention = CallingConvention.Cdecl)]
                static extern IntPtr GAWT_GetLastGpuMetricsSource_Native();

                const bool k_EnableWmiFallback = false;

                const string k_WmiSearcherType = "System.Management.ManagementObjectSearcher, System.Management";
                const string k_WmiScopeType = "System.Management.ManagementScope, System.Management";
                const string k_WmiObjectQueryType = "System.Management.ObjectQuery, System.Management";
                const string k_WmiConnectionOptionsType = "System.Management.ConnectionOptions, System.Management";
                const string k_WmiImpersonationLevelType = "System.Management.ImpersonationLevel, System.Management";
                const string k_WmiQuery = "SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature";
                static readonly TimeSpan k_ThermalDiagnosticLogInterval = TimeSpan.FromSeconds(15);

                static readonly object s_StateLock = new object();
                static Timer s_PollTimer;
                static int s_PowerListenerRefCount;
                static int s_ThermalListenerRefCount;
                static bool s_CachedLowPowerDirty;
                static bool s_CachedThermalDirty;
                static bool s_HasCachedStatus;
                static SYSTEM_POWER_STATUS s_LastStatus;
                static float s_LastThermalCelsius;
                static float s_LastThermalLevel;
                static WarningLevel s_LastThermalWarning = WarningLevel.NoWarning;
                static DateTime s_NextThermalDiagnosticLogUtc = DateTime.MinValue;
                static bool s_NativeBridgeChecked;
                static bool s_NativeBridgeAvailable;
                static bool s_NativeBridgeMissingLogged;
                static bool s_NativeBridgeDiagLogged;

                static void PollState(object state)
                {
                    RefreshCachedState();
                    RefreshCachedThermalState();
                }

                static void LogThermalDiagnostic(string format, params object[] args)
                {
                    lock (s_StateLock)
                    {
                        var nowUtc = DateTime.UtcNow;
                        if (nowUtc < s_NextThermalDiagnosticLogUtc)
                            return;

                        s_NextThermalDiagnosticLogUtc = nowUtc + k_ThermalDiagnosticLogInterval;
                    }

                    WindowsProviderLog.Debug(format, args);
                }

                static bool TryReadStatus(out SYSTEM_POWER_STATUS status)
                {
                    return GetSystemPowerStatus(out status);
                }

                static void RefreshCachedState()
                {
                    if (!TryReadStatus(out var status))
                        return;

                    lock (s_StateLock)
                    {
                        if (!s_HasCachedStatus ||
                            status.ACLineStatus != s_LastStatus.ACLineStatus ||
                            status.BatteryFlag != s_LastStatus.BatteryFlag ||
                            status.BatteryLifePercent != s_LastStatus.BatteryLifePercent ||
                            status.SystemStatusFlag != s_LastStatus.SystemStatusFlag)
                        {
                            s_CachedLowPowerDirty = true;
                        }

                        s_LastStatus = status;
                        s_HasCachedStatus = true;
                    }
                }

                static void RefreshCachedThermalState()
                {
                    if (!TryReadThermalReading(out var thermalCelsius, out var thermalLevel, out var thermalWarning))
                        return;

                    lock (s_StateLock)
                    {
                        if (s_LastThermalLevel != thermalLevel || s_LastThermalWarning != thermalWarning)
                            s_CachedThermalDirty = true;

                        s_LastThermalCelsius = thermalCelsius;
                        s_LastThermalLevel = thermalLevel;
                        s_LastThermalWarning = thermalWarning;
                    }
                }

                public static float GetLastThermalCelsius()
                {
                    lock (s_StateLock)
                    {
                        return s_LastThermalCelsius;
                    }
                }

                static void EnsurePollingTimer()
                {
                    if (s_PollTimer != null)
                        return;

                    s_PollTimer = new Timer(PollState, null, TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(1));
                }

                static void StopPollingTimerIfUnused()
                {
                    if (s_PowerListenerRefCount > 0 || s_ThermalListenerRefCount > 0)
                        return;

                    s_PollTimer?.Dispose();
                    s_PollTimer = null;
                }

                // Reads OS-wide physical memory status via GlobalMemoryStatusEx. memoryLoadPercent
                // is the OS's own 0..100 measure of system memory pressure across all processes —
                // the accurate figure to use in place of this application's own memory footprint.
                public static bool TryGetSystemMemoryStatus(out ulong totalPhysicalBytes, out ulong availablePhysicalBytes, out uint memoryLoadPercent)
                {
                    var status = new MEMORYSTATUSEX { dwLength = (uint)Marshal.SizeOf(typeof(MEMORYSTATUSEX)) };

                    if (!GlobalMemoryStatusEx(ref status))
                    {
                        totalPhysicalBytes = 0;
                        availablePhysicalBytes = 0;
                        memoryLoadPercent = 0;
                        return false;
                    }

                    totalPhysicalBytes = status.ullTotalPhys;
                    availablePhysicalBytes = status.ullAvailPhys;
                    memoryLoadPercent = status.dwMemoryLoad;
                    return true;
                }

                public static bool IsThermalApiAvailable()
                {
                    var settings = GiantArmyWindowsProviderSettings.GetSettings();
                    if (settings != null && !settings.windowsWmiThermalProbeEnabled)
                        return false;

                    if (TryReadNativeThermalCelsius(out _))
                        return true;

                    LogNativeBridgeDiagnosticsOnce();

                    if (!k_EnableWmiFallback)
                        return false;

                    return TryReadThermalReadingViaWmi(out _, out _);
                }

                public static WarningLevel GetThermalWarningLevel()
                {
                    return TryReadThermalReading(out _, out _, out var warningLevel)
                        ? warningLevel
                        : WarningLevel.NoWarning;
                }

                public static bool SetupThermalListener()
                {
                    var settings = GiantArmyWindowsProviderSettings.GetSettings();
                    if (settings != null && !settings.windowsWmiThermalProbeEnabled)
                        return false;

                    if (!IsThermalApiAvailable())
                        return false;

                    lock (s_StateLock)
                    {
                        s_ThermalListenerRefCount++;
                        EnsurePollingTimer();
                    }

                    RefreshCachedThermalState();
                    return true;
                }

                public static void TeardownThermalListener()
                {
                    lock (s_StateLock)
                    {
                        if (s_ThermalListenerRefCount > 0)
                            s_ThermalListenerRefCount--;

                        StopPollingTimerIfUnused();
                    }
                }

                public static bool ConsumeThermalStateDirty()
                {
                    lock (s_StateLock)
                    {
                        bool dirty = s_CachedThermalDirty;
                        s_CachedThermalDirty = false;
                        return dirty;
                    }
                }

                public static bool SetupPowerStateListener()
                {
                    lock (s_StateLock)
                    {
                        s_PowerListenerRefCount++;
                        EnsurePollingTimer();
                    }

                    RefreshCachedState();
                    return true;
                }

                public static void TeardownPowerStateListener()
                {
                    lock (s_StateLock)
                    {
                        if (s_PowerListenerRefCount > 0)
                            s_PowerListenerRefCount--;

                        StopPollingTimerIfUnused();
                    }
                }

                public static bool ConsumeLowPowerStateDirty()
                {
                    lock (s_StateLock)
                    {
                        bool dirty = s_CachedLowPowerDirty;
                        s_CachedLowPowerDirty = false;
                        return dirty;
                    }
                }

                public static bool TryGetLowPowerModeEnabled(out bool enabled)
                {
                    if (TryReadStatus(out var status))
                    {
                        enabled = status.SystemStatusFlag != 0;
                        return true;
                    }

                    enabled = false;
                    return false;
                }

                public static bool TryGetThermalLevel(out float level)
                {
                    if (TryReadThermalReading(out _, out var thermalLevel, out _))
                    {
                        level = thermalLevel;
                        return true;
                    }

                    level = 0.0f;
                    return false;
                }

                static bool TryReadThermalReading(out float thermalCelsius, out float thermalLevel, out WarningLevel warningLevel)
                {
                    if (TryReadNativeThermalCelsius(out var nativeCelsius))
                    {
                        string source = GetLastThermalSourceName();
                        thermalCelsius = nativeCelsius;
                        thermalLevel = NormalizeThermalLevel(nativeCelsius, source);
                        warningLevel = MapThermalWarningLevel(nativeCelsius, source);
                        return true;
                    }

                    if (!k_EnableWmiFallback)
                    {
                        thermalCelsius = 0.0f;
                        thermalLevel = 0.0f;
                        warningLevel = WarningLevel.NoWarning;
                        return false;
                    }

                    if (!TryReadThermalReadingViaWmi(out thermalLevel, out warningLevel))
                    {
                        thermalCelsius = 0.0f;
                        return false;
                    }

                    thermalCelsius = DenormalizeThermalLevelForDisplay(thermalLevel, "wmi-managed-acpi");
                    return true;
                }

                static float DenormalizeThermalLevelForDisplay(float normalizedLevel, string source)
                {
                    var profile = GetCalibrationProfileForSourceInternal(source);

                    if (normalizedLevel >= 0.75f)
                        return profile.levelHotThreshold;

                    if (normalizedLevel >= 0.45f)
                        return profile.levelWarmThreshold;

                    return profile.levelCoolThreshold;
                }

                static bool TryReadNativeThermalCelsius(out float celsius)
                {
                    celsius = 0.0f;

                    try
                    {
                        if (!s_NativeBridgeChecked)
                        {
                            s_NativeBridgeAvailable = GAWT_IsThermalApiAvailable() != 0;
                            s_NativeBridgeChecked = true;
                        }

                        if (!s_NativeBridgeAvailable)
                        {
                            LogNativeBridgeDiagnosticsOnce();
                            return false;
                        }

                        if (GAWT_TryGetThermalCelsius(out celsius) != 0)
                            return true;

                        LogNativeBridgeDiagnosticsOnce();
                        return false;
                    }
                    catch (DllNotFoundException)
                    {
                        if (!s_NativeBridgeMissingLogged)
                        {
                            LogThermalDiagnostic("Native thermal bridge unavailable: GiantArmyWindowsThermalBridge.dll not found.");
                            s_NativeBridgeMissingLogged = true;
                        }

                        s_NativeBridgeChecked = true;
                        s_NativeBridgeAvailable = false;
                        return false;
                    }
                    catch (EntryPointNotFoundException)
                    {
                        if (!s_NativeBridgeMissingLogged)
                        {
                            LogThermalDiagnostic("Native thermal bridge unavailable: required exports not found.");
                            s_NativeBridgeMissingLogged = true;
                        }

                        s_NativeBridgeChecked = true;
                        s_NativeBridgeAvailable = false;
                        return false;
                    }
                    catch (BadImageFormatException)
                    {
                        if (!s_NativeBridgeMissingLogged)
                        {
                            LogThermalDiagnostic("Native thermal bridge unavailable: invalid binary format (x86/x64 mismatch).");
                            s_NativeBridgeMissingLogged = true;
                        }

                        s_NativeBridgeChecked = true;
                        s_NativeBridgeAvailable = false;
                        return false;
                    }
                }

                static void LogNativeBridgeDiagnosticsOnce()
                {
                    if (s_NativeBridgeDiagLogged)
                        return;

                    var diagnosticSummary = GetNativeBridgeDiagnosticSummary();
                    LogThermalDiagnostic("Native thermal bridge unavailable: {0}", diagnosticSummary);
                    s_NativeBridgeDiagLogged = true;
                }

                static string PtrToAnsiString(IntPtr ptr)
                {
                    if (ptr == IntPtr.Zero)
                        return "(null)";

                    return Marshal.PtrToStringAnsi(ptr) ?? "(null)";
                }

                static string GetNativeBridgeDiagnosticSummary()
                {
                    try
                    {
                        int code = GAWT_GetLastDiagnosticCode();
                        uint hr = unchecked((uint)GAWT_GetLastHResult());
                        string message = PtrToAnsiString(GAWT_GetLastDiagnosticMessage());
                        return string.Format("code={0}, hresult=0x{1:X8}, message={2}", code, hr, message);
                    }
                    catch (Exception ex)
                    {
                        return string.Format("diagnostics unavailable ({0}: {1})", ex.GetType().Name, ex.Message);
                    }
                }

                public static string GetLastThermalSourceName()
                {
                    try
                    {
                        string source = PtrToAnsiString(GAWT_GetLastSourceName());
                        if (string.IsNullOrEmpty(source) || source == "none")
                            return "Unavailable";

                        return source;
                    }
                    catch
                    {
                        return "Unavailable";
                    }
                }

                public static string GetUnavailableThermalDiagnosticSummary()
                {
                    string source = GetLastThermalSourceName();
                    string diagnostics = GetNativeBridgeDiagnosticSummary();
                    return string.Format("Unavailable(source={0}, {1})", source, diagnostics);
                }

                public static bool TryGetGpuLoad(out float loadPercent)
                {
                    loadPercent = 0f;
                    try
                    {
                        return GAWT_TryGetGpuLoad(out loadPercent) != 0;
                    }
                    catch { return false; }
                }

                public static bool TryGetGpuClocks(out float currentMhz, out float maxMhz)
                {
                    currentMhz = 0f;
                    maxMhz = 0f;
                    try
                    {
                        return GAWT_TryGetGpuClocks(out currentMhz, out maxMhz) != 0;
                    }
                    catch { return false; }
                }

                public static bool TryGetGpuPowerUsage(out float powerWatts)
                {
                    powerWatts = 0f;
                    try
                    {
                        return GAWT_TryGetGpuPowerUsage(out powerWatts) != 0;
                    }
                    catch { return false; }
                }

                public static string GetLastGpuMetricsSource()
                {
                    try
                    {
                        return PtrToAnsiString(GAWT_GetLastGpuMetricsSource_Native());
                    }
                    catch { return "unavailable"; }
                }

                static bool TryReadThermalReadingViaWmi(out float thermalLevel, out WarningLevel warningLevel)
                {
                    thermalLevel = 0.0f;
                    warningLevel = WarningLevel.NoWarning;

                    try
                    {
                        var searcher = CreateThermalSearcher();
                        if (searcher == null)
                        {
                            LogThermalDiagnostic("Thermal read unavailable: failed to create WMI searcher for query '{0}'.", k_WmiQuery);
                            return false;
                        }

                        try
                        {
                            var getMethod = searcher.GetType().GetMethod("Get", Type.EmptyTypes);
                            if (getMethod == null)
                            {
                                LogThermalDiagnostic("Thermal read unavailable: ManagementObjectSearcher.Get() method not found.");
                                return false;
                            }

                            IEnumerable results;
                            try
                            {
                                results = getMethod.Invoke(searcher, null) as IEnumerable;
                            }
                            catch (Exception ex)
                            {
                                LogThermalDiagnostic("Thermal query invocation failed: {0}: {1}", ex.GetType().Name, ex.Message);
                                return false;
                            }

                            if (results == null)
                            {
                                LogThermalDiagnostic("Thermal read unavailable: WMI query returned null result collection.");
                                return false;
                            }

                            bool found = false;
                            float hottestCelsius = float.MinValue;

                            foreach (var result in results)
                            {
                                if (result == null)
                                    continue;

                                var propertyMethod = result.GetType().GetMethod("GetPropertyValue", new[] { typeof(string) });
                                if (propertyMethod == null)
                                    continue;

                                var rawValue = propertyMethod.Invoke(result, new object[] { "CurrentTemperature" });
                                if (rawValue == null || rawValue == DBNull.Value)
                                    continue;

                                float currentTemperatureTenthsKelvin;
                                try
                                {
                                    currentTemperatureTenthsKelvin = Convert.ToSingle(rawValue);
                                }
                                catch
                                {
                                    continue;
                                }

                                if (currentTemperatureTenthsKelvin <= 0f)
                                    continue;

                                float celsius = (currentTemperatureTenthsKelvin / 10.0f) - 273.15f;
                                if (!found || celsius > hottestCelsius)
                                {
                                    hottestCelsius = celsius;
                                    found = true;
                                }
                            }

                            if (!found)
                            {
                                LogThermalDiagnostic("Thermal read unavailable: WMI query returned no usable CurrentTemperature values.");
                                return false;
                            }

                            const string managedWmiSource = "wmi-managed-acpi";
                            thermalLevel = NormalizeThermalLevel(hottestCelsius, managedWmiSource);
                            warningLevel = MapThermalWarningLevel(hottestCelsius, managedWmiSource);
                            return true;
                        }
                        finally
                        {
                            if (searcher is IDisposable disposable)
                                disposable.Dispose();
                        }
                    }
                    catch (Exception ex)
                    {
                        LogThermalDiagnostic("WMI thermal query failed: {0}: {1}", ex.GetType().Name, ex.Message);
                        return false;
                    }
                }

                static object CreateThermalSearcher()
                {
                    var searcherType = Type.GetType(k_WmiSearcherType, throwOnError: false);
                    if (searcherType == null)
                    {
                        WindowsProviderLog.Debug("WMI thermal searcher unavailable: System.Management not found.");
                        return null;
                    }

                    try
                    {
                        return Activator.CreateInstance(searcherType, new object[] { @"\\.\root\wmi", k_WmiQuery });
                    }
                    catch
                    {
                        try
                        {
                            var scopeType = Type.GetType(k_WmiScopeType, throwOnError: false);
                            var queryType = Type.GetType(k_WmiObjectQueryType, throwOnError: false);
                            var connectionOptionsType = Type.GetType(k_WmiConnectionOptionsType, throwOnError: false);

                            if (scopeType == null || queryType == null)
                            {
                                LogThermalDiagnostic("WMI thermal searcher unavailable: ManagementScope or ObjectQuery type not found.");
                                return null;
                            }

                            object scope;
                            if (connectionOptionsType != null)
                            {
                                var connectionOptions = Activator.CreateInstance(connectionOptionsType);
                                var enablePrivilegesProperty = connectionOptionsType.GetProperty("EnablePrivileges");
                                if (enablePrivilegesProperty != null)
                                {
                                    try
                                    {
                                        enablePrivilegesProperty.SetValue(connectionOptions, true);
                                    }
                                    catch (Exception ex)
                                    {
                                        LogThermalDiagnostic("WMI connection option EnablePrivileges is unsupported: {0}: {1}", ex.GetType().Name, ex.Message);
                                    }
                                }

                                var impersonationType = Type.GetType(k_WmiImpersonationLevelType, throwOnError: false);
                                var impersonationProperty = connectionOptionsType.GetProperty("Impersonation");
                                if (impersonationType != null && impersonationProperty != null)
                                {
                                    try
                                    {
                                        var impersonateValue = Enum.Parse(impersonationType, "Impersonate", ignoreCase: true);
                                        impersonationProperty.SetValue(connectionOptions, impersonateValue);
                                    }
                                    catch (Exception ex)
                                    {
                                        LogThermalDiagnostic("WMI connection option Impersonation is unsupported: {0}: {1}", ex.GetType().Name, ex.Message);
                                    }
                                }

                                scope = Activator.CreateInstance(scopeType, new[] { @"\\.\root\wmi", connectionOptions });
                            }
                            else
                            {
                                scope = Activator.CreateInstance(scopeType, new object[] { @"\\.\root\wmi" });
                            }

                            if (scope == null)
                                return null;

                            var connectMethod = scopeType.GetMethod("Connect", Type.EmptyTypes);
                            connectMethod?.Invoke(scope, null);

                            var query = Activator.CreateInstance(queryType, new object[] { k_WmiQuery });
                            var searcher = Activator.CreateInstance(searcherType, new[] { scope, query });

                            if (searcher == null)
                                return null;

                            return searcher;
                        }
                        catch (Exception ex)
                        {
                            LogThermalDiagnostic("WMI thermal searcher setup failed: {0}: {1}", ex.GetType().Name, ex.Message);
                            return null;
                        }
                    }
                }

                public struct ThermalCalibrationProfile
                {
                    public float noWarningUpperBound;
                    public float imminentThreshold;
                    public float throttlingThreshold;
                    public float levelCoolThreshold;
                    public float levelWarmThreshold;
                    public float levelHotThreshold;
                }

                static ThermalCalibrationProfile GetCalibrationProfileForSourceInternal(string source)
                {
                    if (string.IsNullOrEmpty(source) || source == "Unavailable" || source == "none")
                    {
                        return new ThermalCalibrationProfile
                        {
                            noWarningUpperBound = 1000.0f,
                            imminentThreshold = 1001.0f,
                            throttlingThreshold = 1002.0f,
                            levelCoolThreshold = 35.0f,
                            levelWarmThreshold = 50.0f,
                            levelHotThreshold = 65.0f
                        };
                    }

                    // NVIDIA laptop profile:
                    // Idle/light: 35C-50C
                    // Medium/heavy gaming: 70C-85C
                    // Thermal throttling: 86C-90C+
                    if (source.StartsWith("nvapi", StringComparison.OrdinalIgnoreCase))
                    {
                        return new ThermalCalibrationProfile
                        {
                            noWarningUpperBound = 70.0f,
                            imminentThreshold = 86.0f,
                            throttlingThreshold = 90.0f,
                            levelCoolThreshold = 50.0f,
                            levelWarmThreshold = 70.0f,
                            levelHotThreshold = 85.0f
                        };
                    }

                    // AMD profile (ADL):
                    // Idle: 30C-50C
                    // Light load: 50C-70C
                    // Heavy load: 70C-85C
                    // Maximum safe edge: 88C-90C
                    // Junction/hotspot: safe up to 110C, throttling above that
                    if (source.StartsWith("adl", StringComparison.OrdinalIgnoreCase))
                    {
                        return new ThermalCalibrationProfile
                        {
                            noWarningUpperBound = 85.0f,
                            imminentThreshold = 88.0f,
                            throttlingThreshold = 110.0f,
                            levelCoolThreshold = 50.0f,
                            levelWarmThreshold = 70.0f,
                            levelHotThreshold = 85.0f
                        };
                    }

                    if (source.StartsWith("intel", StringComparison.OrdinalIgnoreCase))
                    {
                        return new ThermalCalibrationProfile
                        {
                            noWarningUpperBound = 65.0f,
                            imminentThreshold = 75.0f,
                            throttlingThreshold = 85.0f,
                            levelCoolThreshold = 50.0f,
                            levelWarmThreshold = 60.0f,
                            levelHotThreshold = 75.0f
                        };
                    }

                    // Non-vendor sources can represent ambient/zone values. Keep warnings conservative.
                    return new ThermalCalibrationProfile
                    {
                        noWarningUpperBound = 75.0f,
                        imminentThreshold = 85.0f,
                        throttlingThreshold = 95.0f,
                        levelCoolThreshold = 35.0f,
                        levelWarmThreshold = 50.0f,
                        levelHotThreshold = 65.0f
                    };
                }

                public static ThermalCalibrationProfile GetCalibrationProfileForSource(string source)
                {
                    return GetCalibrationProfileForSourceInternal(source);
                }

                static WarningLevel MapThermalWarningLevel(float celsius, string source)
                {
                    var profile = GetCalibrationProfileForSourceInternal(source);

                    if (celsius >= profile.throttlingThreshold)
                        return WarningLevel.Throttling;

                    if (celsius >= profile.imminentThreshold)
                        return WarningLevel.ThrottlingImminent;

                    return WarningLevel.NoWarning;
                }

                static float NormalizeThermalLevel(float celsius, string source)
                {
                    var profile = GetCalibrationProfileForSourceInternal(source);

                    if (celsius <= profile.levelCoolThreshold)
                        return 0.15f;

                    if (celsius <= profile.levelWarmThreshold)
                        return 0.45f;

                    if (celsius <= profile.levelHotThreshold)
                        return 0.75f;

                    return 1.0f;
                }
            }
        }
    }
}