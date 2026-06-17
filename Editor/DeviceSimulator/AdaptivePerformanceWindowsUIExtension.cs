using System;
using System.Reflection;
using UnityEditor.DeviceSimulation;
using UnityEngine;
using UnityEngine.UIElements;
using UnityEditor.AdaptivePerformance.Simulator.Editor;
using UnityEngine.AdaptivePerformance;
using GiantArmy.Windows;
using UnityEditor;
using UnityEngine.AdaptivePerformance.GiantArmy.Windows;

namespace GiantArmy.Windows.Editor
{
    public class AdaptivePerformanceWindowsUIExtension : DeviceSimulatorPlugin
    {
        public override string title => "Adaptive Performance Windows";

        sealed class TemperatureHistoryGraph : VisualElement
        {
            const int k_MaxSamples = 120;
            readonly System.Collections.Generic.List<float> m_Samples = new System.Collections.Generic.List<float>(k_MaxSamples);

            public TemperatureHistoryGraph()
            {
                pickingMode = PickingMode.Ignore;
                generateVisualContent += OnGenerateVisualContent;
            }

            public void SetSample(float value, bool available)
            {
                m_Samples.Add(available ? Mathf.Clamp01(value) : -1f);

                if (m_Samples.Count > k_MaxSamples)
                    m_Samples.RemoveAt(0);

                MarkDirtyRepaint();
            }

            void OnGenerateVisualContent(MeshGenerationContext context)
            {
                var rect = contentRect;
                if (rect.width <= 1f || rect.height <= 1f)
                    return;

                var painter = context.painter2D;

                painter.fillColor = new Color(0.09f, 0.10f, 0.13f, 0.95f);
                painter.BeginPath();
                painter.MoveTo(new Vector2(rect.xMin, rect.yMin));
                painter.LineTo(new Vector2(rect.xMax, rect.yMin));
                painter.LineTo(new Vector2(rect.xMax, rect.yMax));
                painter.LineTo(new Vector2(rect.xMin, rect.yMax));
                painter.ClosePath();
                painter.Fill();

                DrawBand(painter, rect, 0.0f, 0.3f, new Color(0.14f, 0.48f, 0.24f, 0.12f));
                DrawBand(painter, rect, 0.3f, 0.6f, new Color(0.14f, 0.34f, 0.58f, 0.12f));
                DrawBand(painter, rect, 0.6f, 0.8f, new Color(0.60f, 0.43f, 0.12f, 0.12f));
                DrawBand(painter, rect, 0.8f, 1.0f, new Color(0.65f, 0.22f, 0.22f, 0.14f));

                DrawGuideLine(painter, rect, 0.25f);
                DrawGuideLine(painter, rect, 0.50f);
                DrawGuideLine(painter, rect, 0.75f);

                DrawPolyline(painter, rect);
            }

            void DrawBand(Painter2D painter, Rect rect, float min, float max, Color color)
            {
                var top = Mathf.Lerp(rect.yMax, rect.yMin, max);
                var bottom = Mathf.Lerp(rect.yMax, rect.yMin, min);

                painter.fillColor = color;
                painter.BeginPath();
                painter.MoveTo(new Vector2(rect.xMin, top));
                painter.LineTo(new Vector2(rect.xMax, top));
                painter.LineTo(new Vector2(rect.xMax, bottom));
                painter.LineTo(new Vector2(rect.xMin, bottom));
                painter.ClosePath();
                painter.Fill();
            }

            void DrawGuideLine(Painter2D painter, Rect rect, float normalizedY)
            {
                var y = Mathf.Lerp(rect.yMax, rect.yMin, normalizedY);
                painter.strokeColor = new Color(1f, 1f, 1f, 0.06f);
                painter.lineWidth = 1f;
                painter.BeginPath();
                painter.MoveTo(new Vector2(rect.xMin, y));
                painter.LineTo(new Vector2(rect.xMax, y));
                painter.Stroke();
            }

