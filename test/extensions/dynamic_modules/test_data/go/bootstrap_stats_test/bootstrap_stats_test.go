// Test module for Bootstrap stats access + metrics definition. Mirrors
// test_data/rust/bootstrap_stats_test.rs.
//
// Two phases:
//   1. config_new — define unlabeled and labeled counters/gauges/histograms, mutate them,
//      and emit log lines the C++ test asserts on.
//   2. on_server_initialized — exercise read-only stats access (get_counter_value /
//      get_gauge_value / get_histogram_summary / iterate_*) including non-existent keys.
package main

import (
	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterBootstrapExtensionConfigFactories(map[string]shared.BootstrapExtensionConfigFactory{
		"test": &statsConfigFactory{},
	})
}

func main() {}

type statsConfigFactory struct {
	shared.EmptyBootstrapExtensionConfigFactory
}

func (f *statsConfigFactory) Create(handle shared.BootstrapExtensionConfigHandle, _ []byte) (shared.BootstrapExtension, error) {
	// ---- Unlabeled metrics (no label names) ----
	counterID, res := handle.DefineCounter("refresh_success_total", nil)
	if res != shared.MetricsSuccess {
		panic("failed to define counter")
	}
	gaugeID, res := handle.DefineGauge("connection_state", nil)
	if res != shared.MetricsSuccess {
		panic("failed to define gauge")
	}
	histogramID, res := handle.DefineHistogram("refresh_duration_ms", nil)
	if res != shared.MetricsSuccess {
		panic("failed to define histogram")
	}

	// Counter +3, +2 = 5.
	if r := handle.IncrementCounter(counterID, nil, 3); r != shared.MetricsSuccess {
		panic("failed to increment counter")
	}
	if r := handle.IncrementCounter(counterID, nil, 2); r != shared.MetricsSuccess {
		panic("failed to increment counter")
	}
	sdk.Log(shared.LogLevelInfo, "Counter incremented to expected value of 5")

	// Gauge: set 100, +10, -30 = 80.
	if r := handle.SetGauge(gaugeID, nil, 100); r != shared.MetricsSuccess {
		panic("failed to set gauge")
	}
	if r := handle.IncrementGauge(gaugeID, nil, 10); r != shared.MetricsSuccess {
		panic("failed to increment gauge")
	}
	if r := handle.DecrementGauge(gaugeID, nil, 30); r != shared.MetricsSuccess {
		panic("failed to decrement gauge")
	}
	sdk.Log(shared.LogLevelInfo, "Gauge set to expected value of 80")

	// Histogram: record two values.
	if r := handle.RecordHistogramValue(histogramID, nil, 42); r != shared.MetricsSuccess {
		panic("failed to record histogram value")
	}
	if r := handle.RecordHistogramValue(histogramID, nil, 100); r != shared.MetricsSuccess {
		panic("failed to record histogram value")
	}
	sdk.Log(shared.LogLevelInfo, "Histogram values recorded successfully")

	// ---- Labeled (vec) metrics ----
	counterVecID, res := handle.DefineCounter("request_total", []string{"method", "status"})
	if res != shared.MetricsSuccess {
		panic("failed to define counter vec")
	}
	if r := handle.IncrementCounter(counterVecID, []string{"GET", "200"}, 7); r != shared.MetricsSuccess {
		panic("failed to increment counter vec")
	}
	sdk.Log(shared.LogLevelInfo, "Counter vec incremented successfully")

	gaugeVecID, res := handle.DefineGauge("active_connections", []string{"upstream"})
	if res != shared.MetricsSuccess {
		panic("failed to define gauge vec")
	}
	if r := handle.SetGauge(gaugeVecID, []string{"svc_a"}, 50); r != shared.MetricsSuccess {
		panic("failed to set gauge vec")
	}
	if r := handle.IncrementGauge(gaugeVecID, []string{"svc_a"}, 5); r != shared.MetricsSuccess {
		panic("failed to increment gauge vec")
	}
	if r := handle.DecrementGauge(gaugeVecID, []string{"svc_a"}, 10); r != shared.MetricsSuccess {
		panic("failed to decrement gauge vec")
	}
	sdk.Log(shared.LogLevelInfo, "Gauge vec manipulated successfully")

	histogramVecID, res := handle.DefineHistogram("latency_ms", []string{"endpoint"})
	if res != shared.MetricsSuccess {
		panic("failed to define histogram vec")
	}
	if r := handle.RecordHistogramValue(histogramVecID, []string{"backend_a"}, 15); r != shared.MetricsSuccess {
		panic("failed to record histogram vec value")
	}
	sdk.Log(shared.LogLevelInfo, "Histogram vec recorded successfully")

	sdk.Log(shared.LogLevelInfo, "Bootstrap metrics definition and update test completed successfully!")

	handle.SignalInitComplete()
	return &statsExtension{}, nil
}

type statsExtension struct {
	shared.EmptyBootstrapExtension
}

func (*statsExtension) OnServerInitialized(handle shared.BootstrapExtensionHandle) {
	// server.live should exist after init — we don't assert on its presence (some test
	// configs may omit it) but we do log if found.
	if value, ok := handle.GetGaugeValue("server.live"); ok {
		sdk.Log(shared.LogLevelInfo, "Found server.live gauge with value: %d", value)
	} else {
		sdk.Log(shared.LogLevelInfo, "server.live gauge not found (this is expected in some test configs)")
	}

	// Iterate counters and gauges to exercise the stats-iteration trampolines.
	counterCount := 0
	handle.IterateCounters(func(_ shared.UnsafeEnvoyBuffer, _ uint64) shared.StatsIterationAction {
		counterCount++
		return shared.StatsIterationActionContinue
	})
	sdk.Log(shared.LogLevelInfo, "Found %d counters in stats store", counterCount)

	gaugeCount := 0
	handle.IterateGauges(func(_ shared.UnsafeEnvoyBuffer, _ uint64) shared.StatsIterationAction {
		gaugeCount++
		return shared.StatsIterationActionContinue
	})
	sdk.Log(shared.LogLevelInfo, "Found %d gauges in stats store", gaugeCount)

	if _, ok := handle.GetCounterValue("non.existent.counter"); !ok {
		sdk.Log(shared.LogLevelInfo, "Correctly returned None for non-existent counter")
	}
	if _, ok := handle.GetGaugeValue("non.existent.gauge"); !ok {
		sdk.Log(shared.LogLevelInfo, "Correctly returned None for non-existent gauge")
	}
	if _, _, ok := handle.GetHistogramSummary("non.existent.histogram"); !ok {
		sdk.Log(shared.LogLevelInfo, "Correctly returned None for non-existent histogram")
	}

	sdk.Log(shared.LogLevelInfo, "Bootstrap stats access test completed successfully!")
}

func (*statsExtension) OnWorkerThreadInitialized(_ shared.BootstrapExtensionHandle) {
	sdk.Log(shared.LogLevelInfo, "Bootstrap extension worker thread initialized with stats access!")
}
