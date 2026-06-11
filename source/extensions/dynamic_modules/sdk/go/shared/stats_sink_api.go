package shared

// StatSink is the per-config stats sink instance. A single StatSink lives for
// the lifetime of the DynamicModuleStatsSink config in Envoy, and its methods
// are called on every flush and for every histogram observation.
//
// OnFlush runs on the main thread during the periodic stats flush.
// OnHistogramComplete runs synchronously on the worker thread that recorded the
// sample, so implementations must be thread-safe and must not block.
type StatSink interface {
	// OnFlush is called periodically with a full snapshot of the metrics that
	// passed the configured sink predicates.
	OnFlush(snapshot MetricSnapshot)

	// OnHistogramComplete is called synchronously for every histogram
	// observation. The name buffer is only valid for the duration of this call.
	OnHistogramComplete(name UnsafeEnvoyBuffer, value uint64)

	// OnDestroy is called when the stats sink config is being torn down. Close
	// sockets and flush any buffered state here.
	OnDestroy()
}

// EmptyStatSink is a no-op StatSink. Embed it to get forward-compatible
// defaults for methods you don't care about.
type EmptyStatSink struct{}

func (s *EmptyStatSink) OnFlush(snapshot MetricSnapshot) {
}

func (s *EmptyStatSink) OnHistogramComplete(name UnsafeEnvoyBuffer, value uint64) {
}

func (s *EmptyStatSink) OnDestroy() {
}

// StatSinkConfigFactory parses the configuration for one stats sink and builds
// a StatSink. It runs once per sink config on the main thread at server start.
// Implementations should be stateless and keep per-config state on the returned
// StatSink.
type StatSinkConfigFactory interface {
	// Create parses the sink configuration and returns a StatSink instance, or
	// an error if the configuration is invalid. Returning a nil StatSink with
	// no error is also treated as a failure.
	//
	// handle provides runtime services such as logging. config contains the
	// bytes passed via the sink_config field of the DynamicModuleStatsSink
	// proto. The encoding depends on the Any type used in the config, for
	// example raw bytes for BytesValue and JSON for Struct.
	Create(handle StatSinkHandle, config []byte) (StatSink, error)
}

// EmptyStatSinkConfigFactory builds an EmptyStatSink. Useful for testing.
type EmptyStatSinkConfigFactory struct{}

func (f *EmptyStatSinkConfigFactory) Create(handle StatSinkHandle, unparsedConfig []byte) (StatSink, error) {
	return &EmptyStatSink{}, nil
}
