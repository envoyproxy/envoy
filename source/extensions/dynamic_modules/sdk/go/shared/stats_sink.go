package shared

// CounterValue holds the values of a counter returned by
// MetricSnapshot.GetCounter.
type CounterValue struct {
	Value uint64
	Delta uint64
}

// HistogramValue holds the cumulative sample count and sum of a histogram
// returned by MetricSnapshot.GetHistogram. Per-bucket counts are read with
// MetricSnapshot.HistogramBucketCount and MetricSnapshot.GetHistogramBucket.
type HistogramValue struct {
	SampleCount uint64
	SampleSum   float64
}

// HistogramBucket holds one cumulative bucket of a histogram returned by
// MetricSnapshot.GetHistogramBucket. CumulativeCount is the number of samples at
// or below UpperBound.
type HistogramBucket struct {
	UpperBound      float64
	CumulativeCount uint64
}

// MetricSnapshot gives the stats sink random access to all counters, gauges,
// text readouts, and histograms in a single flush cycle, along with each
// metric's tag-extracted name and tags. The runtime provides the implementation
// and modules only consume this interface.
//
// Names, text-readout values, and tags are decoded directly into a
// caller-provided byte slice, which the runtime reslices and grows (like append)
// as needed, so a single buffer can be reused across every entry to avoid
// allocating per metric (for example writing each name straight to a socket).
// Pass buf[:0] and assign the returned slice back to keep reusing the buffer.
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

	// HistogramCount returns the number of histograms in the snapshot.
	HistogramCount() uint64
	// GetHistogram writes the histogram name at index into name and returns the
	// (possibly reallocated) name slice, the cumulative sample count and sum, and
	// whether the index was valid. When false, name is returned unchanged.
	// Per-bucket counts are read with HistogramBucketCount and GetHistogramBucket.
	GetHistogram(index uint64, name []byte) ([]byte, HistogramValue, bool)
	// HistogramBucketCount returns the number of buckets for the histogram at
	// index, or 0 when the index is out of range.
	HistogramBucketCount(index uint64) uint64
	// GetHistogramBucket returns bucket bucketIndex of the histogram at index and
	// whether both indices were valid. The count is cumulative, meaning the number
	// of samples at or below the bucket upper bound.
	GetHistogramBucket(index, bucketIndex uint64) (HistogramBucket, bool)

	// GetCounterTagExtractedName writes the tag-extracted name of the counter at
	// index into name (the stat name with tag values removed) and returns the
	// (possibly reallocated) slice and whether the index was valid. When false,
	// name is returned unchanged.
	GetCounterTagExtractedName(index uint64, name []byte) ([]byte, bool)
	// CounterTagCount returns the number of tags on the counter at index and
	// whether the index was valid.
	CounterTagCount(index uint64) (uint64, bool)
	// GetCounterTag writes tag tagIndex of the counter at index into name and
	// value and returns the (possibly reallocated) slices and whether both indices
	// were valid. When false, name and value are returned unchanged. The name and
	// value slices must not share a backing array.
	GetCounterTag(index, tagIndex uint64, name, value []byte) ([]byte, []byte, bool)

	// GetGaugeTagExtractedName is the gauge counterpart of
	// GetCounterTagExtractedName.
	GetGaugeTagExtractedName(index uint64, name []byte) ([]byte, bool)
	// GaugeTagCount is the gauge counterpart of CounterTagCount.
	GaugeTagCount(index uint64) (uint64, bool)
	// GetGaugeTag is the gauge counterpart of GetCounterTag.
	GetGaugeTag(index, tagIndex uint64, name, value []byte) ([]byte, []byte, bool)

	// GetTextReadoutTagExtractedName is the text readout counterpart of
	// GetCounterTagExtractedName.
	GetTextReadoutTagExtractedName(index uint64, name []byte) ([]byte, bool)
	// TextReadoutTagCount is the text readout counterpart of CounterTagCount.
	TextReadoutTagCount(index uint64) (uint64, bool)
	// GetTextReadoutTag is the text readout counterpart of GetCounterTag.
	GetTextReadoutTag(index, tagIndex uint64, name, value []byte) ([]byte, []byte, bool)

	// GetHistogramTagExtractedName is the histogram counterpart of
	// GetCounterTagExtractedName.
	GetHistogramTagExtractedName(index uint64, name []byte) ([]byte, bool)
	// HistogramTagCount is the histogram counterpart of CounterTagCount.
	HistogramTagCount(index uint64) (uint64, bool)
	// GetHistogramTag is the histogram counterpart of GetCounterTag.
	GetHistogramTag(index, tagIndex uint64, name, value []byte) ([]byte, []byte, bool)
}

// StatSinkHandle is passed to the StatSinkConfigFactory and gives a sink access
// to host services. It provides logging, gauge definition and updates, and
// scheduling work back onto the main thread.
type StatSinkHandle interface {
	CommonHandle

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