            void DrawPolyline(Painter2D painter, Rect rect)
            {
                if (m_Samples.Count < 2)
                    return;

                var strokeColor = new Color(0.30f, 0.74f, 1.0f, 0.95f);
                bool started = false;
                int validCount = 0;

                for (int i = 0; i < m_Samples.Count; i++)
                {
                    var sample = m_Samples[i];
                    if (sample < 0f)
                    {
                        started = false;
                        continue;
                    }

                    validCount++;
                    var x = Mathf.Lerp(rect.xMin + 8f, rect.xMax - 8f, m_Samples.Count <= 1 ? 0f : i / (float)(m_Samples.Count - 1));
                    var y = Mathf.Lerp(rect.yMax - 8f, rect.yMin + 8f, sample);

                    if (!started)
                    {
                        painter.BeginPath();
                        painter.MoveTo(new Vector2(x, y));
                        started = true;
                    }
                    else
                    {
                        painter.LineTo(new Vector2(x, y));
                    }
                }

                if (!started || validCount < 2)
                    return;

                painter.strokeColor = strokeColor;
                painter.lineWidth = 2.5f;
                painter.Stroke();
            }
        }

        sealed class GpuMetricsGraph : VisualElement
        {
            const int k_MaxSamples = 120;
            readonly System.Collections.Generic.List<float> m_Samples = new System.Collections.Generic.List<float>(k_MaxSamples);
            readonly Color m_LineColor;
            readonly string m_Label;

            public GpuMetricsGraph(Color lineColor, string label)
            {
                m_LineColor = lineColor;
                m_Label = label;
                pickingMode = PickingMode.Ignore;
                generateVisualContent += OnGenerateVisualContent;
            }

            public void SetSample(float normalizedValue, bool available)
            {
                m_Samples.Add(available ? Mathf.Clamp01(normalizedValue) : -1f);
                if (m_Samples.Count > k_MaxSamples)
                    m_Samples.RemoveAt(0);
                MarkDirtyRepaint();
            }

            void OnGenerateVisualContent(MeshGenerationContext context)
            {
                var rect = contentRect;
                if (rect.width <= 1f || rect.height <= 1f)
                    return;

                var painter = context.painter2D;

                // Background
                painter.fillColor = new Color(0.09f, 0.10f, 0.13f, 0.95f);
                painter.BeginPath();
                painter.MoveTo(new Vector2(rect.xMin, rect.yMin));
                painter.LineTo(new Vector2(rect.xMax, rect.yMin));
                painter.LineTo(new Vector2(rect.xMax, rect.yMax));
                painter.LineTo(new Vector2(rect.xMin, rect.yMax));
                painter.ClosePath();
                painter.Fill();

                // Guide lines at 25/50/75%
                for (float g = 0.25f; g <= 0.75f; g += 0.25f)
                {
                    var y = Mathf.Lerp(rect.yMax, rect.yMin, g);
                    painter.strokeColor = new Color(1f, 1f, 1f, 0.06f);
                    painter.lineWidth = 1f;
                    painter.BeginPath();
                    painter.MoveTo(new Vector2(rect.xMin, y));
                    painter.LineTo(new Vector2(rect.xMax, y));
                    painter.Stroke();
                }

                // Polyline
                if (m_Samples.Count < 2)
                    return;

                bool started = false;
                int validCount = 0;

                for (int i = 0; i < m_Samples.Count; i++)
                {
                    var sample = m_Samples[i];
                    if (sample < 0f) { started = false; continue; }

                    validCount++;
                    var x = Mathf.Lerp(rect.xMin + 8f, rect.xMax - 8f, m_Samples.Count <= 1 ? 0f : i / (float)(m_Samples.Count - 1));
                    var y = Mathf.Lerp(rect.yMax - 8f, rect.yMin + 8f, sample);

                    if (!started) { painter.BeginPath(); painter.MoveTo(new Vector2(x, y)); started = true; }
                    else { painter.LineTo(new Vector2(x, y)); }
                }

                if (!started || validCount < 2)
                    return;

                painter.strokeColor = m_LineColor;
                painter.lineWidth = 2.5f;
                painter.Stroke();
            }
        }

