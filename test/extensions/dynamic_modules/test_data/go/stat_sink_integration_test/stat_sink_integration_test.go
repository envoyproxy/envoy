package main

import (
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

	for i := uint64(0); i < counterCount; i++ {
		snapshot.GetCounter(i)
	}
	for i := uint64(0); i < gaugeCount; i++ {
		snapshot.GetGauge(i)
	}
	textReadoutCount := snapshot.TextReadoutCount()
	for i := uint64(0); i < textReadoutCount; i++ {
		snapshot.GetTextReadout(i)
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
