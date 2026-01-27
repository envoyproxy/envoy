package main

import (
	"runtime"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"

	"fmt"
	"strconv"
	"strings"
	"sync/atomic"
	"time"
)

func init() {
	sdk.RegisterHttpFilterConfigFactories(map[string]shared.HttpFilterConfigFactory{
		"passthrough":               &PassthroughConfigFactory{},
		"header_callbacks":          &HeaderCallbacksConfigFactory{},
		"per_route_config":          &PerRouteConfigFactory{},
		"body_callbacks":            &BodyCallbacksConfigFactory{},
		"http_callouts":             &HttpCalloutsConfigFactory{},
		"send_response":             &SendResponseConfigFactory{},
		"http_filter_scheduler":     &HttpFilterSchedulerConfigFactory{},
		"fake_external_cache":       &FakeExternalCacheConfigFactory{},
		"stats_callbacks":           &StatsCallbacksConfigFactory{},
		"streaming_terminal_filter": &StreamingTerminalConfigFactory{},
		"http_stream_basic":         &HttpStreamBasicConfigFactory{},
		"http_stream_bidirectional": &HttpStreamBidirectionalConfigFactory{},
		"upstream_reset":            &UpstreamResetConfigFactory{},
		"http_config_scheduler":     &ConfigSchedulerConfigFactory{},
	})
}

func main() {} //nolint:all

// -----------------------------------------------------------------------------
// ConfigScheduler
// -----------------------------------------------------------------------------

type ConfigSchedulerConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (f *ConfigSchedulerConfigFactory) Create(handle shared.HttpFilterConfigHandle,
	config []byte) (shared.HttpFilterFactory, error) {
	// Simulate async config update.
	// In the Go SDK, we don't have an explicit scheduler for the config object,
	// but we can spawn a goroutine that updates a shared state.
	// This mimics the Rust test's "http_config_scheduler" logic.
	sharedStatus := new(atomic.Bool)
	sharedStatus.Store(false)

	// TODO(wbpcode): to support the actual config scheduler in golang SDK.
	go func() {
		time.Sleep(100 * time.Millisecond)
		sharedStatus.Store(true)
	}()

	return &ConfigSchedulerFilterFactory{sharedStatus: sharedStatus}, nil
}

type ConfigSchedulerFilterFactory struct {
	sharedStatus *atomic.Bool
}

func (f *ConfigSchedulerFilterFactory) Create(handle shared.HttpFilterHandle) shared.HttpFilter {
	return &ConfigSchedulerFilter{sharedStatus: f.sharedStatus}
}

type ConfigSchedulerFilter struct {
	shared.EmptyHttpFilter
	sharedStatus *atomic.Bool
}

func (p *ConfigSchedulerFilter) OnRequestHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	if p.sharedStatus.Load() {
		headers.Set("x-test-status", "true")
	} else {
		headers.Set("x-test-status", "false")
	}
	return shared.HeadersStatusContinue
}

// -----------------------------------------------------------------------------
// Passthrough
// -----------------------------------------------------------------------------

type PassthroughConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (f *PassthroughConfigFactory) Create(handle shared.HttpFilterConfigHandle,
	config []byte) (shared.HttpFilterFactory, error) {
	return &PassthroughFilterFactory{}, nil
}

type PassthroughFilterFactory struct{}

func (f *PassthroughFilterFactory) Create(handle shared.HttpFilterHandle) shared.HttpFilter {
	// Log test
	handle.Log(shared.LogLevelTrace, "new_http_filter called")
	handle.Log(shared.LogLevelDebug, "new_http_filter called")
	handle.Log(shared.LogLevelInfo, "new_http_filter called")
	handle.Log(shared.LogLevelWarn, "new_http_filter called")
	handle.Log(shared.LogLevelError, "new_http_filter called")
	handle.Log(shared.LogLevelCritical, "new_http_filter called")
	return &PassthroughFilter{handle: handle}
}

type PassthroughFilter struct {
	shared.EmptyHttpFilter
	handle shared.HttpFilterHandle
}

func (p *PassthroughFilter) OnRequestHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	p.handle.Log(shared.LogLevelTrace, "on_request_headers called")
	p.handle.Log(shared.LogLevelDebug, "on_request_headers called")
	p.handle.Log(shared.LogLevelInfo, "on_request_headers called")
	p.handle.Log(shared.LogLevelWarn, "on_request_headers called")
	p.handle.Log(shared.LogLevelError, "on_request_headers called")
	p.handle.Log(shared.LogLevelCritical, "on_request_headers called")
	return shared.HeadersStatusContinue
}

// -----------------------------------------------------------------------------
// HeaderCallbacks
// -----------------------------------------------------------------------------

type HeaderCallbacksConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (f *HeaderCallbacksConfigFactory) Create(handle shared.HttpFilterConfigHandle,
	config []byte) (shared.HttpFilterFactory, error) {
	headersToAdd := make(map[string]string)
	if len(config) > 0 {
		str := string(config)
		parts := strings.Split(str, ",")
		for _, part := range parts {
			kv := strings.Split(part, ":")
			if len(kv) == 2 {
				headersToAdd[kv[0]] = kv[1]
			}
		}
	}
	return &HeaderCallbacksFilterFactory{headersToAdd: headersToAdd}, nil
}

type HeaderCallbacksFilterFactory struct {
	headersToAdd map[string]string
}

func (f *HeaderCallbacksFilterFactory) Create(handle shared.HttpFilterHandle) shared.HttpFilter {
	return &HeaderCallbacksFilter{
		headersToAdd: f.headersToAdd,
		// Init checks for cleanup
		reqHeadersCalled:  false,
		reqTrailersCalled: false,
		resHeadersCalled:  false,
		resTrailersCalled: false,
	}
}

type HeaderCallbacksFilter struct {
	shared.EmptyHttpFilter
	headersToAdd      map[string]string
	reqHeadersCalled  bool
	reqTrailersCalled bool
	resHeadersCalled  bool
	resTrailersCalled bool
}

func assert(cond bool, msg string) {
	if !cond {
		panic("assertion failed: " + msg)
	}
}

func assertEq(a, b interface{}, msg string) {
	if a != b {
		panic(fmt.Sprintf("%s: %v != %v", msg, a, b))
	}
}

func (p *HeaderCallbacksFilter) OnRequestHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	p.reqHeadersCalled = true
	assertEq(headers.GetOne(":path"), "/test/long/url", ":path header")
	assertEq(headers.GetOne(":method"), "POST", ":method header")
	assertEq(headers.GetOne("foo"), "bar", "foo header")

	for k, v := range p.headersToAdd {
		headers.Set(k, v)
	}

	// Test setter/getter
	headers.Set("new", "value1")
	assertEq(headers.GetOne("new"), "value1", "new header set")

	vals := headers.Get("new")
	assertEq(len(vals), 1, "new header count")
	assertEq(vals[0], "value1", "new header val")

	// Test add
	headers.Add("new", "value2")
	vals = headers.Get("new")
	assertEq(len(vals), 2, "new header count after add")
	assertEq(vals[1], "value2", "new header val 2")

	// Test remove
	headers.Remove("new")
	assertEq(headers.GetOne("new"), "", "new header removed")
	return shared.HeadersStatusContinue
}

func (p *HeaderCallbacksFilter) OnRequestTrailers(trailers shared.HeaderMap) shared.TrailersStatus {
	p.reqTrailersCalled = true
	assertEq(trailers.GetOne("foo"), "bar", "foo trailer")
	for k, v := range p.headersToAdd {
		trailers.Set(k, v)
	}
	// Test setter/getter
	trailers.Set("new", "value1")
	assertEq(trailers.GetOne("new"), "value1", "new trailer set")

	// Test add
	trailers.Add("new", "value2")
	vals := trailers.Get("new")
	assertEq(len(vals), 2, "new trailer count")

	// Test remove
	trailers.Remove("new")
	assertEq(trailers.GetOne("new"), "", "new trailer removed")
	return shared.TrailersStatusContinue
}

func (p *HeaderCallbacksFilter) OnResponseHeaders(headers shared.HeaderMap, endOfStream bool) shared.HeadersStatus {
	p.resHeadersCalled = true
	assertEq(headers.GetOne("foo"), "bar", "foo response header")
	for k, v := range p.headersToAdd {
		headers.Set(k, v)
	}

	headers.Set("new", "value1")
	assertEq(headers.GetOne("new"), "value1", "new resp header")
	headers.Add("new", "value2")
	assertEq(len(headers.Get("new")), 2, "new resp header count")
	headers.Remove("new")
	assertEq(headers.GetOne("new"), "", "new resp header removed")
	return shared.HeadersStatusContinue
}

func (p *HeaderCallbacksFilter) OnResponseTrailers(trailers shared.HeaderMap) shared.TrailersStatus {
	p.resTrailersCalled = true
	assertEq(trailers.GetOne("foo"), "bar", "foo response trailer")
	for k, v := range p.headersToAdd {
		trailers.Set(k, v)
	}

	trailers.Set("new", "value1")
	assertEq(trailers.GetOne("new"), "value1", "new resp trailer")
	trailers.Add("new", "value2")
	assertEq(len(trailers.Get("new")), 2, "new resp trailer count")
	trailers.Remove("new")
	assertEq(trailers.GetOne("new"), "", "new resp trailer removed")
	return shared.TrailersStatusContinue
}

func (p *HeaderCallbacksFilter) OnStreamComplete() {
	assert(p.reqHeadersCalled, "reqHeadersCalled")
	assert(p.reqTrailersCalled, "reqTrailersCalled")
	assert(p.resHeadersCalled, "resHeadersCalled")
	assert(p.resTrailersCalled, "resTrailersCalled")
}

// -----------------------------------------------------------------------------
// PerRoute
// -----------------------------------------------------------------------------

type PerRouteConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (f *PerRouteConfigFactory) Create(handle shared.HttpFilterConfigHandle,
	config []byte) (shared.HttpFilterFactory, error) {
	return &PerRouteFilterFactory{value: string(config)}, nil
}

func (f *PerRouteConfigFactory) CreatePerRoute(unparsedConfig []byte) (any, error) {
	return string(unparsedConfig), nil
}

type PerRouteFilterFactory struct {
	value string
}

func (f *PerRouteFilterFactory) Create(handle shared.HttpFilterHandle) shared.HttpFilter {
	return &PerRouteFilter{handle: handle, value: f.value}
}

type PerRouteFilter struct {
	shared.EmptyHttpFilter
	handle         shared.HttpFilterHandle
	value          string
	perRouteConfig string
}

func (p *PerRouteFilter) OnRequestHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	headers.Set("x-config", p.value)

	cfg := p.handle.GetMostSpecificConfig()
	if cfg != nil {
		if val, ok := cfg.(string); ok {
			p.perRouteConfig = val
			headers.Set("x-per-route-config", val)
		}
	}
	return shared.HeadersStatusContinue
}

func (p *PerRouteFilter) OnResponseHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	if p.perRouteConfig != "" {
		headers.Set("x-per-route-config-response", p.perRouteConfig)
	}
	return shared.HeadersStatusContinue
}

// -----------------------------------------------------------------------------
// BodyCallbacks
// -----------------------------------------------------------------------------

type BodyCallbacksConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (f *BodyCallbacksConfigFactory) Create(handle shared.HttpFilterConfigHandle,
	config []byte) (shared.HttpFilterFactory, error) {
	immediate := string(config) == "immediate_end_of_stream"
	return &BodyCallbacksFilterFactory{immediate: immediate}, nil
}

type BodyCallbacksFilterFactory struct {
	immediate bool
}

func (f *BodyCallbacksFilterFactory) Create(handle shared.HttpFilterHandle) shared.HttpFilter {
	return &BodyCallbacksFilter{handle: handle, immediate: f.immediate}
}

type BodyCallbacksFilter struct {
	shared.EmptyHttpFilter
	handle           shared.HttpFilterHandle
	immediate        bool
	seenRequestBody  bool
	seenResponseBody bool
}

func (p *BodyCallbacksFilter) OnRequestHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	return shared.HeadersStatusStop
}

func (p *BodyCallbacksFilter) OnRequestBody(body shared.BodyBuffer,
	endOfStream bool) shared.BodyStatus {
	if !endOfStream {
		assert(!p.immediate, "immediate_end_of_stream is true but got !endOfStream")
		return shared.BodyStatusStopAndBuffer
	}
	p.seenRequestBody = true

	var body_content string

	for _, c := range p.handle.BufferedRequestBody().GetChunks() {
		body_content += string(c)
	}
	for _, c := range body.GetChunks() {
		body_content += string(c)
	}

	assertEq(body_content, "request_body", "request body content")

	// Drain everything
	p.handle.BufferedRequestBody().Drain(p.handle.BufferedRequestBody().GetSize())
	body.Drain(body.GetSize())

	// Append new
	body.Append([]byte("new_request_body"))
	p.handle.RequestHeaders().Set("content-length", "16")

	return shared.BodyStatusContinue
}

func (p *BodyCallbacksFilter) OnResponseHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	return shared.HeadersStatusStop
}

func (p *BodyCallbacksFilter) OnResponseBody(body shared.BodyBuffer,
	endOfStream bool) shared.BodyStatus {
	if !endOfStream {
		return shared.BodyStatusStopAndBuffer
	}
	p.seenResponseBody = true

	var body_content string

	for _, c := range p.handle.BufferedResponseBody().GetChunks() {
		body_content += string(c)
	}
	for _, c := range body.GetChunks() {
		body_content += string(c)
	}

	assertEq(body_content, "response_body", "response body content")

	p.handle.BufferedResponseBody().Drain(p.handle.BufferedResponseBody().GetSize())
	body.Drain(body.GetSize())

	body.Append([]byte("new_response_body"))
	p.handle.ResponseHeaders().Set("content-length", "17")

	return shared.BodyStatusContinue
}

func (p *BodyCallbacksFilter) OnStreamComplete() {
	assert(p.seenRequestBody, "seenRequestBody")
	assert(p.seenResponseBody, "seenResponseBody")
}

// -----------------------------------------------------------------------------
// SendResponse
// -----------------------------------------------------------------------------

type SendResponseConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (f *SendResponseConfigFactory) Create(handle shared.HttpFilterConfigHandle,
	config []byte) (shared.HttpFilterFactory, error) {
	return &SendResponseFilterFactory{mode: string(config)}, nil
}

type SendResponseFilterFactory struct {
	mode string
}

func (f *SendResponseFilterFactory) Create(handle shared.HttpFilterHandle) shared.HttpFilter {
	return &SendResponseFilter{handle: handle, mode: f.mode}
}