        Toggle m_LowPowerModeToggle;
        IntegerField m_TargetFrameRateField;
        EnumField m_PerformanceModeField;
        EnumField m_WarningLevelField;
        Slider m_TemperatureLevelSlider;
        TemperatureHistoryGraph m_TemperatureHistoryGraph;
        VisualElement m_TemperatureHistoryGraphHost;
        Label m_TemperatureHistoryCaptionLabel;
        Label m_ThermalSourceValueLabel;
        Label m_ThermalAvailableValueLabel;
        Label m_LowPowerValueLabel;
        Label m_PerformanceModeValueLabel;
        Label m_WarningLevelValueLabel;
        Label m_TemperatureValueLabel;

        // GPU metrics
        Label m_GpuSourceValueLabel;
        Label m_GpuLoadValueLabel;
        Label m_GpuClockValueLabel;
        Label m_GpuPowerValueLabel;
        Label m_GpuPerfLevelValueLabel;
        Label m_GpuLoadCaptionLabel;
        Label m_GpuClockCaptionLabel;
        Label m_GpuPowerCaptionLabel;
        GpuMetricsGraph m_GpuLoadGraph;
        GpuMetricsGraph m_GpuClockGraph;
        GpuMetricsGraph m_GpuPowerGraph;
        VisualElement m_GpuLoadGraphHost;
        VisualElement m_GpuClockGraphHost;
        VisualElement m_GpuPowerGraphHost;

