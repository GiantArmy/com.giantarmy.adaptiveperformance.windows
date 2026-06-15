using UnityEngine;
using UnityEditor.AdaptivePerformance.Editor;
using GiantArmy.Windows;
using UnityEditor;

namespace GiantArmy.Windows.Editor
{
    [CustomEditor(typeof(GiantArmyWindowsProviderSettings))]
    public class GiantArmyWindowsProviderSettingsEditor : ProviderSettingsEditor
    {
        const string k_WindowsProviderLogging = "m_WindowsProviderLogging";
        const string k_WmiThermalProbeEnabled = "m_WmiThermalProbeEnabled";

        static readonly GUIContent s_WindowsProviderLoggingLabel = EditorGUIUtility.TrTextContent(L10n.Tr("Windows Provider Logging"), L10n.Tr("Only active in development mode."));
        static readonly GUIContent s_WmiThermalProbeEnabledLabel = EditorGUIUtility.TrTextContent(L10n.Tr("Enable WMI Thermal Probe"), L10n.Tr("Best-effort thermal telemetry from WMI. Disable if the probe causes issues on a device or build."));
        static readonly GUIContent s_RuntimeLimitationsLabel = EditorGUIUtility.TrTextContent(L10n.Tr("Windows Runtime Limitations"), L10n.Tr("CPU/GPU level control and boost APIs are not available on Windows and remain disabled."));
        SerializedProperty m_WindowsProviderLoggingProperty;
        SerializedProperty m_WmiThermalProbeEnabledProperty;

        protected override bool IsBoostAvailable => false;
        protected override bool IsAutoPerformanceModeAvailable => true;
        protected override BuildTargetGroup CurrentTargetGroup => BuildTargetGroup.Standalone;

        public override string UnsupportedInfo => L10n.Tr("Adaptive Performance Windows provider is not supported on this platform");

        void ShowProviderDevelopmentSettings()
        {
            if (!m_ShowDevelopmentSettings)
                return;

            EditorGUI.indentLevel++;
            GUI.enabled = !EditorApplication.isPlayingOrWillChangePlaymode;
            EditorGUILayout.PropertyField(m_WindowsProviderLoggingProperty, s_WindowsProviderLoggingLabel);
            EditorGUILayout.PropertyField(m_WmiThermalProbeEnabledProperty, s_WmiThermalProbeEnabledLabel);
            GUI.enabled = true;
            EditorGUI.indentLevel--;
        }

        protected override void DisplayTargetProviderSettings()
        {
            if (m_WindowsProviderLoggingProperty == null)
                m_WindowsProviderLoggingProperty = serializedObject.FindProperty(k_WindowsProviderLogging);

            if (m_WmiThermalProbeEnabledProperty == null)
                m_WmiThermalProbeEnabledProperty = serializedObject.FindProperty(k_WmiThermalProbeEnabled);

            EditorGUIUtility.labelWidth = 180;
            DisplayBaseRuntimeSettings();
            EditorGUILayout.Space();
            DisplayBaseDeveloperSettings();
            EditorGUILayout.HelpBox(s_RuntimeLimitationsLabel.text, MessageType.Info);
            ShowProviderDevelopmentSettings();
        }

        public override void OnInspectorGUI()
        {
            DisplayProviderSettings();
        }
    }
}