package main

import (
	"fmt"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterHttpFilterConfigFactories(map[string]shared.HttpFilterConfigFactory{
		"stats_callbacks":            &statsCallbacksConfigFactory{},
		"header_callbacks":           &headerCallbacksConfigFactory{},
		"send_response":              &sendResponseConfigFactory{},
		"dynamic_metadata_callbacks": &dynamicMetadataCallbacksConfigFactory{},
		"filter_state_callbacks":     &filterStateCallbacksConfigFactory{},
		"body_callbacks":             &bodyCallbacksConfigFactory{},
		"config_init_failure":        &configInitFailureConfigFactory{},
	})
}

func main() {}

// --- config_init_failure ---
type configInitFailureConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (f *configInitFailureConfigFactory) Create(handle shared.HttpFilterConfigHandle,
	config []byte) (shared.HttpFilterFactory, error) {
	return nil, fmt.Errorf("config init failure")
}

// --- stats_callbacks ---
type statsCallbacksConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

type statsCallbacksFactory struct {
	streamsTotal      shared.MetricID
	concurrentStreams shared.MetricID
	magicNumber       shared.MetricID
	ones              shared.MetricID
	testCounterVec    shared.MetricID
	testGaugeVec      shared.MetricID
	testHistogramVec  shared.MetricID
}

func (f *statsCallbacksConfigFactory) Create(handle shared.HttpFilterConfigHandle, config []byte) (shared.HttpFilterFactory, error) {
	pluginFactory := &statsCallbacksFactory{}
	pluginFactory.streamsTotal, _ = handle.DefineCounter("streams_total")
	pluginFactory.concurrentStreams, _ = handle.DefineGauge("concurrent_streams")
	pluginFactory.ones, _ = handle.DefineHistogram("ones")
	pluginFactory.magicNumber, _ = handle.DefineGauge("magic_number")
	pluginFactory.testCounterVec, _ = handle.DefineCounter("test_counter_vec", "test_label")
	pluginFactory.testGaugeVec, _ = handle.DefineGauge("test_gauge_vec", "test_label")
	pluginFactory.testHistogramVec, _ = handle.DefineHistogram("test_histogram_vec", "test_label")
	return pluginFactory, nil
}

type statsCallbacksFilter struct {
	factory *statsCallbacksFactory
	handle  shared.HttpFilterHandle
	shared.EmptyHttpFilter
}

func (f *statsCallbacksFactory) Create(handle shared.HttpFilterHandle) shared.HttpFilter {
	handle.IncrementCounterValue(f.streamsTotal, 1)
	handle.IncrementGaugeValue(f.concurrentStreams, 1)
	handle.SetGaugeValue(f.magicNumber, 42)
	handle.IncrementCounterValue(f.testCounterVec, 1, "increment")
	handle.IncrementGaugeValue(f.testGaugeVec, 1, "increase")
	handle.IncrementGaugeValue(f.testGaugeVec, 10, "decrease")
	handle.DecrementGaugeValue(f.testGaugeVec, 8, "decrease")
	handle.SetGaugeValue(f.testGaugeVec, 9001, "set")
	handle.RecordHistogramValue(f.testHistogramVec, 1, "record")

	return &statsCallbacksFilter{factory: f, handle: handle}
}

func (p *statsCallbacksFilter) OnRequestHeaders(headers shared.HeaderMap, endOfStream bool) shared.HeadersStatus {
	p.handle.RecordHistogramValue(p.factory.ones, 1)
	header := headers.GetOne("header")
	p.handle.IncrementCounterValue(p.factory.testCounterVec, 1, header)
	p.handle.IncrementGaugeValue(p.factory.testGaugeVec, 1, header)
	p.handle.RecordHistogramValue(p.factory.testHistogramVec, 1, header)
	return shared.HeadersStatusContinue
}

func (p *statsCallbacksFilter) OnStreamComplete() {
	p.handle.DecrementGaugeValue(p.factory.concurrentStreams, 1)
	localVar := "local_var"
	p.handle.IncrementCounterValue(p.factory.testCounterVec, 1, localVar)
	p.handle.IncrementGaugeValue(p.factory.testGaugeVec, 1, localVar)
	p.handle.RecordHistogramValue(p.factory.testHistogramVec, 1, localVar)
}

// --- header_callbacks ---
type headerCallbacksConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}
type headerCallbacksFactory struct{}

func (f *headerCallbacksConfigFactory) Create(handle shared.HttpFilterConfigHandle,
	config []byte) (shared.HttpFilterFactory, error) {
	return &headerCallbacksFactory{}, nil
}

type headerCallbacksFilter struct {
	handle shared.HttpFilterHandle
	shared.EmptyHttpFilter
}