        public override VisualElement OnCreateUI()
        {
            var root = new VisualElement();
            var tree = AssetDatabase.LoadAssetAtPath<VisualTreeAsset>("Packages/com.giantarmy.adaptiveperformance.windows/Editor/DeviceSimulator/AdaptivePerformanceExtension.uxml");

            if (tree == null)
            {
                root.Add(new Label("Adaptive Performance Windows options are available in supported Unity versions."));
                return root;
            }

            root.Add(tree.CloneTree());

            m_LowPowerModeToggle = root.Q<Toggle>("windows-low-power-mode");
            m_TargetFrameRateField = root.Q<IntegerField>("windows-target-frame-rate");
            m_PerformanceModeField = root.Q<EnumField>("windows-performance-mode");
            m_WarningLevelField = root.Q<EnumField>("windows-warning-level");
            m_TemperatureLevelSlider = root.Q<Slider>("windows-temperature-level");
            m_TemperatureHistoryGraphHost = root.Q<VisualElement>("windows-temperature-history-graph");
            m_TemperatureHistoryCaptionLabel = root.Q<Label>("windows-temperature-history-caption");
            m_ThermalSourceValueLabel = root.Q<Label>("windows-thermal-source-value");
            m_ThermalAvailableValueLabel = root.Q<Label>("windows-thermal-available-value");
            m_LowPowerValueLabel = root.Q<Label>("windows-low-power-value");
            m_PerformanceModeValueLabel = root.Q<Label>("windows-performance-mode-value");
            m_WarningLevelValueLabel = root.Q<Label>("windows-warning-level-value");
            m_TemperatureValueLabel = root.Q<Label>("windows-temperature-value");

            // GPU metrics labels
            m_GpuSourceValueLabel = root.Q<Label>("windows-gpu-source-value");
            m_GpuLoadValueLabel = root.Q<Label>("windows-gpu-load-value");
            m_GpuClockValueLabel = root.Q<Label>("windows-gpu-clock-value");
            m_GpuPowerValueLabel = root.Q<Label>("windows-gpu-power-value");
            m_GpuPerfLevelValueLabel = root.Q<Label>("windows-gpu-perf-level-value");
            m_GpuLoadCaptionLabel = root.Q<Label>("windows-gpu-load-caption");
            m_GpuClockCaptionLabel = root.Q<Label>("windows-gpu-clock-caption");
            m_GpuPowerCaptionLabel = root.Q<Label>("windows-gpu-power-caption");
            m_GpuLoadGraphHost = root.Q<VisualElement>("windows-gpu-load-graph");
            m_GpuClockGraphHost = root.Q<VisualElement>("windows-gpu-clock-graph");
            m_GpuPowerGraphHost = root.Q<VisualElement>("windows-gpu-power-graph");

            SetupStatusLabel(m_ThermalSourceValueLabel);
            SetupStatusLabel(m_ThermalAvailableValueLabel);
            SetupStatusLabel(m_LowPowerValueLabel);
            SetupStatusLabel(m_PerformanceModeValueLabel);
            SetupStatusLabel(m_WarningLevelValueLabel);
            SetupStatusLabel(m_TemperatureValueLabel);
            SetupStatusLabel(m_TemperatureHistoryCaptionLabel);
            SetupStatusLabel(m_GpuSourceValueLabel);
            SetupStatusLabel(m_GpuLoadValueLabel);
            SetupStatusLabel(m_GpuClockValueLabel);
            SetupStatusLabel(m_GpuPowerValueLabel);
            SetupStatusLabel(m_GpuPerfLevelValueLabel);
            SetupStatusLabel(m_GpuLoadCaptionLabel);
            SetupStatusLabel(m_GpuClockCaptionLabel);
            SetupStatusLabel(m_GpuPowerCaptionLabel);

            if (m_TemperatureHistoryGraphHost != null)
            {
                m_TemperatureHistoryGraphHost.Clear();
                m_TemperatureHistoryGraph = new TemperatureHistoryGraph
                {
                    name = "windows-temperature-history-graph-canvas"
                };
                m_TemperatureHistoryGraph.style.flexGrow = 1f;
                m_TemperatureHistoryGraph.style.height = new StyleLength(new Length(100f, LengthUnit.Percent));
                m_TemperatureHistoryGraphHost.Add(m_TemperatureHistoryGraph);
            }

            if (m_GpuLoadGraphHost != null)
            {
                m_GpuLoadGraphHost.Clear();
                m_GpuLoadGraph = new GpuMetricsGraph(new Color(0.45f, 0.85f, 0.45f, 0.95f), "GPU Load");
                m_GpuLoadGraph.style.flexGrow = 1f;
                m_GpuLoadGraph.style.height = new StyleLength(new Length(100f, LengthUnit.Percent));
                m_GpuLoadGraphHost.Add(m_GpuLoadGraph);
            }

            if (m_GpuClockGraphHost != null)
            {
                m_GpuClockGraphHost.Clear();
                m_GpuClockGraph = new GpuMetricsGraph(new Color(0.55f, 0.70f, 1.0f, 0.95f), "GPU Clock");
                m_GpuClockGraph.style.flexGrow = 1f;
                m_GpuClockGraph.style.height = new StyleLength(new Length(100f, LengthUnit.Percent));
                m_GpuClockGraphHost.Add(m_GpuClockGraph);
            }

            if (m_GpuPowerGraphHost != null)
            {
                m_GpuPowerGraphHost.Clear();
                m_GpuPowerGraph = new GpuMetricsGraph(new Color(1.0f, 0.6f, 0.2f, 0.95f), "GPU Power");
                m_GpuPowerGraph.style.flexGrow = 1f;
                m_GpuPowerGraph.style.height = new StyleLength(new Length(100f, LengthUnit.Percent));
                m_GpuPowerGraphHost.Add(m_GpuPowerGraph);
            }

            if (m_LowPowerModeToggle != null)
            {
                m_LowPowerModeToggle.RegisterValueChangedCallback(evt =>
                {
                    // Simulator internals differ across Unity versions, so probe multiple names.
                    if (!TrySetAnySubsystemProperty(new[] { "LowPowerModeEnabled", "LowPowerMode", "BatterySaverEnabled" }, evt.newValue))
                        TrySetSubsystemProperty("PerformanceMode", GetModeFromInputs(evt.newValue, Application.targetFrameRate));
                });
            }

            if (m_TargetFrameRateField != null)
            {
                m_TargetFrameRateField.RegisterValueChangedCallback(evt =>
                {
                    var clampedFrameRate = Mathf.Clamp(evt.newValue, -1, 240);
                    Application.targetFrameRate = clampedFrameRate;

                    if (m_LowPowerModeToggle == null || !m_LowPowerModeToggle.value)
                        TrySetSubsystemProperty("PerformanceMode", GetModeFromInputs(false, clampedFrameRate));
                });
            }

            if (m_PerformanceModeField != null)
            {
                m_PerformanceModeField.Init(PerformanceMode.Standard);
                m_PerformanceModeField.RegisterValueChangedCallback(evt =>
                {
                    TrySetSubsystemProperty("PerformanceMode", (PerformanceMode)evt.newValue);
                });
            }

            if (m_WarningLevelField != null)
            {
                m_WarningLevelField.Init(WarningLevel.NoWarning);
                m_WarningLevelField.RegisterValueChangedCallback(evt =>
                {
                    TrySetSubsystemProperty("WarningLevel", (WarningLevel)evt.newValue);
                });
            }

            if (m_TemperatureLevelSlider != null)
            {
                m_TemperatureLevelSlider.RegisterValueChangedCallback(evt =>
                {
                    var value = Mathf.Clamp01(evt.newValue);
                    if (!TrySetSubsystemProperty("TemperatureLevel", value))
                        TrySetSubsystemProperty("Temperature", value);
                });
            }

            root.schedule.Execute(RefreshDiagnostics).Every(500);
            RefreshDiagnostics();

            return root;
        }