type SendResponseFilter struct {
	shared.EmptyHttpFilter
	handle shared.HttpFilterHandle
	mode   string
}

func (p *SendResponseFilter) OnRequestHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	if p.mode == "on_request_headers" {
		p.handle.SendLocalResponse(200,
			[][2]string{{"some_header", "some_value"}},
			[]byte("local_response_body_from_on_request_headers"), "test_details")
		return shared.HeadersStatusStop
	}
	return shared.HeadersStatusContinue
}

func (p *SendResponseFilter) OnRequestBody(body shared.BodyBuffer,
	endOfStream bool) shared.BodyStatus {
	if p.mode == "on_request_body" {
		p.handle.SendLocalResponse(200,
			[][2]string{{"some_header", "some_value"}},
			[]byte("local_response_body_from_on_request_body"), "")
		return shared.BodyStatusStopAndBuffer
	}
	return shared.BodyStatusContinue
}

func (p *SendResponseFilter) OnResponseHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	if p.mode == "on_response_headers" {
		p.handle.SendLocalResponse(500,
			[][2]string{{"some_header", "some_value"}},
			[]byte("local_response_body_from_on_response_headers"), "")
		return shared.HeadersStatusStop
	}
	return shared.HeadersStatusContinue
}

// -----------------------------------------------------------------------------
// HttpCallouts
// -----------------------------------------------------------------------------

type HttpCalloutsConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (f *HttpCalloutsConfigFactory) Create(handle shared.HttpFilterConfigHandle,
	config []byte) (shared.HttpFilterFactory, error) {
	return &HttpCalloutsFilterFactory{clusterName: string(config)}, nil
}

type HttpCalloutsFilterFactory struct {
	clusterName string
}

func (f *HttpCalloutsFilterFactory) Create(handle shared.HttpFilterHandle) shared.HttpFilter {
	return &HttpCalloutsFilter{handle: handle, clusterName: f.clusterName}
}

type HttpCalloutsFilter struct {
	shared.EmptyHttpFilter
	handle        shared.HttpFilterHandle
	clusterName   string
	calloutHandle uint64
}

func (p *HttpCalloutsFilter) OnRequestHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	res, id := p.handle.HttpCallout(
		p.clusterName,
		[][2]string{{":path", "/"}, {":method", "GET"}, {"host", "example.com"}},
		[]byte("http_callout_body"),
		1000,
		p,
	)
	if res != shared.HttpCalloutInitSuccess {
		p.handle.SendLocalResponse(500, [][2]string{{"foo", "bar"}}, nil, "")
	}
	p.calloutHandle = id
	return shared.HeadersStatusStop
}

func (p *HttpCalloutsFilter) OnHttpCalloutDone(calloutID uint64, result shared.HttpCalloutResult,
	headers [][2]string, body [][]byte) {
	if p.clusterName == "resetting_cluster" {
		assert(result == shared.HttpCalloutReset, "expected reset")
		return
	}
	assertEq(result, shared.HttpCalloutSuccess, "callout success")
	assertEq(calloutID, p.calloutHandle, "callout handle")

	found := false
	for _, h := range headers {
		if h[0] == "some_header" && h[1] == "some_value" {
			found = true
			break
		}
	}
	assert(found, "some_header found")

	fullBody := ""
	for _, b := range body {
		fullBody += string(b)
	}
	assertEq(fullBody, "response_body_from_callout", "resp body")

	p.handle.SendLocalResponse(200, [][2]string{{"some_header", "some_value"}},
		[]byte("local_response_body"), "callout_success")
}

// -----------------------------------------------------------------------------
// HttpFilterScheduler
// -----------------------------------------------------------------------------

type HttpFilterSchedulerConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (f *HttpFilterSchedulerConfigFactory) Create(handle shared.HttpFilterConfigHandle,
	c []byte) (shared.HttpFilterFactory, error) {
	return &HttpFilterSchedulerFilterFactory{}, nil
}

type HttpFilterSchedulerFilterFactory struct{}

func (f *HttpFilterSchedulerFilterFactory) Create(h shared.HttpFilterHandle) shared.HttpFilter {
	return &HttpFilterSchedulerFilter{handle: h}
}

type HttpFilterSchedulerFilter struct {
	shared.EmptyHttpFilter
	handle   shared.HttpFilterHandle
	eventIDs []u64
}

// In Go, since we pass a closure to Schedule, we don't have "event IDs" implicitly.
// We will mimic the behavior by appending to eventIDs inside the closure.
// But wait, the Rust test asserts the order of event IDs.
// We need to sync access to eventIDs if we are appending from scheduled callback?
// Schedule runs on main thread, so safe.

type u64 = uint64

