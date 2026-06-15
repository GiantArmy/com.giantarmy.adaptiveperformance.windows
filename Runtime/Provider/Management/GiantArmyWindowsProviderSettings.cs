using UnityEngine;
using UnityEngine.AdaptivePerformance;

namespace GiantArmy.Windows
{
    [System.Serializable]
    [AdaptivePerformanceConfigurationData("Windows", GiantArmyWindowsProviderConstants.k_SettingsKey)]
    public class GiantArmyWindowsProviderSettings : IAdaptivePerformanceSettings
    {
        [SerializeField, Tooltip("Enable logging in development builds")]
        bool m_WindowsProviderLogging = true;

        [SerializeField, Tooltip("Enable best-effort WMI thermal probing when available")]
        bool m_WmiThermalProbeEnabled = true;

        public bool windowsProviderLogging
        {
            get { return m_WindowsProviderLogging; }
            set { m_WindowsProviderLogging = value; }
        }

        public bool windowsWmiThermalProbeEnabled
        {
            get { return m_WmiThermalProbeEnabled; }
            set { m_WmiThermalProbeEnabled = value; }
        }

        [SerializeField]
        bool m_FrameStatsDialogDisplayed = false;

        internal bool frameStatsDialogDisplayed
        {
            get { return m_FrameStatsDialogDisplayed; }
            set { m_FrameStatsDialogDisplayed = value; }
        }

#if !UNITY_EDITOR
        public static GiantArmyWindowsProviderSettings s_RuntimeInstance = null;
#endif

        void Awake()
        {
#if !UNITY_EDITOR
            s_RuntimeInstance = this;
#endif
        }

        public static GiantArmyWindowsProviderSettings GetSettings()
        {
            GiantArmyWindowsProviderSettings settings = null;
#if UNITY_EDITOR
            UnityEditor.EditorBuildSettings.TryGetConfigObject<GiantArmyWindowsProviderSettings>(GiantArmyWindowsProviderConstants.k_SettingsKey, out settings);
#else
            settings = s_RuntimeInstance;
#endif
            return settings;
        }
    }
}
