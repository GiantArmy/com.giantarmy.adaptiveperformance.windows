using System.Collections.Generic;
using UnityEngine;
using UnityEditor.AdaptivePerformance.Editor.Metadata;
using UnityEditor;

namespace GiantArmy.Windows.Editor
{
    public class GiantArmyWindowsProviderMetadata : IAdaptivePerformancePackage
    {
        private static readonly IAdaptivePerformancePackageMetadata s_Metadata = new GiantArmyWindowsPackageMetadata();
        public IAdaptivePerformancePackageMetadata metadata => s_Metadata;

        public bool PopulateNewSettingsInstance(ScriptableObject obj)
        {
            var settings = obj as GiantArmy.Windows.GiantArmyWindowsProviderSettings;
            if (settings == null)
                return false;

            settings.logging = true;
            settings.statsLoggingFrequencyInFrames = 50;
            settings.automaticPerformanceMode = true;
            settings.frameStatsDialogDisplayed = GiantArmyWindowsProviderFrameStatsDialog.DialogDisplayed;
            EditorUtility.SetDirty(settings);
            return true;
        }
    }

    public class GiantArmyWindowsPackageMetadata : IAdaptivePerformancePackageMetadata
    {
        public string packageName => "GiantArmy Adaptive Performance Windows";
        public string packageId => "com.giantarmy.adaptiveperformance.windows";
        public string settingsType { get; } = typeof(GiantArmy.Windows.GiantArmyWindowsProviderSettings).FullName;
        public string licenseURL => "LICENSE.md";
        public string isDefaultPlatformProvider => "true";
        public List<IAdaptivePerformanceLoaderMetadata> loaderMetadata => s_LoaderMetadata;

        private static readonly List<IAdaptivePerformanceLoaderMetadata> s_LoaderMetadata = new List<IAdaptivePerformanceLoaderMetadata> { new GiantArmyWindowsLoaderMetadata() };
    }

    public class GiantArmyWindowsLoaderMetadata : IAdaptivePerformanceLoaderMetadata
    {
        public string loaderName => "Windows Provider";
        public string loaderType { get; } = typeof(GiantArmy.Windows.GiantArmyWindowsProviderLoader).FullName;
        public List<BuildTargetGroup> supportedBuildTargets => s_SupportedBuildTargets;
        public int priority => 0;

        private static readonly List<BuildTargetGroup> s_SupportedBuildTargets = new List<BuildTargetGroup>
            {
                BuildTargetGroup.Standalone
            };
    }
}