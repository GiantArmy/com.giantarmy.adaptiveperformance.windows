using System.Collections.Generic;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEditor.Build.Profile;
using UnityEngine;
using UnityEngine.AdaptivePerformance;
using UnityEditor.AdaptivePerformance.Editor;
using GiantArmy.Windows;
using UnityEditor.AdaptivePerformance.Editor.Metadata;

namespace GiantArmy.Windows.Editor
{
    public class GiantArmyWindowsProviderBuildProcess : IPreprocessBuildWithReport, IPostprocessBuildWithReport
    {
        public int callbackOrder => 0;

        void CleanOldSettings()
        {
            UnityEngine.Object[] preloadedAssets = PlayerSettings.GetPreloadedAssets();
            if (preloadedAssets == null)
                return;

            var assets = new List<UnityEngine.Object>();
            bool hasOldSettings = false;
            foreach (var s in preloadedAssets)
            {
                if (s != null && s.GetType() == typeof(GiantArmyWindowsProviderSettings))
                {
                    hasOldSettings = true;
                    continue;
                }
                assets.Add(s);
            }

            if (hasOldSettings)
                PlayerSettings.SetPreloadedAssets(assets.ToArray());
        }

        bool IsProviderEnabled(AdaptivePerformanceGeneralSettings generalSettings)
        {
            foreach (var loader in generalSettings.Manager.loaders)
            {
                if (loader is GiantArmyWindowsProviderLoader)
                    return true;
            }
            return false;
        }

        public void OnPreprocessBuild(BuildReport report)
        {
            if (report.summary.platform != BuildTarget.StandaloneWindows64 && report.summary.platform != BuildTarget.StandaloneWindows)
                return;

            CleanOldSettings();

            var assets = new List<UnityEngine.Object>(PlayerSettings.GetPreloadedAssets());
            var activeProfile = BuildProfile.GetActiveBuildProfile();

            GiantArmyWindowsProviderSettings settings = null;
            AdaptivePerformanceGeneralSettings generalSettings = null;

            if (activeProfile != null)
            {
                generalSettings = activeProfile.GetComponent<AdaptivePerformanceGeneralSettings>();
                if (generalSettings != null && IsProviderEnabled(generalSettings))
                {
                    var container = activeProfile.GetComponent<BuildProfileProviderContainer>();
                    if (container != null)
                    {
                        foreach (var providerSettings in container.adaptivePerformanceProviderSettings)
                        {
                            if (providerSettings is GiantArmyWindowsProviderSettings windowsSettings)
                                settings = windowsSettings;
                        }
                    }
                }
            }

            if (settings == null && generalSettings == null)
            {
                EditorBuildSettings.TryGetConfigObject(GiantArmyWindowsProviderConstants.k_SettingsKey, out settings);
                if (settings == null)
                    return;

                generalSettings = AdaptivePerformanceGeneralSettingsPerBuildTarget.AdaptivePerformanceGeneralSettingsForBuildTarget(BuildTargetGroup.Standalone);
                if (!IsProviderEnabled(generalSettings))
                    return;
            }

            if (settings != null)
                AdaptivePerformanceBuildUtils.AddCustomScalerToProviderSetting(settings);

            if (settings != null && !assets.Contains(settings))
                assets.Add(settings);

            PlayerSettings.SetPreloadedAssets(assets.ToArray());
        }

        public void OnPostprocessBuild(BuildReport report)
        {
            if (report.summary.platform != BuildTarget.StandaloneWindows64 && report.summary.platform != BuildTarget.StandaloneWindows)
                return;

            CleanOldSettings();
        }
    }
}