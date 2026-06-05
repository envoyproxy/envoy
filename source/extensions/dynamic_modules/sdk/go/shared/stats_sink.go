package shared

// CounterSnapshot is one counter in a MetricSnapshot. The Name field points into
// Envoy-owned memory that is only valid during the enclosing OnFlush callback.
// Copy it with ToString() if you need to keep it.
type CounterSnapshot struct {
	Name  UnsafeEnvoyBuffer
	Value uint64
	Delta uint64
}

// GaugeSnapshot is one gauge in a MetricSnapshot. Name is only valid during the
// enclosing OnFlush callback. See CounterSnapshot for details.
type GaugeSnapshot struct {
	Name  UnsafeEnvoyBuffer
	Value uint64
}

// TextReadoutSnapshot is one text readout in a MetricSnapshot. Name and Value
// are only valid during the enclosing OnFlush callback.
type TextReadoutSnapshot struct {
	Name  UnsafeEnvoyBuffer
	Value UnsafeEnvoyBuffer
}

// MetricSnapshot gives the stats sink random access to all counters, gauges, and
// text readouts in a single flush cycle. The runtime provides the implementation
// and modules only consume this interface.
//
// The returned UnsafeEnvoyBuffer fields point into Envoy-owned memory that is
// only valid during the OnFlush call. Copy any data you need to retain.
type MetricSnapshot interface {
	// CounterCount returns the number of counters in the snapshot.
	CounterCount() uint64
	// GetCounter returns the counter at the given index. If the index is out of
	// range, the second return value is false.
	GetCounter(index uint64) (CounterSnapshot, bool)

	// GaugeCount returns the number of gauges in the snapshot.
	GaugeCount() uint64
	// GetGauge returns the gauge at the given index.
	GetGauge(index uint64) (GaugeSnapshot, bool)

	// TextReadoutCount returns the number of text readouts in the snapshot.
	TextReadoutCount() uint64
	// GetTextReadout returns the text readout at the given index.
	GetTextReadout(index uint64) (TextReadoutSnapshot, bool)
}

// StatSinkHandle is passed to StatSink methods. It currently exposes only
// logging and exists so future capabilities can be added without breaking
// existing modules.
type StatSinkHandle interface {
	Log(level LogLevel, format string, args ...any)
}
