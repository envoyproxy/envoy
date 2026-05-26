package shared

// CounterSnapshot is one counter in a MetricSnapshot.
// The Name field shares memory with Envoy's buffer and is only valid during the
// enclosing OnFlush callback — copy it with ToString() if you need to keep it.
type CounterSnapshot struct {
	Name  UnsafeEnvoyBuffer
	Value uint64
	Delta uint64
}

// GaugeSnapshot is one gauge in a MetricSnapshot. Name is only valid during the
// enclosing OnFlush callback; see CounterSnapshot for details.
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
// text readouts in a single flush cycle. Implementations are provided by the
// runtime; modules only consume this interface.
//
// NOTE: the returned UnsafeEnvoyBuffer fields point into Envoy-owned memory
// that lives only for the duration of the OnFlush call. Copy any data you need
// to retain.
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

// StatSinkHandle is passed to StatSink methods. Today it only exposes logging;
// it exists so future capabilities (e.g. worker index, per-sink shared data)
// can be added without breaking existing modules.
type StatSinkHandle interface {
	Log(level LogLevel, format string, args ...any)
}