func (p *HttpFilterSchedulerFilter) OnRequestHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	sched := p.handle.GetScheduler()
	// Spawn thread to schedule events
	go func() {
		// Event 0
		sched.Schedule(func() {
			p.eventIDs = append(p.eventIDs, 0)
		})
		// Event 1 - which continues decoding
		sched.Schedule(func() {
			p.eventIDs = append(p.eventIDs, 1)
			p.handle.ContinueRequest()
		})
	}()
	return shared.HeadersStatusStop
}

func (p *HttpFilterSchedulerFilter) OnResponseHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	sched := p.handle.GetScheduler()
	go func() {
		sched.Schedule(func() {
			p.eventIDs = append(p.eventIDs, 2)
		})
		sched.Schedule(func() {
			p.eventIDs = append(p.eventIDs, 3)
			p.handle.ContinueResponse()
		})
	}()
	return shared.HeadersStatusStop
}

func (p *HttpFilterSchedulerFilter) OnStreamComplete() {
	// Assert event order: 0, 1, 2, 3
	assertEq(len(p.eventIDs), 4, "event count")
	for i, v := range p.eventIDs {
		assertEq(int(v), i, "event id order")
	}

	// Force the GC to release the scheduler and related C resources.
	runtime.GC()
}

// -----------------------------------------------------------------------------
// FakeExternalCache
// -----------------------------------------------------------------------------

type FakeExternalCacheConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (f *FakeExternalCacheConfigFactory) Create(h shared.HttpFilterConfigHandle,
	c []byte) (shared.HttpFilterFactory, error) {
	return &FakeExternalCacheFilterFactory{}, nil
}

type FakeExternalCacheFilterFactory struct{}

func (f *FakeExternalCacheFilterFactory) Create(h shared.HttpFilterHandle) shared.HttpFilter {
	return &FakeExternalCacheFilter{handle: h}
}

type FakeExternalCacheFilter struct {
	shared.EmptyHttpFilter
	handle shared.HttpFilterHandle
}

func (p *FakeExternalCacheFilter) OnRequestHeaders(headers shared.HeaderMap, endOfStream bool) shared.HeadersStatus {
	key := headers.GetOne("cacahe-key")
	sched := p.handle.GetScheduler()

	go func() {
		if key == "existing" {
			// Simulate hit
			sched.Schedule(func() {
				p.handle.SendLocalResponse(200, [][2]string{{"cached", "yes"}}, []byte("cached_response_body"), "")
			})
		} else {
			// Simulate miss
			sched.Schedule(func() {
				p.handle.RequestHeaders().Set("on-scheduled", "req")
				p.handle.ContinueRequest()
			})
		}
	}()
	return shared.HeadersStatusStop
}

func (p *FakeExternalCacheFilter) OnResponseHeaders(headers shared.HeaderMap, endOfStream bool) shared.HeadersStatus {
	sched := p.handle.GetScheduler()
	go func() {
		sched.Schedule(func() {
			p.handle.ResponseHeaders().Set("on-scheduled", "res")
			p.handle.ContinueResponse()
		})
	}()
	return shared.HeadersStatusStop
}

func (p *FakeExternalCacheFilter) OnStreamComplete() {
	// Force the GC to release the scheduler and related C resources.
	runtime.GC()
}

// -----------------------------------------------------------------------------
// StatsCallbacks
// -----------------------------------------------------------------------------

type StatsCallbacksConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

type StatsCallbacksIDs struct {
	reqTotal      shared.MetricID
	reqPending    shared.MetricID
	reqSet        shared.MetricID
	reqVals       shared.MetricID
	epTotal       shared.MetricID
	epPending     shared.MetricID
	epSet         shared.MetricID
	epVals        shared.MetricID
	headerToCount string
	headerToSet   string
}

func (f *StatsCallbacksConfigFactory) Create(h shared.HttpFilterConfigHandle, c []byte) (shared.HttpFilterFactory, error) {
	cfg := string(c)
	parts := strings.Split(cfg, ",")
	ids := StatsCallbacksIDs{}

	var err shared.MetricsResult
	ids.reqTotal, err = h.DefineCounter("requests_total")
	assertEq(err, shared.MetricsSuccess, "c1")
	ids.reqPending, err = h.DefineGauge("requests_pending")
	assertEq(err, shared.MetricsSuccess, "g1")
	ids.reqSet, err = h.DefineGauge("requests_set_value")
	assertEq(err, shared.MetricsSuccess, "g2")
	ids.reqVals, err = h.DefineHistogram("requests_header_values")
	assertEq(err, shared.MetricsSuccess, "h1")

	ids.epTotal, err = h.DefineCounter("entrypoint_total", "entrypoint", "method")
	assertEq(err, shared.MetricsSuccess, "c2")
	ids.epPending, err = h.DefineGauge("entrypoint_pending", "entrypoint", "method")
	assertEq(err, shared.MetricsSuccess, "g3")
	ids.epSet, err = h.DefineGauge("entrypoint_set_value", "entrypoint", "method")
	assertEq(err, shared.MetricsSuccess, "g4")
	ids.epVals, err = h.DefineHistogram("entrypoint_header_values", "entrypoint", "method")
	assertEq(err, shared.MetricsSuccess, "h2")

	ids.headerToCount = parts[0]
	ids.headerToSet = parts[1]

	return &StatsCallbacksFilterFactory{ids: ids}, nil
}