        void RefreshDiagnostics()
        {
            if (!GiantArmyWindowsAdaptivePerformanceSubsystem.TryGetDiagnosticSnapshot(out var snapshot))
                return;

            if (m_ThermalSourceValueLabel != null)
            {
                m_ThermalSourceValueLabel.text = snapshot.thermalSource;
                ApplyState(m_ThermalSourceValueLabel, snapshot.thermalProbeEnabled ? "state-info" : "state-muted");
            }

            if (m_ThermalAvailableValueLabel != null)
            {
                m_ThermalAvailableValueLabel.text = snapshot.thermalAvailable ? "Yes" : "No";
                ApplyState(m_ThermalAvailableValueLabel, snapshot.thermalAvailable ? "state-ok" : "state-muted");
            }

            if (m_LowPowerValueLabel != null)
            {
                m_LowPowerValueLabel.text = snapshot.lowPowerModeEnabled ? "Enabled" : "Disabled";
                ApplyState(m_LowPowerValueLabel, snapshot.lowPowerModeEnabled ? "state-warn" : "state-ok");
            }

            if (m_PerformanceModeValueLabel != null)
            {
                m_PerformanceModeValueLabel.text = snapshot.performanceMode.ToString();
                ApplyState(m_PerformanceModeValueLabel, GetPerformanceModeState(snapshot.performanceMode));
            }

            if (m_WarningLevelValueLabel != null)
            {
                m_WarningLevelValueLabel.text = snapshot.warningLevel.ToString();
                ApplyState(m_WarningLevelValueLabel, GetWarningState(snapshot.warningLevel));
            }

            if (m_TemperatureValueLabel != null)
            {
                m_TemperatureValueLabel.text = snapshot.thermalAvailable
                    ? string.Format("{0:0.00} ({1:0.0} C)", snapshot.temperatureLevel, snapshot.temperatureCelsius)
                    : "N/A";
                ApplyState(m_TemperatureValueLabel, GetTemperatureState(snapshot.temperatureLevel));
            }

            if (m_TemperatureHistoryGraph != null)
                m_TemperatureHistoryGraph.SetSample(snapshot.temperatureLevel, snapshot.thermalAvailable);

            if (m_TemperatureHistoryCaptionLabel != null)
            {
                m_TemperatureHistoryCaptionLabel.text = snapshot.thermalAvailable
                    ? string.Format("Latest sample: {0:0.0} C (level {1:0.00})", snapshot.temperatureCelsius, snapshot.temperatureLevel)
                    : $"No thermal data ({snapshot.thermalSource})";

                ApplyState(m_TemperatureHistoryCaptionLabel, snapshot.thermalAvailable ? "state-info" : "state-muted");
            }

            // GPU metrics
            if (m_GpuSourceValueLabel != null)
            {
                m_GpuSourceValueLabel.text = snapshot.gpuMetricsAvailable ? snapshot.gpuMetricsSource : "Unavailable";
                ApplyState(m_GpuSourceValueLabel, snapshot.gpuMetricsAvailable ? "state-info" : "state-muted");
            }

            if (m_GpuLoadValueLabel != null)
            {
                m_GpuLoadValueLabel.text = snapshot.gpuMetricsAvailable
                    ? string.Format("{0:0}%", snapshot.gpuLoadPercent)
                    : "N/A";
                ApplyState(m_GpuLoadValueLabel, snapshot.gpuMetricsAvailable ? GetGpuLoadState(snapshot.gpuLoadPercent) : "state-muted");
            }

            if (m_GpuClockValueLabel != null)
            {
                m_GpuClockValueLabel.text = snapshot.gpuMetricsAvailable
                    ? string.Format("{0:0} / {1:0} MHz", snapshot.gpuClockCurrentMhz, snapshot.gpuClockMaxMhz)
                    : "N/A";
                ApplyState(m_GpuClockValueLabel, snapshot.gpuMetricsAvailable ? "state-info" : "state-muted");
            }

            if (m_GpuPowerValueLabel != null)
            {
                m_GpuPowerValueLabel.text = snapshot.gpuMetricsAvailable
                    ? string.Format("{0:0.0} W", snapshot.gpuPowerWatts)
                    : "N/A";
                ApplyState(m_GpuPowerValueLabel, snapshot.gpuMetricsAvailable ? GetGpuPowerState(snapshot.gpuPowerWatts) : "state-muted");
            }

            if (m_GpuPerfLevelValueLabel != null)
            {
                m_GpuPerfLevelValueLabel.text = snapshot.gpuMetricsAvailable
                    ? string.Format("{0} / 6", snapshot.gpuPerformanceLevel)
                    : "N/A";
                ApplyState(m_GpuPerfLevelValueLabel, snapshot.gpuMetricsAvailable ? GetGpuLoadState(snapshot.gpuPerformanceLevel / 6f * 100f) : "state-muted");
            }

            if (m_GpuLoadGraph != null)
                m_GpuLoadGraph.SetSample(snapshot.gpuLoadPercent / 100f, snapshot.gpuMetricsAvailable);

            if (m_GpuLoadCaptionLabel != null)
            {
                m_GpuLoadCaptionLabel.text = snapshot.gpuMetricsAvailable
                    ? string.Format("GPU Load: {0:0}%", snapshot.gpuLoadPercent)
                    : "No GPU data";
                ApplyState(m_GpuLoadCaptionLabel, snapshot.gpuMetricsAvailable ? "state-info" : "state-muted");
            }

            if (m_GpuClockGraph != null)
            {
                // Normalize clock to 0-1 range using max observed clock
                float normalizedClock = snapshot.gpuClockMaxMhz > 0f
                    ? Mathf.Clamp01(snapshot.gpuClockCurrentMhz / snapshot.gpuClockMaxMhz)
                    : 0f;
                m_GpuClockGraph.SetSample(normalizedClock, snapshot.gpuMetricsAvailable);
            }

            if (m_GpuClockCaptionLabel != null)
            {
                m_GpuClockCaptionLabel.text = snapshot.gpuMetricsAvailable
                    ? string.Format("GPU Clock: {0:0} MHz (max {1:0})", snapshot.gpuClockCurrentMhz, snapshot.gpuClockMaxMhz)
                    : "No GPU data";
                ApplyState(m_GpuClockCaptionLabel, snapshot.gpuMetricsAvailable ? "state-info" : "state-muted");
            }

            if (m_GpuPowerGraph != null)
            {
                // Normalize power to 0-1 range assuming 450W TDP max for high-end GPUs
                float normalizedPower = snapshot.gpuPowerWatts > 0f ? Mathf.Clamp01(snapshot.gpuPowerWatts / 450f) : 0f;
                m_GpuPowerGraph.SetSample(normalizedPower, snapshot.gpuMetricsAvailable);
            }

            if (m_GpuPowerCaptionLabel != null)
            {
                m_GpuPowerCaptionLabel.text = snapshot.gpuMetricsAvailable
                    ? string.Format("GPU Power: {0:0.0} W", snapshot.gpuPowerWatts)
                    : "No GPU data";
                ApplyState(m_GpuPowerCaptionLabel, snapshot.gpuMetricsAvailable ? "state-info" : "state-muted");
            }
        }

