package main

import (
	"bytes"
	"fmt"
	"runtime"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterStatSinkConfigFactories(map[string]shared.StatSinkConfigFactory{
		"integration_test": &integrationTestConfigFactory{},
	})
}

func main() {}

type integrationTestConfigFactory struct{}

func (f *integrationTestConfigFactory) Create(handle shared.StatSinkHandle, config []byte) (shared.StatSink, error) {
	handle.Log(shared.LogLevelInfo, "stat sink integration test: config_new called")

	// Define a gauge to publish the aggregated result into, and a scheduler to post that result back
	// to the main thread once it has been computed off the main thread.
	gaugeID, result := handle.DefineGauge("integration_aggregated_counters")
	if result != shared.MetricsSuccess {
		return nil, fmt.Errorf("failed to define gauge: %v", result)
	}

	sink := &integrationTestSink{
		handle:    handle,
		gaugeID:   gaugeID,
		scheduler: handle.GetScheduler(),
		flushes:   make(chan []uint64, 16),
		done:      make(chan struct{}),
	}
	// Aggregate snapshots on a dedicated goroutine to model a sink that offloads work from the main
	// thread. The goroutine publishes each aggregated total back on the main thread via the scheduler.
	go sink.aggregateLoop()
	return sink, nil
}

type integrationTestSink struct {
	shared.EmptyStatSink
	handle    shared.StatSinkHandle
	gaugeID   shared.MetricID
	scheduler shared.Scheduler
	flushes   chan []uint64
	done      chan struct{}
}

func (s *integrationTestSink) aggregateLoop() {
	for values := range s.flushes {
		var total uint64
		for _, value := range values {
			total += value
		}
		// total is a fresh variable each iteration, so the scheduled closure captures the right value.
		// Schedule runs on the main thread, where setting a gauge is safe.
		s.scheduler.Schedule(func() {
			s.handle.SetGauge(s.gaugeID, total)
			s.handle.Log(shared.LogLevelInfo, fmt.Sprintf(
				"stat sink integration test: scheduled publish total=%d", total))
		})
	}
	close(s.done)
}

// tagsRoundTrip reads a metric's tag-extracted name and every tag the count reports, confirming the
// count and the per-tag reader agree. It returns false if the tag-extracted name or any tag the
// count implies fails to read. The getters are passed as bound method values so one body serves all
// four metric types.
func tagsRoundTrip(
	extractedName func(uint64, []byte) ([]byte, bool),
	tagCount func(uint64) (uint64, bool),
	tag func(uint64, uint64, []byte, []byte) ([]byte, []byte, bool),
	index uint64,
) bool {
	var name, value []byte
	name, ok := extractedName(index, name[:0])
	if !ok {
		return false
	}
	count, ok := tagCount(index)
	if !ok {
		return false
	}
	for t := uint64(0); t < count; t++ {
		if name, value, ok = tag(index, t, name[:0], value[:0]); !ok {
			return false
		}
	}
	return true
}