type StatsCallbacksFilterFactory struct {
	ids StatsCallbacksIDs
}

func (f *StatsCallbacksFilterFactory) Create(h shared.HttpFilterHandle) shared.HttpFilter {
	return &StatsCallbacksFilter{handle: h, ids: f.ids}
}

type StatsCallbacksFilter struct {
	shared.EmptyHttpFilter
	handle shared.HttpFilterHandle
	ids    StatsCallbacksIDs
	method string
}

func (p *StatsCallbacksFilter) OnRequestHeaders(headers shared.HeaderMap, endOfStream bool) shared.HeadersStatus {
	p.handle.IncrementCounterValue(p.ids.reqTotal, 1)
	p.handle.IncrementGaugeValue(p.ids.reqPending, 1)
	p.method = headers.GetOne(":method")

	p.handle.IncrementCounterValue(p.ids.epTotal, 1, "on_request_headers", p.method)
	p.handle.IncrementGaugeValue(p.ids.epPending, 1, "on_request_headers", p.method)

	if valStr := headers.GetOne(p.ids.headerToCount); valStr != "" {
		if val, err := strconv.ParseUint(valStr, 10, 64); err == nil {
			p.handle.RecordHistogramValue(p.ids.reqVals, val)
			p.handle.RecordHistogramValue(p.ids.epVals, val, "on_request_headers", p.method)
		}
	}
	if valStr := headers.GetOne(p.ids.headerToSet); valStr != "" {
		if val, err := strconv.ParseUint(valStr, 10, 64); err == nil {
			p.handle.SetGaugeValue(p.ids.reqSet, val)
			p.handle.SetGaugeValue(p.ids.epSet, val, "on_request_headers", p.method)
		}
	}
	return shared.HeadersStatusContinue
}

func (p *StatsCallbacksFilter) OnResponseHeaders(headers shared.HeaderMap, endOfStream bool) shared.HeadersStatus {
	p.handle.IncrementCounterValue(p.ids.epTotal, 1, "on_response_headers", p.method)
	p.handle.DecrementGaugeValue(p.ids.reqPending, 1)
	p.handle.DecrementGaugeValue(p.ids.epPending, 1, "on_request_headers", p.method)
	p.handle.IncrementGaugeValue(p.ids.epPending, 1, "on_response_headers", p.method)

	if valStr := headers.GetOne(p.ids.headerToCount); valStr != "" {
		if val, err := strconv.ParseUint(valStr, 10, 64); err == nil {
			p.handle.RecordHistogramValue(p.ids.epVals, val, "on_response_headers", p.method)
		}
	}
	if valStr := headers.GetOne(p.ids.headerToSet); valStr != "" {
		if val, err := strconv.ParseUint(valStr, 10, 64); err == nil {
			p.handle.SetGaugeValue(p.ids.epSet, val, "on_response_headers", p.method)
		}
	}
	return shared.HeadersStatusContinue
}

func (p *StatsCallbacksFilter) OnStreamComplete() {
	p.handle.DecrementGaugeValue(p.ids.epPending, 1, "on_response_headers", p.method)
}

// -----------------------------------------------------------------------------
// StreamingTerminal
// -----------------------------------------------------------------------------

type StreamingTerminalConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (f *StreamingTerminalConfigFactory) Create(h shared.HttpFilterConfigHandle, c []byte) (shared.HttpFilterFactory, error) {
	return &StreamingTerminalFilterFactory{}, nil
}

type StreamingTerminalFilterFactory struct{}

func (f *StreamingTerminalFilterFactory) Create(h shared.HttpFilterHandle) shared.HttpFilter {
	p := &StreamingTerminalFilter{handle: h}
	h.SetDownstreamWatermarkCallbacks(p)
	return p
}

type StreamingTerminalFilter struct {
	shared.EmptyHttpFilter
	handle            shared.HttpFilterHandle
	requestClosed     bool
	aboveW            int
	belowW            int
	largeResponseSent int
}

func (p *StreamingTerminalFilter) OnRequestHeaders(headers shared.HeaderMap, endOfStream bool) shared.HeadersStatus {
	p.handle.GetScheduler().Schedule(p.onScheduledStartResponse)
	return shared.HeadersStatusContinue
}

func (p *StreamingTerminalFilter) OnRequestBody(body shared.BodyBuffer, endOfStream bool) shared.BodyStatus {
	if endOfStream {
		p.requestClosed = true
	}
	p.handle.GetScheduler().Schedule(p.onScheduledReadRequest)
	return shared.BodyStatusStopAndBuffer
}

func (p *StreamingTerminalFilter) onScheduledStartResponse() {
	p.handle.SendResponseHeaders([][2]string{{":status", "200"}, {"x-filter", "terminal"}, {"trailers", "x-status"}}, false)
	p.handle.SendResponseData([]byte("Who are you?"), false)
}