        static void SetupStatusLabel(Label label)
        {
            if (label == null)
                return;

            label.AddToClassList("status-value");
        }

        static void ApplyState(Label label, string stateClass)
        {
            if (label == null)
                return;

            label.EnableInClassList("state-muted", stateClass == "state-muted");
            label.EnableInClassList("state-ok", stateClass == "state-ok");
            label.EnableInClassList("state-info", stateClass == "state-info");
            label.EnableInClassList("state-warn", stateClass == "state-warn");
            label.EnableInClassList("state-danger", stateClass == "state-danger");
        }

        static string GetPerformanceModeState(PerformanceMode mode)
        {
            switch (mode)
            {
                case PerformanceMode.Battery:
                    return "state-warn";
                case PerformanceMode.Optimize:
                    return "state-info";
                case PerformanceMode.Standard:
                    return "state-ok";
                default:
                    return "state-muted";
            }
        }

        static string GetWarningState(WarningLevel warningLevel)
        {
            switch (warningLevel)
            {
                case WarningLevel.Throttling:
                    return "state-danger";
                case WarningLevel.ThrottlingImminent:
                    return "state-warn";
                default:
                    return "state-ok";
            }
        }

        static string GetTemperatureState(float temperatureLevel)
        {
            if (temperatureLevel >= 0.8f)
                return "state-danger";

            if (temperatureLevel >= 0.6f)
                return "state-warn";

            if (temperatureLevel >= 0.3f)
                return "state-info";

            return "state-ok";
        }

