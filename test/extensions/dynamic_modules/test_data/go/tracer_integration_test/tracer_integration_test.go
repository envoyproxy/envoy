// Tracer integration test module.
//
// Registers a "test_tracer" that returns a recording TracerSpan from StartSpan. The span
// records started/finished counts in process-wide atomics for diagnostics; the test
// asserts the tracer was loaded successfully (request flows complete) and span dispatch
// runs without crashing.
package main

import (
	"sync/atomic"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterTracerConfigFactories(map[string]shared.TracerConfigFactory{
		"test_tracer": &testTracerConfigFactory{},
	})
}

func main() {} //nolint:all

var (
	startedSpans  atomic.Uint32
	finishedSpans atomic.Uint32
)

type testTracerConfigFactory struct {
	shared.EmptyTracerConfigFactory
}

func (testTracerConfigFactory) Create(_ shared.TracerConfigHandle, _ []byte) (shared.Tracer, error) {
	return &testTracer{}, nil
}

type testTracer struct {
	shared.EmptyTracer
}

func (*testTracer) StartSpan(_ shared.TracerSpanContext, _ string, _ bool, _ shared.TraceReason) shared.TracerSpan {
	startedSpans.Add(1)
	return &testSpan{}
}

// testSpan inherits all default no-ops from EmptyTracerSpan; override Finish to track
// completion. Envoy will call OnDestroy after Finish.
type testSpan struct {
	shared.EmptyTracerSpan
}

func (*testSpan) Finish() {
	finishedSpans.Add(1)
}