func (p *StreamingTerminalFilter) onScheduledReadRequest() {
	if !p.requestClosed {
		buf := p.handle.BufferedRequestBody()
		if buf != nil {
			buf.Drain(buf.GetSize())
		}
		p.sendLargeResponseChunk()
	} else {
		p.handle.SendResponseData([]byte("Thanks!"), false)
		p.handle.SendResponseTrailers([][2]string{
			{"x-status", "finished"},
			{"x-above-watermark-count", strconv.Itoa(p.aboveW)},
			{"x-below-watermark-count", strconv.Itoa(p.belowW)},
		})
	}
}

func (p *StreamingTerminalFilter) sendLargeResponseChunk() {
	if p.largeResponseSent >= 8*1024 {
		return
	}
	size := 1024
	chunk := make([]byte, size)
	for i := 0; i < size; i++ {
		chunk[i] = 'a'
	}
	p.handle.SendResponseData(chunk, false)
	p.largeResponseSent += size
}

func (p *StreamingTerminalFilter) OnAboveWriteBufferHighWatermark() {
	p.aboveW++
}
func (p *StreamingTerminalFilter) OnBelowWriteBufferLowWatermark() {
	p.belowW++
	if p.aboveW == p.belowW {
		p.sendLargeResponseChunk()
	}
}

func (p *StreamingTerminalFilter) OnStreamComplete() {
	// Force the GC to release the scheduler and related C resources.
	runtime.GC()
}

// -----------------------------------------------------------------------------
// HttpStreamBasic / Bidi / Reset
// -----------------------------------------------------------------------------

// Helper for Http Streams
type HttpStreamBasicConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (f *HttpStreamBasicConfigFactory) Create(h shared.HttpFilterConfigHandle,
	c []byte) (shared.HttpFilterFactory, error) {
	return &HttpStreamBasicFilterFactory{cluster: string(c)}, nil
}

type HttpStreamBasicFilterFactory struct{ cluster string }

func (f *HttpStreamBasicFilterFactory) Create(h shared.HttpFilterHandle) shared.HttpFilter {
	return &HttpStreamBasicFilter{handle: h, cluster: f.cluster}
}

type HttpStreamBasicFilter struct {
	shared.EmptyHttpFilter
	handle   shared.HttpFilterHandle
	cluster  string
	streamID uint64
	headers  bool
	data     bool
	complete bool
}

func (p *HttpStreamBasicFilter) OnRequestHeaders(h shared.HeaderMap, end bool) shared.HeadersStatus {
	res, id := p.handle.StartHttpStream(p.cluster,
		[][2]string{{":path", "/"}, {":method", "GET"}, {"host", "example.com"}},
		nil, true, 5000, p)
	if res != shared.HttpCalloutInitSuccess {
		p.handle.SendLocalResponse(500,
			[][2]string{{"x-error", "stream_init_failed"}}, nil, "")
		return shared.HeadersStatusStop
	}
	p.streamID = id
	success := p.handle.SendHttpStreamData(id, nil, true)
	assert(success, "send basic data")
	return shared.HeadersStatusStop
}

func (p *HttpStreamBasicFilter) OnHttpStreamHeaders(id uint64, headers [][2]string, end bool) {
	assertEq(id, p.streamID, "stream id")
	p.headers = true
	found := false
	for _, h := range headers {
		if h[0] == ":status" && h[1] == "200" {
			found = true
		}
	}
	assert(found, "status 200")
}
func (p *HttpStreamBasicFilter) OnHttpStreamData(id uint64, body [][]byte, end bool) {
	assertEq(id, p.streamID, "stream id")
	p.data = true
}
func (p *HttpStreamBasicFilter) OnHttpStreamTrailers(id uint64, trailers [][2]string) {}
func (p *HttpStreamBasicFilter) OnHttpStreamComplete(id uint64) {
	assertEq(id, p.streamID, "stream id")
	p.complete = true
	p.handle.SendLocalResponse(200,
		[][2]string{{"x-stream-test", "basic"}},
		[]byte("stream_callout_success"), "")
}
func (p *HttpStreamBasicFilter) OnHttpStreamReset(id uint64, reason shared.HttpStreamResetReason) {}

func (p *HttpStreamBasicFilter) OnStreamComplete() {
	assert(p.headers, "headers received")
	assert(p.data, "data received")
	assert(p.complete, "stream complete")
}

// Bidi
type HttpStreamBidirectionalConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (f *HttpStreamBidirectionalConfigFactory) Create(h shared.HttpFilterConfigHandle,
	c []byte) (shared.HttpFilterFactory, error) {
	return &HttpStreamBidiFilterFactory{cluster: string(c)}, nil
}

type HttpStreamBidiFilterFactory struct{ cluster string }

func (f *HttpStreamBidiFilterFactory) Create(h shared.HttpFilterHandle) shared.HttpFilter {
	return &HttpStreamBidiFilter{handle: h, cluster: f.cluster}
}