        static string GetGpuLoadState(float loadPercent)
        {
            if (loadPercent >= 95f) return "state-danger";
            if (loadPercent >= 75f) return "state-warn";
            if (loadPercent >= 30f) return "state-info";
            return "state-ok";
        }

        static string GetGpuPowerState(float powerWatts)
        {
            if (powerWatts >= 300f) return "state-danger";
            if (powerWatts >= 200f) return "state-warn";
            if (powerWatts >= 50f) return "state-info";
            return "state-ok";
        }

        static PerformanceMode GetModeFromInputs(bool lowPowerMode, int targetFrameRate)
        {
            if (lowPowerMode)
                return PerformanceMode.Battery;

            return targetFrameRate >= 60
                ? PerformanceMode.Optimize
                : PerformanceMode.Standard;
        }

        bool TrySetSubsystemProperty(string propertyName, object value)
        {
            var subsystem = Subsystem();
            if (subsystem == null)
                return false;

            var property = subsystem.GetType().GetProperty(propertyName, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            if (property == null || !property.CanWrite)
                return false;

            try
            {
                property.SetValue(subsystem, value);
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        bool TrySetAnySubsystemProperty(string[] propertyNames, object value)
        {
            if (propertyNames == null)
                return false;

            foreach (var propertyName in propertyNames)
            {
                if (TrySetSubsystemProperty(propertyName, value))
                    return true;
            }

            return false;
        }

        SimulatorAdaptivePerformanceSubsystem Subsystem()
        {
            if (!Application.isPlaying)
                return null;

            var loader = AdaptivePerformanceGeneralSettings.Instance?.Manager.activeLoader;
            return loader == null
                ? null
                : loader.GetLoadedSubsystem<SimulatorAdaptivePerformanceSubsystem>();
        }
    }
}