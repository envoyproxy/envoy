package shared

// CounterValue holds the values of a counter returned by
// MetricSnapshot.GetCounter.
type CounterValue struct {
	Value uint64
	Delta uint64
}

// MetricSnapshot gives the stats sink random access to all counters, gauges, and
// text readouts in a single flush cycle. The runtime provides the implementation
// and modules only consume this interface.
//
// Names and text-readout values are decoded directly into a caller-provided byte
// slice, which the runtime reslices and grows (like append) as needed, so a
// single buffer can be reused across every entry to avoid allocating per metric
// (for example writing each name straight to a socket). Pass buf[:0] and assign
// the returned slice back to keep reusing the buffer.
type MetricSnapshot interface {
	// CounterCount returns the number of counters in the snapshot.
	CounterCount() uint64
	// GetCounter writes the counter name at index into name and returns the
	// (possibly reallocated) name slice, the counter values, and whether the
	// index was valid. When false, name is returned unchanged.
	GetCounter(index uint64, name []byte) ([]byte, CounterValue, bool)

	// GaugeCount returns the number of gauges in the snapshot.
	GaugeCount() uint64
	// GetGauge writes the gauge name at index into name and returns the
	// (possibly reallocated) name slice, the gauge value, and whether the index
	// was valid. When false, name is returned unchanged.
	GetGauge(index uint64, name []byte) ([]byte, uint64, bool)

	// TextReadoutCount returns the number of text readouts in the snapshot.
	TextReadoutCount() uint64
	// GetTextReadout writes the text readout name and value at index into name
	// and value, returning the (possibly reallocated) slices and whether the
	// index was valid. When false, name and value are returned unchanged. The
	// name and value slices must not share a backing array, as both are written
	// by the same call.
	GetTextReadout(index uint64, name, value []byte) ([]byte, []byte, bool)
}

// StatSinkHandle is passed to the StatSinkConfigFactory and gives a sink access
// to host services. It provides logging, gauge definition and updates, and
// scheduling work back onto the main thread.
type StatSinkHandle interface {
	// Log writes a message to Envoy's logger at the given level.
	Log(level LogLevel, format string, args ...any)

	// DefineGauge creates a gauge with the given name and returns its ID. It must
	// be called while the sink is being created, from StatSinkConfigFactory.Create.
	// Defining a gauge afterwards returns MetricsFrozen.
	DefineGauge(name string) (MetricID, MetricsResult)

	// SetGauge sets a gauge previously defined with DefineGauge to value. It must
	// be called on the main thread, typically from a function scheduled with the
	// scheduler returned by GetScheduler.
	SetGauge(id MetricID, value uint64) MetricsResult

	// GetScheduler returns a scheduler whose scheduled functions run on the main
	// thread. A sink that aggregates metrics off the main thread, for example on a
	// goroutine started from OnFlush, uses it to publish results with SetGauge. It
	// must be called while the sink is being created, from
	// StatSinkConfigFactory.Create.
	GetScheduler() Scheduler
}
