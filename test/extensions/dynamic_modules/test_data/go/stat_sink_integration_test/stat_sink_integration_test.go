package main

import (
	"bytes"
	"fmt"

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
	return &integrationTestSink{handle: handle}, nil
}

type integrationTestSink struct {
	shared.EmptyStatSink
	handle shared.StatSinkHandle
}

func (s *integrationTestSink) OnFlush(snapshot shared.MetricSnapshot) {
	counterCount := snapshot.CounterCount()
	gaugeCount := snapshot.GaugeCount()

	// Reuse a single name/value buffer across every entry, decoding each name straight into
	// module-owned memory. This is the allocation-free pattern the buffer API enables (for
	// example writing each name to a socket). Start from zero capacity so the first decode
	// exercises the SDK grow-and-retry path, after which the buffer stays sized for later entries.
	var name []byte
	var value []byte
	for i := uint64(0); i < counterCount; i++ {
		name, _, _ = snapshot.GetCounter(i, name[:0])
	}
	// Decode every gauge name and look for the always-present "server.uptime" gauge, which proves a
	// name round-trips byte-for-byte through the buffer API end to end.
	foundUptime := false
	for i := uint64(0); i < gaugeCount; i++ {
		name, _, _ = snapshot.GetGauge(i, name[:0])
		if bytes.Equal(name, []byte("server.uptime")) {
			foundUptime = true
		}
	}
	textReadoutCount := snapshot.TextReadoutCount()
	for i := uint64(0); i < textReadoutCount; i++ {
		name, value, _ = snapshot.GetTextReadout(i, name[:0], value[:0])
	}

	if foundUptime {
		s.handle.Log(shared.LogLevelInfo, "stat sink integration test: found gauge server.uptime")
	}
	s.handle.Log(shared.LogLevelInfo, fmt.Sprintf(
		"stat sink integration test: flush called counters=%d gauges=%d", counterCount, gaugeCount))
}

func (s *integrationTestSink) OnHistogramComplete(name shared.UnsafeEnvoyBuffer, value uint64) {
	s.handle.Log(shared.LogLevelInfo, fmt.Sprintf(
		"stat sink integration test: histogram complete: %s", name.ToUnsafeString()))
}

func (s *integrationTestSink) OnDestroy() {
	s.handle.Log(shared.LogLevelInfo, "stat sink integration test: config_destroy called")
}