func (f *headerCallbacksFactory) Create(handle shared.HttpFilterHandle) shared.HttpFilter {
	return &headerCallbacksFilter{handle: handle}
}

func (p *headerCallbacksFilter) OnRequestHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	p.handle.ClearRouteCache()

	testHeaders(headers)

	// Attribute tests
	if val, ok := p.handle.GetAttributeNumber(shared.AttributeIDSourcePort); !ok || val != 1234 {
		panic(fmt.Sprintf("source port mismatch: %v", val))
	}
	if _, ok := p.handle.GetAttributeString(shared.AttributeIDSourceAddress); !ok {
		panic("source address not found")
	}

	return shared.HeadersStatusContinue
}

func (p *headerCallbacksFilter) OnRequestTrailers(trailers shared.HeaderMap) shared.TrailersStatus {
	testHeaders(trailers)
	return shared.TrailersStatusContinue
}

func (p *headerCallbacksFilter) OnResponseHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	testHeaders(headers)
	return shared.HeadersStatusContinue
}

func (p *headerCallbacksFilter) OnResponseTrailers(trailers shared.HeaderMap) shared.TrailersStatus {
	testHeaders(trailers)
	return shared.TrailersStatusContinue
}

func testHeaders(headers shared.HeaderMap) {
	// Test single getter API
	if val := headers.GetOne("single"); val != "value" {
		panic(fmt.Sprintf("header single mismatch: %s", val))
	}
	if val := headers.GetOne("non-exist"); val != "" {
		panic(fmt.Sprintf("header non-exist found: %s", val))
	}

	// Test multi getter API
	vals := headers.Get("multi")
	if len(vals) != 2 || vals[0] != "value1" || vals[1] != "value2" {
		panic(fmt.Sprintf("header multi mismatch: %v", vals))
	}
	if len(headers.Get("non-exist")) != 0 {
		panic("header non-exist found/not empty")
	}

	// Test setter API
	headers.Set("new", "value")
	if headers.GetOne("new") != "value" {
		panic("header new mismatch")
	}
	headers.Remove("to-be-deleted")

	// Test adder API
	headers.Add("multi", "value3")
	newVals := headers.Get("multi")
	if len(newVals) != 3 || newVals[0] != "value1" || newVals[1] != "value2" || newVals[2] != "value3" {
		panic(fmt.Sprintf("header multi values mismatch: %v", newVals))
	}

	// Test all getter API
	all := headers.GetAll()
	if len(all) != 5 {
		panic(fmt.Sprintf("header all length mismatch: %d", len(all)))
	}
	if all[0][0] != "single" || all[0][1] != "value" ||
		all[1][0] != "multi" || all[1][1] != "value1" ||
		all[2][0] != "multi" || all[2][1] != "value2" ||
		all[3][0] != "new" || all[3][1] != "value" ||
		all[4][0] != "multi" || all[4][1] != "value3" {
		panic(fmt.Sprintf("header all mismatch: %v", all))
	}
}

// --- send_response ---
type sendResponseConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}
type sendResponseFactory struct{}

func (f *sendResponseConfigFactory) Create(handle shared.HttpFilterConfigHandle,
	config []byte) (shared.HttpFilterFactory, error) {
	return &sendResponseFactory{}, nil
}

type sendResponseFilter struct {
	handle shared.HttpFilterHandle
	shared.EmptyHttpFilter
}

func (f *sendResponseFactory) Create(handle shared.HttpFilterHandle) shared.HttpFilter {
	return &sendResponseFilter{handle: handle}
}

func (p *sendResponseFilter) OnRequestHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	p.handle.SendLocalResponse(200, [][2]string{{"header1", "value1"}, {"header2", "value2"}},
		[]byte("Hello, World!"), "")
	return shared.HeadersStatusStop
}

// --- dynamic_metadata_callbacks ---
type dynamicMetadataCallbacksConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}
type dynamicMetadataCallbacksFactory struct{}

func (f *dynamicMetadataCallbacksConfigFactory) Create(handle shared.HttpFilterConfigHandle,
	config []byte) (shared.HttpFilterFactory, error) {
	return &dynamicMetadataCallbacksFactory{}, nil
}

type dynamicMetadataCallbacksFilter struct {
	handle shared.HttpFilterHandle
	shared.EmptyHttpFilter
}

