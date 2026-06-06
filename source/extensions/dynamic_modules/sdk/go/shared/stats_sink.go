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
	// index was valid. When false, name and value are returned unchanged. name
	// and value must not share a backing array, as both are written by the same
	// call.
	GetTextReadout(index uint64, name, value []byte) ([]byte, []byte, bool)
}

// StatSinkHandle is passed to StatSink methods. It currently exposes only
// logging and exists so future capabilities can be added without breaking
// existing modules.
type StatSinkHandle interface {
	Log(level LogLevel, format string, args ...any)
}
