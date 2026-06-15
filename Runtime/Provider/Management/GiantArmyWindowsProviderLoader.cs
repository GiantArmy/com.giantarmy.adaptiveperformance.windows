using System.Collections.Generic;

using UnityEngine;
using UnityEngine.AdaptivePerformance;
using UnityEngine.AdaptivePerformance.GiantArmy.Windows;

#if UNITY_EDITOR
using UnityEditor;
using UnityEditor.AdaptivePerformance.Editor;
#endif
using UnityEngine.AdaptivePerformance.Provider;

namespace GiantArmy.Windows
{
#if UNITY_EDITOR
    [AdaptivePerformanceSupportedBuildTargetAttribute(BuildTargetGroup.Standalone)]
#endif
    public class GiantArmyWindowsProviderLoader : AdaptivePerformanceLoaderHelper
    {
        static List<AdaptivePerformanceSubsystemDescriptor> s_WindowsSubsystemDescriptors =
            new List<AdaptivePerformanceSubsystemDescriptor>();

        public override bool Initialized
        {
            get
            {
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
                return windowsSubsystem != null;
#else
                return false;
#endif
            }
        }

        public override bool Running
        {
            get
            {
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
                return windowsSubsystem != null && windowsSubsystem.running;
#else
                return false;
#endif
            }
        }

#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
        public GiantArmyWindowsAdaptivePerformanceSubsystem windowsSubsystem
        {
            get { return GetLoadedSubsystem<GiantArmyWindowsAdaptivePerformanceSubsystem>(); }
        }
#endif

        public override ISubsystem GetDefaultSubsystem()
        {
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
            return windowsSubsystem;
#else
            return null;
#endif
        }

        public override IAdaptivePerformanceSettings GetSettings()
        {
            return GiantArmyWindowsProviderSettings.GetSettings();
        }

        public override bool Initialize()
        {
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
            CreateSubsystem<AdaptivePerformanceSubsystemDescriptor, GiantArmyWindowsAdaptivePerformanceSubsystem>(s_WindowsSubsystemDescriptors, "GiantArmyWindows");
            if (windowsSubsystem == null)
            {
                Debug.LogError("[AP Windows] Unable to start the Windows subsystem.");
            }

            Debug.Log("[AP Windows] Windows subsystem initialized.");
            return windowsSubsystem != null;
#else
            return false;
#endif
        }

        public override bool Start()
        {
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
            StartSubsystem<GiantArmyWindowsAdaptivePerformanceSubsystem>();
            return true;
#else
            return false;
#endif
        }

        public override bool Stop()
        {
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
            StopSubsystem<GiantArmyWindowsAdaptivePerformanceSubsystem>();
            return true;
#else
            return false;
#endif
        }

        public override bool Deinitialize()
        {
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
            DestroySubsystem<GiantArmyWindowsAdaptivePerformanceSubsystem>();
            return base.Deinitialize();
#else
            return false;
#endif
        }
    }
}