func (f *dynamicMetadataCallbacksFactory) Create(handle shared.HttpFilterHandle) shared.HttpFilter {
	return &dynamicMetadataCallbacksFilter{handle: handle}
}
func (p *dynamicMetadataCallbacksFilter) OnRequestHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	// No namespace.
	if noNamespace, ok := p.handle.GetMetadataNumber(shared.MetadataSourceTypeDynamic,
		"no_namespace", "key"); ok {
		panic(fmt.Sprintf("expected no metadata, got %v", noNamespace))
	}

	// Set a number.
	p.handle.SetMetadata("ns_req_header", "key", float64(123.0))
	if val, ok := p.handle.GetMetadataNumber(shared.MetadataSourceTypeDynamic,
		"ns_req_header", "key"); !ok || val != 123.0 {
		panic(fmt.Sprintf("metadata key mismatch: %v", val))
	}

	// Try getting a number as string.
	if _, ok := p.handle.GetMetadataString(shared.MetadataSourceTypeDynamic,
		"ns_req_header", "key"); ok {
		panic("metadata type mismatch not detected")
	}

	// Try getting metadata from router, cluster, and host.
	if val, ok := p.handle.GetMetadataString(shared.MetadataSourceTypeRoute,
		"metadata", "route_key"); !ok || val != "route" {
		panic(fmt.Sprintf("route metadata mismatch: %v", val))
	}
	if val, ok := p.handle.GetMetadataString(shared.MetadataSourceTypeCluster,
		"metadata", "cluster_key"); !ok || val != "cluster" {
		panic(fmt.Sprintf("cluster metadata mismatch: %v", val))
	}
	if val, ok := p.handle.GetMetadataString(shared.MetadataSourceTypeHost,
		"metadata", "host_key"); !ok || val != "host" {
		panic(fmt.Sprintf("host metadata mismatch: %v", val))
	}

	return shared.HeadersStatusContinue
}

func (p *dynamicMetadataCallbacksFilter) OnRequestBody(body shared.BodyBuffer, endOfStream bool) shared.BodyStatus {
	// No namespace.
	if _, ok := p.handle.GetMetadataString(shared.MetadataSourceTypeDynamic, "ns_req_body", "key"); ok {
		panic("expected no metadata")
	}
	// Set a string.
	p.handle.SetMetadata("ns_req_body", "key", "value")
	if val, ok := p.handle.GetMetadataString(shared.MetadataSourceTypeDynamic, "ns_req_body", "key"); !ok || val != "value" {
		panic("metadata key mismatch")
	}
	// Try getting a string as number.
	if _, ok := p.handle.GetMetadataNumber(shared.MetadataSourceTypeDynamic, "ns_req_body", "key"); ok {
		panic("metadata type mismatch")
	}
	return shared.BodyStatusContinue
}

func (p *dynamicMetadataCallbacksFilter) OnResponseHeaders(headers shared.HeaderMap, endOfStream bool) shared.HeadersStatus {
	// No namespace.
	if _, ok := p.handle.GetMetadataString(shared.MetadataSourceTypeDynamic, "ns_res_header", "key"); ok {
		panic("expected no metadata")
	}
	// Set a number.
	p.handle.SetMetadata("ns_res_header", "key", 123.0)
	if val, ok := p.handle.GetMetadataNumber(shared.MetadataSourceTypeDynamic, "ns_res_header", "key"); !ok || val != 123.0 {
		panic(fmt.Sprintf("metadata key mismatch: %v", val))
	}
	// Try getting a number as string.
	if _, ok := p.handle.GetMetadataString(shared.MetadataSourceTypeDynamic, "ns_res_header", "key"); ok {
		panic("metadata type mismatch")
	}
	return shared.HeadersStatusContinue
}

func (p *dynamicMetadataCallbacksFilter) OnResponseBody(body shared.BodyBuffer, endOfStream bool) shared.BodyStatus {
	// No namespace.
	if _, ok := p.handle.GetMetadataString(shared.MetadataSourceTypeDynamic, "ns_res_body", "key"); ok {
		panic("expected no metadata")
	}
	// Set a string.
	p.handle.SetMetadata("ns_res_body", "key", "value")
	if val, ok := p.handle.GetMetadataString(shared.MetadataSourceTypeDynamic, "ns_res_body", "key"); !ok || val != "value" {
		panic("metadata key mismatch")
	}
	// Try getting a string as number.
	if _, ok := p.handle.GetMetadataNumber(shared.MetadataSourceTypeDynamic, "ns_res_body", "key"); ok {
		panic("metadata type mismatch")
	}
	return shared.BodyStatusContinue
}

// --- filter_state_callbacks ---
type filterStateCallbacksConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}
type filterStateCallbacksFactory struct{}

func (f *filterStateCallbacksConfigFactory) Create(handle shared.HttpFilterConfigHandle,
	config []byte) (shared.HttpFilterFactory, error) {
	return &filterStateCallbacksFactory{}, nil
}

type filterStateCallbacksFilter struct {
	handle shared.HttpFilterHandle
	shared.EmptyHttpFilter
}

