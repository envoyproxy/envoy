package shared

// StatSink is the per-config stats sink instance. A single StatSink lives for
// the lifetime of the DynamicModuleStatsSink config in Envoy, and its methods
// are called on every flush and for every histogram observation.
//
// Thread-safety: OnFlush is serialized on Envoy's stats flush thread. However,
// OnHistogramComplete is called SYNCHRONOUSLY from whichever worker thread
// recorded the histogram sample, so implementations must be thread-safe and
// fast. Buffer or batch if observation volume is high.
type StatSink interface {
	// OnFlush is called periodically (every stats_flush_interval) with a full
	// snapshot of metrics that passed the SinkPredicates filter.
	OnFlush(snapshot MetricSnapshot)

	// OnHistogramComplete is called synchronously for every histogram
	// observation. The name buffer is only valid for the duration of this call.
	OnHistogramComplete(name UnsafeEnvoyBuffer, value uint64)

	// OnDestroy is called when the stats sink config is being torn down. This
	// is the place to close sockets, flush batches, etc.
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
// Implementations should be stateless — per-config state lives on the returned
// StatSink.
type StatSinkConfigFactory interface {
	// Create parses the sink configuration and returns a StatSink instance, or
	// an error if the configuration is invalid. Returning a nil StatSink with
	// no error is also treated as a failure.
	//
	// handle provides runtime services (logging today).
	// config contains the bytes passed via the sink_config field of the
	// DynamicModuleStatsSink proto. Encoding depends on the Any type used in
	// the config (e.g. raw bytes for BytesValue, JSON for Struct).
	Create(handle StatSinkHandle, config []byte) (StatSink, error)
}

// EmptyStatSinkConfigFactory builds an EmptyStatSink. Useful for testing.
type EmptyStatSinkConfigFactory struct{}

func (f *EmptyStatSinkConfigFactory) Create(handle StatSinkHandle, unparsedConfig []byte) (StatSink, error) {
	return &EmptyStatSink{}, nil
}
