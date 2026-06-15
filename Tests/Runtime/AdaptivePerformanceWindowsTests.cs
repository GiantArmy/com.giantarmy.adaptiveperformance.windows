using System;
using System.Reflection;
using NUnit.Framework;
using GiantArmy.Windows;

namespace GiantArmy.Windows.Tests
{
    public class AdaptivePerformanceWindowsTests
    {
        [Test]
        public void SettingsKey_IsStableAndWindowsScoped()
        {
            Assert.AreEqual(
                "com.giantarmy.adaptiveperformance.windows.provider_settings",
                GiantArmyWindowsProviderConstants.k_SettingsKey);
        }

        [Test]
        public void SettingsObject_RoundTripsSerializableFlags()
        {
            var settings = new GiantArmyWindowsProviderSettings();

            settings.windowsProviderLogging = true;
            Assert.IsTrue(settings.windowsProviderLogging);

            var frameStatsProperty = typeof(GiantArmyWindowsProviderSettings)
                .GetProperty("frameStatsDialogDisplayed", BindingFlags.Instance | BindingFlags.NonPublic);

            Assert.IsNotNull(frameStatsProperty, "Expected internal frame stats property to exist.");
            frameStatsProperty.SetValue(settings, true);
            Assert.AreEqual(true, frameStatsProperty.GetValue(settings));
        }

        [Test]
        public void Loader_GetSettings_DoesNotThrowWhenConfigIsMissing()
        {
            var loader = new GiantArmyWindowsProviderLoader();
            Assert.DoesNotThrow(() => loader.GetSettings());
        }

#if !UNITY_STANDALONE_WIN && !UNITY_EDITOR_WIN
        [Test]
        public void LoaderLifecycle_OnUnsupportedContext_IsSafelyDisabled()
        {
            var loader = new GiantArmyWindowsProviderLoader();

            Assert.IsFalse(loader.Initialized);
            Assert.IsFalse(loader.Running);
            Assert.IsNull(loader.GetDefaultSubsystem());

            Assert.IsFalse(loader.Initialize());
            Assert.IsFalse(loader.Start());
            Assert.IsFalse(loader.Stop());
            Assert.IsFalse(loader.Deinitialize());
        }
#endif

#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
        [Test]
        public void NativeBridgeIntegration_UsesWindowsPowerStatusAndBestEffortThermalWmi()
        {
            var providerType = typeof(GiantArmyWindowsAdaptivePerformanceSubsystem.GiantArmyWindowsAdaptivePerformanceSubsystemProvider);
            var nativeApiType = providerType.GetNestedType("NativeApi", BindingFlags.NonPublic);
            Assert.IsNotNull(nativeApiType, "Expected NativeApi bridge wrapper type.");

            var isThermalAvailable = (bool)nativeApiType.GetMethod("IsThermalApiAvailable", BindingFlags.Public | BindingFlags.Static).Invoke(null, null);

            if (isThermalAvailable)
                Assert.IsTrue((bool)nativeApiType.GetMethod("SetupThermalListener", BindingFlags.Public | BindingFlags.Static).Invoke(null, null));
            else
                Assert.IsFalse((bool)nativeApiType.GetMethod("SetupThermalListener", BindingFlags.Public | BindingFlags.Static).Invoke(null, null));

            Assert.IsTrue((bool)nativeApiType.GetMethod("SetupPowerStateListener", BindingFlags.Public | BindingFlags.Static).Invoke(null, null));

            Assert.IsTrue((bool)nativeApiType.GetMethod("ConsumeLowPowerStateDirty", BindingFlags.Public | BindingFlags.Static).Invoke(null, null));

            object[] thermalLevelArgs = { 0f };
            var gotThermalLevel = (bool)nativeApiType.GetMethod("TryGetThermalLevel", BindingFlags.Public | BindingFlags.Static)
                .Invoke(null, thermalLevelArgs);

            if (isThermalAvailable)
            {
                Assert.IsTrue(gotThermalLevel);
                Assert.That(thermalLevelArgs[0], Is.GreaterThanOrEqualTo(0f).And.LessThanOrEqualTo(1f));

                var thermalWarningLevel = (WarningLevel)nativeApiType.GetMethod("GetThermalWarningLevel", BindingFlags.Public | BindingFlags.Static)
                    .Invoke(null, null);
                Assert.That(thermalWarningLevel, Is.EqualTo(WarningLevel.NoWarning).Or.EqualTo(WarningLevel.ThrottlingImminent).Or.EqualTo(WarningLevel.Throttling));
            }
            else
            {
                Assert.IsFalse(gotThermalLevel);
            }

            object[] lowPowerArgs = { false };
            Assert.IsTrue((bool)nativeApiType.GetMethod("TryGetLowPowerModeEnabled", BindingFlags.Public | BindingFlags.Static)
                .Invoke(null, lowPowerArgs));

            if (isThermalAvailable)
                nativeApiType.GetMethod("TeardownThermalListener", BindingFlags.Public | BindingFlags.Static).Invoke(null, null);

            nativeApiType.GetMethod("TeardownPowerStateListener", BindingFlags.Public | BindingFlags.Static).Invoke(null, null);
        }
#endif
    }
}