func (f *filterStateCallbacksFactory) Create(handle shared.HttpFilterHandle) shared.HttpFilter {
	return &filterStateCallbacksFilter{handle: handle}
}

func (p *filterStateCallbacksFilter) OnRequestHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	p.testFilterState("req_header_key", "req_header_value")
	return shared.HeadersStatusContinue
}

func (p *filterStateCallbacksFilter) OnRequestBody(body shared.BodyBuffer, endOfStream bool) shared.BodyStatus {
	p.testFilterState("req_body_key", "req_body_value")
	return shared.BodyStatusContinue
}

func (p *filterStateCallbacksFilter) OnRequestTrailers(trailers shared.HeaderMap) shared.TrailersStatus {
	p.testFilterState("req_trailer_key", "req_trailer_value")
	return shared.TrailersStatusContinue
}

func (p *filterStateCallbacksFilter) OnResponseHeaders(headers shared.HeaderMap, endOfStream bool) shared.HeadersStatus {
	p.testFilterState("res_header_key", "res_header_value")
	return shared.HeadersStatusContinue
}

func (p *filterStateCallbacksFilter) OnResponseBody(body shared.BodyBuffer, endOfStream bool) shared.BodyStatus {
	p.testFilterState("res_body_key", "res_body_value")
	return shared.BodyStatusContinue
}

func (p *filterStateCallbacksFilter) OnResponseTrailers(trailers shared.HeaderMap) shared.TrailersStatus {
	p.testFilterState("res_trailer_key", "res_trailer_value")
	return shared.TrailersStatusContinue
}

func (p *filterStateCallbacksFilter) OnStreamComplete() {
	p.testFilterState("stream_complete_key", "stream_complete_value")
}

func (p *filterStateCallbacksFilter) testFilterState(key, value string) {
	p.handle.SetFilterState(key, []byte(value))
	if val, ok := p.handle.GetFilterState(key); !ok || string(val) != value {
		panic(fmt.Sprintf("filter state %s mismatch", key))
	}
	if _, ok := p.handle.GetFilterState("key"); ok {
		panic("filter state key found")
	}
}

// --- body_callbacks ---
type bodyCallbacksConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}
type bodyCallbacksFactory struct{}

func (f *bodyCallbacksConfigFactory) Create(handle shared.HttpFilterConfigHandle,
	config []byte) (shared.HttpFilterFactory, error) {
	return &bodyCallbacksFactory{}, nil
}

type bodyCallbacksFilter struct {
	shared.EmptyHttpFilter
	handle shared.HttpFilterHandle
}

func (f *bodyCallbacksFactory) Create(handle shared.HttpFilterHandle) shared.HttpFilter {
	return &bodyCallbacksFilter{
		handle: handle,
	}
}

func (p *bodyCallbacksFilter) OnRequestBody(body shared.BodyBuffer, endOfStream bool) shared.BodyStatus {
	receivedBodyChunks := body.GetChunks()
	p.handle.Log(shared.LogLevelInfo, "Received body chunks: %v\n", receivedBodyChunks)
	receivedBodySize := body.GetSize()
	body.Drain(receivedBodySize)
	body.Append([]byte("foo"))
	if endOfStream {
		body.Append([]byte("end"))
	}

	bufferedBody := p.handle.BufferedRequestBody()
	bufferedBodyChunks := bufferedBody.GetChunks()
	p.handle.Log(shared.LogLevelInfo, "Buffered body chunks: %v\n", bufferedBodyChunks)
	bufferedBodySize := bufferedBody.GetSize()
	bufferedBody.Drain(bufferedBodySize)
	bufferedBody.Append([]byte("foo"))
	if endOfStream {
		bufferedBody.Append([]byte("end"))
	}

	return shared.BodyStatusContinue
}

func (p *bodyCallbacksFilter) OnResponseBody(body shared.BodyBuffer, endOfStream bool) shared.BodyStatus {
	receivedBodyChunks := body.GetChunks()
	p.handle.Log(shared.LogLevelInfo, "Received body chunks: %v\n", receivedBodyChunks)
	receivedBodySize := body.GetSize()
	body.Drain(receivedBodySize)
	body.Append([]byte("bar"))
	if endOfStream {
		body.Append([]byte("end"))
	}

	bufferedBody := p.handle.BufferedResponseBody()
	bufferedBodyChunks := bufferedBody.GetChunks()
	p.handle.Log(shared.LogLevelInfo, "Buffered body chunks: %v\n", bufferedBodyChunks)
	bufferedBodySize := bufferedBody.GetSize()
	bufferedBody.Drain(bufferedBodySize)
	bufferedBody.Append([]byte("bar"))
	if endOfStream {
		bufferedBody.Append([]byte("end"))
	}

	return shared.BodyStatusContinue
}