func (s *integrationTestSink) OnFlush(snapshot shared.MetricSnapshot) {
	counterCount := snapshot.CounterCount()
	gaugeCount := snapshot.GaugeCount()
	histogramCount := snapshot.HistogramCount()

	// Reuse a single name/value buffer across every entry, decoding each name straight into
	// module-owned memory. This is the allocation-free pattern the buffer API enables (for
	// example writing each name to a socket). Start from zero capacity so the first decode
	// exercises the SDK grow-and-retry path, after which the buffer stays sized for later entries.
	var name []byte
	var value []byte
	var tagName []byte
	var tagValue []byte

	// Every tag index the count reports must resolve through the per-tag reader across all metric
	// types. A mismatch means the count and the tag getter disagree.
	tagsConsistent := true

	// Copy the counter values so they outlive this call, then hand them to the worker goroutine to
	// aggregate off the main thread.
	values := make([]uint64, 0, counterCount)
	for i := uint64(0); i < counterCount; i++ {
		var counter shared.CounterValue
		var ok bool
		name, counter, ok = snapshot.GetCounter(i, name[:0])
		if ok {
			values = append(values, counter.Value)
		}
		if !tagsRoundTrip(snapshot.GetCounterTagExtractedName, snapshot.CounterTagCount,
			snapshot.GetCounterTag, i) {
			tagsConsistent = false
		}
	}

	// Decode every gauge name and look for the always-present "server.uptime" gauge, which proves a
	// name round-trips byte-for-byte through the buffer API end to end. Also reconstruct the
	// dimensional name of a tagged gauge from its tag-extracted name and tags, the way a
	// Prometheus-style sink would. "cluster.cluster_0.membership_total" carries a single
	// "envoy.cluster_name" tag, so the tag getters must yield tag-extracted name
	// "cluster.membership_total" and tag ("envoy.cluster_name", "cluster_0").
	foundUptime := false
	foundTaggedGauge := false
	for i := uint64(0); i < gaugeCount; i++ {
		name, _, _ = snapshot.GetGauge(i, name[:0])
		if bytes.Equal(name, []byte("server.uptime")) {
			foundUptime = true
		}
		name, _ = snapshot.GetGaugeTagExtractedName(i, name[:0])
		if bytes.Equal(name, []byte("cluster.membership_total")) {
			if count, ok := snapshot.GaugeTagCount(i); ok && count == 1 {
				var ok2 bool
				tagName, tagValue, ok2 = snapshot.GetGaugeTag(i, 0, tagName[:0], tagValue[:0])
				if ok2 && bytes.Equal(tagName, []byte("envoy.cluster_name")) &&
					bytes.Equal(tagValue, []byte("cluster_0")) {
					foundTaggedGauge = true
				}
			}
		}
		if !tagsRoundTrip(snapshot.GetGaugeTagExtractedName, snapshot.GaugeTagCount,
			snapshot.GetGaugeTag, i) {
			tagsConsistent = false
		}
	}

	textReadoutCount := snapshot.TextReadoutCount()
	for i := uint64(0); i < textReadoutCount; i++ {
		name, value, _ = snapshot.GetTextReadout(i, name[:0], value[:0])
		if !tagsRoundTrip(snapshot.GetTextReadoutTagExtractedName, snapshot.TextReadoutTagCount,
			snapshot.GetTextReadoutTag, i) {
			tagsConsistent = false
		}
	}

	// Read every histogram's scalar values and its cumulative buckets. A non-empty bucket list whose
	// last cumulative count equals the sample count confirms the buckets decode in the cumulative
	// form Envoy produces.
	for i := uint64(0); i < histogramCount; i++ {
		var histogram shared.HistogramValue
		var ok bool
		name, histogram, ok = snapshot.GetHistogram(i, name[:0])
		if !ok {
			continue
		}
		bucketCount := snapshot.HistogramBucketCount(i)
		var lastCumulative uint64
		for b := uint64(0); b < bucketCount; b++ {
			if bucket, bucketOk := snapshot.GetHistogramBucket(i, b); bucketOk {
				lastCumulative = bucket.CumulativeCount
			}
		}
		if bucketCount > 0 && lastCumulative == histogram.SampleCount {
			s.handle.Log(shared.LogLevelInfo, fmt.Sprintf(
				"stat sink integration test: histogram buckets cumulative for %s", name))
		}
		if !tagsRoundTrip(snapshot.GetHistogramTagExtractedName, snapshot.HistogramTagCount,
			snapshot.GetHistogramTag, i) {
			tagsConsistent = false
		}
	}

	if foundUptime {
		s.handle.Log(shared.LogLevelInfo, "stat sink integration test: found gauge server.uptime")
	}
	if foundTaggedGauge && tagsConsistent {
		s.handle.Log(shared.LogLevelInfo, "stat sink integration test: reconstructed tagged gauge "+
			"cluster.membership_total envoy.cluster_name=cluster_0")
	}
	s.handle.Log(shared.LogLevelInfo, fmt.Sprintf(
		"stat sink integration test: flush called counters=%d gauges=%d histograms=%d",
		counterCount, gaugeCount, histogramCount))

	// Hand the values to the worker without ever stalling the flush path. The buffered channel starts
	// empty so the first flush always enqueues, and counter sums are monotonic, so the gauge still
	// reaches a non-zero value even if a later flush is dropped under load.
	select {
	case s.flushes <- values:
	default:
	}
}

func (s *integrationTestSink) OnHistogramComplete(name shared.UnsafeEnvoyBuffer, value uint64) {
	s.handle.Log(shared.LogLevelInfo, fmt.Sprintf(
		"stat sink integration test: histogram complete: %s", name.ToUnsafeString()))
}

func (s *integrationTestSink) OnDestroy() {
	// Stop the worker goroutine and wait for it to finish before the config is torn down.
	close(s.flushes)
	<-s.done
	s.handle.Log(shared.LogLevelInfo, "stat sink integration test: config_destroy called")
	// Drop the scheduler reference and force the GC to release the scheduler and its Envoy-side
	// resource through the finalizer before process exit.
	s.scheduler = nil
	runtime.GC()
}
