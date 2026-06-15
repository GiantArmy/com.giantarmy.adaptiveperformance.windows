using UnityEngine;
using GiantArmy.Windows;
using UnityEditor;

namespace GiantArmy.Windows.Editor
{
    [InitializeOnLoad]
    static internal class GiantArmyWindowsProviderFrameStatsDialog
    {
        static internal bool DialogDisplayed { get; private set; } = false;

        static GiantArmyWindowsProviderFrameStatsDialog()
        {
            if (Application.isBatchMode)
                return;

            if (PlayerSettings.enableFrameTimingStats)
                return;

            GiantArmyWindowsProviderSettings settings = null;
            var assets = AssetDatabase.FindAssets($"t:{typeof(GiantArmyWindowsProviderSettings).Name}");
            if (assets.Length > 0)
            {
                var path = AssetDatabase.GUIDToAssetPath(assets[0]);
                settings = AssetDatabase.LoadAssetAtPath(path, typeof(GiantArmyWindowsProviderSettings)) as GiantArmyWindowsProviderSettings;
            }

            if (!DialogDisplayed && settings != null)
                DialogDisplayed = settings.frameStatsDialogDisplayed;

            if (DialogDisplayed)
                return;

            DialogDisplayed = true;
            if (settings != null)
            {
                settings.frameStatsDialogDisplayed = true;
                EditorUtility.SetDirty(settings);
            }

            if (EditorUtility.DisplayDialog("Adaptive Performance Windows",
                "Frame Timing Stats should be enabled in Player Settings to provide precise frame time information.",
                "Enable", "Don't enable"))
            {
                PlayerSettings.enableFrameTimingStats = true;
            }
        }
    }
}