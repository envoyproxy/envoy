package fake

import (
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

var _ shared.MetricSnapshot = (*FakeMetricSnapshot)(nil)

// FakeTag is one tag on a fake metric.
type FakeTag struct {
	Name  string
	Value string
}

// FakeCounter is one counter in a FakeMetricSnapshot.
type FakeCounter struct {
	Name             string
	Value            uint64
	Delta            uint64
	TagExtractedName string
	Tags             []FakeTag
}

// FakeGauge is one gauge in a FakeMetricSnapshot.
type FakeGauge struct {
	Name             string
	Value            uint64
	TagExtractedName string
	Tags             []FakeTag
}

// FakeTextReadout is one text readout in a FakeMetricSnapshot.
type FakeTextReadout struct {
	Name             string
	Value            string
	TagExtractedName string
	Tags             []FakeTag
}

// FakeHistogram is one histogram in a FakeMetricSnapshot.
type FakeHistogram struct {
	Name             string
	SampleCount      uint64
	SampleSum        float64
	Buckets          []shared.HistogramBucket
	TagExtractedName string
	Tags             []FakeTag
}

// FakeMetricSnapshot is an in-memory shared.MetricSnapshot for unit-testing a
// stats sink OnFlush implementation without loading the module into Envoy. It
// honors the same append-and-reuse buffer contract as the runtime and returns
// the caller buffers unchanged for an out-of-range index.
type FakeMetricSnapshot struct {
	Counters     []FakeCounter
	Gauges       []FakeGauge
	TextReadouts []FakeTextReadout
	Histograms   []FakeHistogram
}

func (s *FakeMetricSnapshot) CounterCount() uint64 { return uint64(len(s.Counters)) }

func (s *FakeMetricSnapshot) GetCounter(index uint64, name []byte) ([]byte, shared.CounterValue, bool) {
	if index >= uint64(len(s.Counters)) {
		return name, shared.CounterValue{}, false
	}
	c := s.Counters[index]
	return append(name[:0], c.Name...), shared.CounterValue{Value: c.Value, Delta: c.Delta}, true
}

func (s *FakeMetricSnapshot) GaugeCount() uint64 { return uint64(len(s.Gauges)) }

func (s *FakeMetricSnapshot) GetGauge(index uint64, name []byte) ([]byte, uint64, bool) {
	if index >= uint64(len(s.Gauges)) {
		return name, 0, false
	}
	g := s.Gauges[index]
	return append(name[:0], g.Name...), g.Value, true
}

func (s *FakeMetricSnapshot) TextReadoutCount() uint64 { return uint64(len(s.TextReadouts)) }

func (s *FakeMetricSnapshot) GetTextReadout(index uint64, name, value []byte) ([]byte, []byte, bool) {
	if index >= uint64(len(s.TextReadouts)) {
		return name, value, false
	}
	t := s.TextReadouts[index]
	return append(name[:0], t.Name...), append(value[:0], t.Value...), true
}

func (s *FakeMetricSnapshot) HistogramCount() uint64 { return uint64(len(s.Histograms)) }

func (s *FakeMetricSnapshot) GetHistogram(index uint64, name []byte) ([]byte, shared.HistogramValue, bool) {
	if index >= uint64(len(s.Histograms)) {
		return name, shared.HistogramValue{}, false
	}
	h := s.Histograms[index]
	return append(name[:0], h.Name...),
		shared.HistogramValue{SampleCount: h.SampleCount, SampleSum: h.SampleSum}, true
}

func (s *FakeMetricSnapshot) HistogramBucketCount(index uint64) uint64 {
	if index >= uint64(len(s.Histograms)) {
		return 0
	}
	return uint64(len(s.Histograms[index].Buckets))
}

func (s *FakeMetricSnapshot) GetHistogramBucket(index, bucketIndex uint64) (shared.HistogramBucket, bool) {
	if index >= uint64(len(s.Histograms)) || bucketIndex >= uint64(len(s.Histograms[index].Buckets)) {
		return shared.HistogramBucket{}, false
	}
	return s.Histograms[index].Buckets[bucketIndex], true
}

func (s *FakeMetricSnapshot) GetCounterTagExtractedName(index uint64, name []byte) ([]byte, bool) {
	return tagExtractedNameAt(s.Counters, index, name, func(c FakeCounter) string { return c.TagExtractedName })
}

func (s *FakeMetricSnapshot) CounterTagCount(index uint64) (uint64, bool) {
	return tagCountAt(s.Counters, index, func(c FakeCounter) []FakeTag { return c.Tags })
}

func (s *FakeMetricSnapshot) GetCounterTag(index, tagIndex uint64, name, value []byte) ([]byte, []byte, bool) {
	return tagAt(s.Counters, index, tagIndex, func(c FakeCounter) []FakeTag { return c.Tags }, name, value)
}

func (s *FakeMetricSnapshot) GetGaugeTagExtractedName(index uint64, name []byte) ([]byte, bool) {
	return tagExtractedNameAt(s.Gauges, index, name, func(g FakeGauge) string { return g.TagExtractedName })
}

func (s *FakeMetricSnapshot) GaugeTagCount(index uint64) (uint64, bool) {
	return tagCountAt(s.Gauges, index, func(g FakeGauge) []FakeTag { return g.Tags })
}

func (s *FakeMetricSnapshot) GetGaugeTag(index, tagIndex uint64, name, value []byte) ([]byte, []byte, bool) {
	return tagAt(s.Gauges, index, tagIndex, func(g FakeGauge) []FakeTag { return g.Tags }, name, value)
}

func (s *FakeMetricSnapshot) GetTextReadoutTagExtractedName(index uint64, name []byte) ([]byte, bool) {
	return tagExtractedNameAt(s.TextReadouts, index, name, func(t FakeTextReadout) string { return t.TagExtractedName })
}

func (s *FakeMetricSnapshot) TextReadoutTagCount(index uint64) (uint64, bool) {
	return tagCountAt(s.TextReadouts, index, func(t FakeTextReadout) []FakeTag { return t.Tags })
}

func (s *FakeMetricSnapshot) GetTextReadoutTag(index, tagIndex uint64, name, value []byte) ([]byte, []byte, bool) {
	return tagAt(s.TextReadouts, index, tagIndex, func(t FakeTextReadout) []FakeTag { return t.Tags }, name, value)
}

func (s *FakeMetricSnapshot) GetHistogramTagExtractedName(index uint64, name []byte) ([]byte, bool) {
	return tagExtractedNameAt(s.Histograms, index, name, func(h FakeHistogram) string { return h.TagExtractedName })
}

func (s *FakeMetricSnapshot) HistogramTagCount(index uint64) (uint64, bool) {
	return tagCountAt(s.Histograms, index, func(h FakeHistogram) []FakeTag { return h.Tags })
}

func (s *FakeMetricSnapshot) GetHistogramTag(index, tagIndex uint64, name, value []byte) ([]byte, []byte, bool) {
	return tagAt(s.Histograms, index, tagIndex, func(h FakeHistogram) []FakeTag { return h.Tags }, name, value)
}

// tagExtractedNameAt writes the tag-extracted name of metrics[index] into name, or leaves name
// unchanged when the index is out of range.
func tagExtractedNameAt[T any](metrics []T, index uint64, name []byte, teName func(T) string) ([]byte, bool) {
	if index >= uint64(len(metrics)) {
		return name, false
	}
	return append(name[:0], teName(metrics[index])...), true
}

// tagCountAt returns the number of tags on metrics[index], or false when the index is out of range.
func tagCountAt[T any](metrics []T, index uint64, tags func(T) []FakeTag) (uint64, bool) {
	if index >= uint64(len(metrics)) {
		return 0, false
	}
	return uint64(len(tags(metrics[index]))), true
}

// tagAt writes tag tagIndex of metrics[index] into name and value, or leaves both unchanged when
// either index is out of range.
func tagAt[T any](metrics []T, index, tagIndex uint64, tags func(T) []FakeTag, name, value []byte) ([]byte, []byte, bool) {
	if index >= uint64(len(metrics)) {
		return name, value, false
	}
	t := tags(metrics[index])
	if tagIndex >= uint64(len(t)) {
		return name, value, false
	}
	return append(name[:0], t[tagIndex].Name...), append(value[:0], t[tagIndex].Value...), true
}
