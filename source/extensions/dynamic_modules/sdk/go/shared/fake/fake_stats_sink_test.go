package fake

import (
	"testing"

	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func populatedSnapshot() *FakeMetricSnapshot {
	return &FakeMetricSnapshot{
		Counters: []FakeCounter{{
			Name:             "cluster.foo.rq_total",
			Value:            10,
			Delta:            5,
			TagExtractedName: "cluster.rq_total",
			Tags:             []FakeTag{{"envoy.cluster_name", "foo"}, {"region", "us-east-4"}},
		}},
		Gauges: []FakeGauge{{
			Name:             "cluster.foo.membership_healthy",
			Value:            42,
			TagExtractedName: "cluster.membership_healthy",
			Tags:             []FakeTag{{"envoy.cluster_name", "foo"}},
		}},
		TextReadouts: []FakeTextReadout{{
			Name:             "server.version",
			Value:            "1.38.0",
			TagExtractedName: "server.version",
		}},
		Histograms: []FakeHistogram{{
			Name:             "http.ingress_http.downstream_rq_time",
			SampleCount:      7,
			SampleSum:        123.5,
			Buckets:          []shared.HistogramBucket{{UpperBound: 1.0, CumulativeCount: 2}, {UpperBound: 5.0, CumulativeCount: 7}},
			TagExtractedName: "http.downstream_rq_time",
			Tags:             []FakeTag{{"envoy.http_conn_manager_prefix", "ingress_http"}},
		}},
	}
}

func TestFakeMetricSnapshotValues(t *testing.T) {
	s := populatedSnapshot()
	if s.CounterCount() != 1 || s.GaugeCount() != 1 || s.TextReadoutCount() != 1 || s.HistogramCount() != 1 {
		t.Fatalf("unexpected counts %d/%d/%d/%d", s.CounterCount(), s.GaugeCount(),
			s.TextReadoutCount(), s.HistogramCount())
	}

	name, counter, ok := s.GetCounter(0, nil)
	if !ok || string(name) != "cluster.foo.rq_total" || counter.Value != 10 || counter.Delta != 5 {
		t.Errorf("GetCounter = %q %+v %v", name, counter, ok)
	}

	name, gauge, ok := s.GetGauge(0, nil)
	if !ok || string(name) != "cluster.foo.membership_healthy" || gauge != 42 {
		t.Errorf("GetGauge = %q %d %v", name, gauge, ok)
	}

	name, value, ok := s.GetTextReadout(0, nil, nil)
	if !ok || string(name) != "server.version" || string(value) != "1.38.0" {
		t.Errorf("GetTextReadout = %q %q %v", name, value, ok)
	}

	name, histogram, ok := s.GetHistogram(0, nil)
	if !ok || string(name) != "http.ingress_http.downstream_rq_time" || histogram.SampleCount != 7 ||
		histogram.SampleSum != 123.5 {
		t.Errorf("GetHistogram = %q %+v %v", name, histogram, ok)
	}

	if s.HistogramBucketCount(0) != 2 {
		t.Errorf("HistogramBucketCount = %d", s.HistogramBucketCount(0))
	}
	bucket, ok := s.GetHistogramBucket(0, 1)
	if !ok || bucket.UpperBound != 5.0 || bucket.CumulativeCount != 7 {
		t.Errorf("GetHistogramBucket = %+v %v", bucket, ok)
	}
}

func TestFakeMetricSnapshotTags(t *testing.T) {
	s := populatedSnapshot()

	teName, ok := s.GetCounterTagExtractedName(0, nil)
	if !ok || string(teName) != "cluster.rq_total" {
		t.Errorf("GetCounterTagExtractedName = %q %v", teName, ok)
	}
	count, ok := s.CounterTagCount(0)
	if !ok || count != 2 {
		t.Errorf("CounterTagCount = %d %v", count, ok)
	}
	name, value, ok := s.GetCounterTag(0, 1, nil, nil)
	if !ok || string(name) != "region" || string(value) != "us-east-4" {
		t.Errorf("GetCounterTag = %q %q %v", name, value, ok)
	}

	if teName, ok := s.GetGaugeTagExtractedName(0, nil); !ok || string(teName) != "cluster.membership_healthy" {
		t.Errorf("GetGaugeTagExtractedName = %q %v", teName, ok)
	}
	if count, ok := s.GaugeTagCount(0); !ok || count != 1 {
		t.Errorf("GaugeTagCount = %d %v", count, ok)
	}
	if name, value, ok := s.GetGaugeTag(0, 0, nil, nil); !ok || string(name) != "envoy.cluster_name" ||
		string(value) != "foo" {
		t.Errorf("GetGaugeTag = %q %q %v", name, value, ok)
	}

	// A metric with no tags reports zero and its tag-extracted name still reads.
	if teName, ok := s.GetTextReadoutTagExtractedName(0, nil); !ok || string(teName) != "server.version" {
		t.Errorf("GetTextReadoutTagExtractedName = %q %v", teName, ok)
	}
	if count, ok := s.TextReadoutTagCount(0); !ok || count != 0 {
		t.Errorf("TextReadoutTagCount = %d %v", count, ok)
	}
	if _, _, ok := s.GetTextReadoutTag(0, 0, nil, nil); ok {
		t.Error("GetTextReadoutTag on a tag-less text readout returned true")
	}

	if teName, ok := s.GetHistogramTagExtractedName(0, nil); !ok || string(teName) != "http.downstream_rq_time" {
		t.Errorf("GetHistogramTagExtractedName = %q %v", teName, ok)
	}
	if count, ok := s.HistogramTagCount(0); !ok || count != 1 {
		t.Errorf("HistogramTagCount = %d %v", count, ok)
	}
	if name, value, ok := s.GetHistogramTag(0, 0, nil, nil); !ok ||
		string(name) != "envoy.http_conn_manager_prefix" || string(value) != "ingress_http" {
		t.Errorf("GetHistogramTag = %q %q %v", name, value, ok)
	}
}

func TestFakeMetricSnapshotOutOfRange(t *testing.T) {
	s := &FakeMetricSnapshot{}
	sentinel := []byte("keep")

	if name, _, ok := s.GetCounter(0, sentinel); ok || string(name) != "keep" {
		t.Errorf("GetCounter out of range = %q %v", name, ok)
	}
	if name, _, ok := s.GetGauge(0, sentinel); ok || string(name) != "keep" {
		t.Errorf("GetGauge out of range = %q %v", name, ok)
	}
	if name, value, ok := s.GetTextReadout(0, sentinel, sentinel); ok || string(name) != "keep" ||
		string(value) != "keep" {
		t.Errorf("GetTextReadout out of range = %q %q %v", name, value, ok)
	}
	if name, _, ok := s.GetHistogram(0, sentinel); ok || string(name) != "keep" {
		t.Errorf("GetHistogram out of range = %q %v", name, ok)
	}
	if s.HistogramBucketCount(0) != 0 {
		t.Errorf("HistogramBucketCount out of range = %d", s.HistogramBucketCount(0))
	}
	if _, ok := s.GetHistogramBucket(0, 0); ok {
		t.Error("GetHistogramBucket out of range returned true")
	}

	if name, ok := s.GetCounterTagExtractedName(0, sentinel); ok || string(name) != "keep" {
		t.Errorf("GetCounterTagExtractedName out of range = %q %v", name, ok)
	}
	if _, ok := s.CounterTagCount(0); ok {
		t.Error("CounterTagCount out of range returned true")
	}
	if name, value, ok := s.GetCounterTag(0, 0, sentinel, sentinel); ok || string(name) != "keep" ||
		string(value) != "keep" {
		t.Errorf("GetCounterTag out of range = %q %q %v", name, value, ok)
	}

	// A valid metric but an out-of-range tag index also leaves the buffers unchanged.
	populated := populatedSnapshot()
	if name, value, ok := populated.GetHistogramTag(0, 99, sentinel, sentinel); ok ||
		string(name) != "keep" || string(value) != "keep" {
		t.Errorf("GetHistogramTag out of range tag = %q %q %v", name, value, ok)
	}
	// A valid histogram but an out-of-range bucket index returns false.
	if _, ok := populated.GetHistogramBucket(0, 99); ok {
		t.Error("GetHistogramBucket out-of-range bucket returned true")
	}
}

func TestFakeMetricSnapshotBufferReuse(t *testing.T) {
	s := populatedSnapshot()

	buf, _, ok := s.GetCounter(0, make([]byte, 0, 64))
	if !ok || string(buf) != "cluster.foo.rq_total" {
		t.Fatalf("GetCounter = %q %v", buf, ok)
	}
	reusedCap := cap(buf)

	// Reslicing to buf[:0] reuses the backing array for a shorter name without reallocating.
	buf, _, ok = s.GetGauge(0, buf[:0])
	if !ok || string(buf) != "cluster.foo.membership_healthy" || cap(buf) != reusedCap {
		t.Errorf("GetGauge reuse = %q cap=%d ok=%v", buf, cap(buf), ok)
	}
}