type HttpStreamBidiFilter struct {
	shared.EmptyHttpFilter
	handle       shared.HttpFilterHandle
	cluster      string
	streamID     uint64
	sentChunks   int
	sentTrailers bool
	recvHeaders  bool
	recvChunks   int
	recvTrailers bool
	complete     bool
}

func (p *HttpStreamBidiFilter) OnRequestHeaders(h shared.HeaderMap, end bool) shared.HeadersStatus {
	res, id := p.handle.StartHttpStream(p.cluster,
		[][2]string{{":path", "/stream"}, {":method", "POST"}, {"host", "example.com"}},
		nil, false, 10000, p)
	if res != shared.HttpCalloutInitSuccess {
		p.handle.SendLocalResponse(500, [][2]string{{"x-error", "stream_init_failed"}}, nil, "")
		return shared.HeadersStatusStop
	}
	p.streamID = id
	assert(p.handle.SendHttpStreamData(id, []byte("chunk1"), false), "c1")
	p.sentChunks++
	assert(p.handle.SendHttpStreamData(id, []byte("chunk2"), false), "c2")
	p.sentChunks++
	assert(p.handle.SendHttpStreamTrailers(id, [][2]string{{"x-request-trailer", "value"}}), "tr")
	p.sentTrailers = true
	return shared.HeadersStatusStop
}
func (p *HttpStreamBidiFilter) OnHttpStreamHeaders(id uint64, headers [][2]string, end bool) {
	assertEq(id, p.streamID, "id")
	p.recvHeaders = true
}
func (p *HttpStreamBidiFilter) OnHttpStreamData(id uint64, body [][]byte, end bool) {
	assertEq(id, p.streamID, "id")
	p.recvChunks++
}
func (p *HttpStreamBidiFilter) OnHttpStreamTrailers(id uint64, trailers [][2]string) {
	assertEq(id, p.streamID, "id")
	p.recvTrailers = true
}
func (p *HttpStreamBidiFilter) OnHttpStreamComplete(id uint64) {
	assertEq(id, p.streamID, "id")
	p.complete = true
	p.handle.SendLocalResponse(200, [][2]string{
		{"x-stream-test", "bidirectional"},
		{"x-chunks-sent", strconv.Itoa(p.sentChunks)},
		{"x-chunks-received", strconv.Itoa(p.recvChunks)},
	}, []byte("bidirectional_success"), "")
}
func (p *HttpStreamBidiFilter) OnHttpStreamReset(id uint64, reason shared.HttpStreamResetReason) {}

func (p *HttpStreamBidiFilter) OnStreamComplete() {
	assert(p.sentTrailers, "sentTrailers")
	assert(p.recvHeaders, "recvHeaders")
	assert(p.recvChunks > 0, "recvChunks")
	assert(p.recvTrailers, "recvTrailers")
	assert(p.complete, "complete")
}

// Reset
type UpstreamResetConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (f *UpstreamResetConfigFactory) Create(h shared.HttpFilterConfigHandle,
	c []byte) (shared.HttpFilterFactory, error) {
	return &UpstreamResetFilterFactory{cluster: string(c)}, nil
}

type UpstreamResetFilterFactory struct{ cluster string }

func (f *UpstreamResetFilterFactory) Create(h shared.HttpFilterHandle) shared.HttpFilter {
	return &UpstreamResetFilter{handle: h, cluster: f.cluster}
}

type UpstreamResetFilter struct {
	shared.EmptyHttpFilter
	handle   shared.HttpFilterHandle
	cluster  string
	streamID uint64
}

func (p *UpstreamResetFilter) OnRequestHeaders(h shared.HeaderMap, end bool) shared.HeadersStatus {
	res, id := p.handle.StartHttpStream(p.cluster,
		[][2]string{{":path", "/reset"}, {":method", "GET"}, {"host", "example.com"}},
		nil, true, 5000, p)
	if res != shared.HttpCalloutInitSuccess {
		p.handle.SendLocalResponse(500, [][2]string{{"x-error", "stream_init_failed"}}, nil, "")
		return shared.HeadersStatusStop
	}
	p.streamID = id
	return shared.HeadersStatusStop
}
func (p *UpstreamResetFilter) OnHttpStreamHeaders(id uint64, headers [][2]string, end bool) {}
func (p *UpstreamResetFilter) OnHttpStreamData(id uint64, body [][]byte, end bool)          {}
func (p *UpstreamResetFilter) OnHttpStreamTrailers(id uint64, trailers [][2]string)         {}
func (p *UpstreamResetFilter) OnHttpStreamComplete(id uint64)                               {}
func (p *UpstreamResetFilter) OnHttpStreamReset(id uint64, reason shared.HttpStreamResetReason) {
	assertEq(id, p.streamID, "id")
	p.handle.SendLocalResponse(200, [][2]string{{"x-reset", "true"}}, []byte("upstream_reset"), "")
}
