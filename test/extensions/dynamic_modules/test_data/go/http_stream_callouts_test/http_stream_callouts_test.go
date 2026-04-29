// HTTP stream callouts test module. Mirrors test_data/rust/http_stream_callouts_test.rs.
//
// Exercises the StartHttpStream / SendHttpStreamData / SendHttpStreamTrailers /
// ResetHttpStream API family across five filter shapes:
//
//   basic_stream_lifecycle  - start, receive headers/data, complete
//   bidirectional_streaming - send chunks + trailers, count received chunks
//   multiple_streams        - 3 concurrent streams
//   stream_reset            - reset on first headers callback
//   upstream_reset          - rely on upstream to reset
//
// This module is built but currently has no integration driver — its purpose is to
// exercise the SDK API surface at compile time, paralleling the Rust module of the
// same name.
package main

import (
	"fmt"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterHttpFilterConfigFactories(map[string]shared.HttpFilterConfigFactory{
		"basic_stream_lifecycle":  &basicStreamLifecycleFactory{},
		"bidirectional_streaming": &bidirectionalStreamingFactory{},
		"multiple_streams":        &multipleStreamsFactory{},
		"stream_reset":            &streamResetFactory{},
		"upstream_reset":          &upstreamResetFactory{},
	})
}

func main() {} //nolint:all

// =============================================================================
// Test 1: Basic Stream Lifecycle
// =============================================================================

type basicStreamLifecycleFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (basicStreamLifecycleFactory) Create(_ shared.HttpFilterConfigHandle, c []byte) (shared.HttpFilterFactory, error) {
	return &basicStreamLifecycleFilterFactory{cluster: string(c)}, nil
}

type basicStreamLifecycleFilterFactory struct {
	shared.EmptyHttpFilterFactory
	cluster string
}

func (f *basicStreamLifecycleFilterFactory) Create(h shared.HttpFilterHandle) shared.HttpFilter {
	return &basicStreamLifecycleFilter{handle: h, cluster: f.cluster}
}

type basicStreamLifecycleFilter struct {
	shared.EmptyHttpFilter
	handle           shared.HttpFilterHandle
	cluster          string
	receivedResponse bool
}

func (p *basicStreamLifecycleFilter) OnRequestHeaders(_ shared.HeaderMap, _ bool) shared.HeadersStatus {
	res, _ := p.handle.StartHttpStream(p.cluster,
		[][2]string{{":path", "/test"}, {":method", "GET"}, {"host", "example.com"}},
		nil, true, 5000, p)
	if res != shared.HttpCalloutInitSuccess {
		p.handle.SendLocalResponse(500, [][2]string{{"x-error", "stream_init_failed"}}, nil, "")
		return shared.HeadersStatusStop
	}
	return shared.HeadersStatusStop
}

func (p *basicStreamLifecycleFilter) OnHttpStreamHeaders(_ uint64, _ [][2]shared.UnsafeEnvoyBuffer, _ bool) {
}
func (p *basicStreamLifecycleFilter) OnHttpStreamData(_ uint64, _ []shared.UnsafeEnvoyBuffer, _ bool) {
}
func (p *basicStreamLifecycleFilter) OnHttpStreamTrailers(_ uint64, _ [][2]shared.UnsafeEnvoyBuffer) {
}
func (p *basicStreamLifecycleFilter) OnHttpStreamComplete(_ uint64) {
	p.receivedResponse = true
	p.handle.SendLocalResponse(200,
		[][2]string{{"x-stream", "success"}},
		[]byte("stream_callout_success"), "")
}
func (p *basicStreamLifecycleFilter) OnHttpStreamReset(_ uint64, _ shared.HttpStreamResetReason) {
}

// =============================================================================
// Test 2: Bidirectional Streaming
// =============================================================================

type bidirectionalStreamingFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (bidirectionalStreamingFactory) Create(_ shared.HttpFilterConfigHandle, c []byte) (shared.HttpFilterFactory, error) {
	return &bidirectionalStreamingFilterFactory{cluster: string(c)}, nil
}

type bidirectionalStreamingFilterFactory struct {
	shared.EmptyHttpFilterFactory
	cluster string
}

func (f *bidirectionalStreamingFilterFactory) Create(h shared.HttpFilterHandle) shared.HttpFilter {
	return &bidirectionalStreamingFilter{handle: h, cluster: f.cluster}
}

type bidirectionalStreamingFilter struct {
	shared.EmptyHttpFilter
	handle         shared.HttpFilterHandle
	cluster        string
	streamHandle   uint64
	chunksReceived int
}

func (p *bidirectionalStreamingFilter) OnRequestHeaders(_ shared.HeaderMap, _ bool) shared.HeadersStatus {
	res, id := p.handle.StartHttpStream(p.cluster,
		[][2]string{
			{":path", "/stream"},
			{":method", "POST"},
			{"host", "example.com"},
			{"content-type", "application/octet-stream"},
		},
		nil, false, 10000, p)
	if res != shared.HttpCalloutInitSuccess {
		p.handle.SendLocalResponse(500, [][2]string{{"x-error", "stream_init_failed"}}, nil, "")
		return shared.HeadersStatusStop
	}
	p.streamHandle = id

	// Send chunks + trailers.
	if !p.handle.SendHttpStreamData(id, []byte("chunk1"), false) {
		p.handle.SendLocalResponse(500, [][2]string{{"x-error", "send_chunk1"}}, nil, "")
		return shared.HeadersStatusStop
	}
	if !p.handle.SendHttpStreamData(id, []byte("chunk2"), false) {
		p.handle.SendLocalResponse(500, [][2]string{{"x-error", "send_chunk2"}}, nil, "")
		return shared.HeadersStatusStop
	}
	if !p.handle.SendHttpStreamTrailers(id, [][2]string{{"x-trailer", "value"}}) {
		p.handle.SendLocalResponse(500, [][2]string{{"x-error", "send_trailers"}}, nil, "")
		return shared.HeadersStatusStop
	}

	return shared.HeadersStatusStop
}

func (p *bidirectionalStreamingFilter) OnHttpStreamHeaders(_ uint64, _ [][2]shared.UnsafeEnvoyBuffer, _ bool) {
}
func (p *bidirectionalStreamingFilter) OnHttpStreamData(id uint64, _ []shared.UnsafeEnvoyBuffer, _ bool) {
	if id == p.streamHandle {
		p.chunksReceived++
	}
}
func (p *bidirectionalStreamingFilter) OnHttpStreamTrailers(_ uint64, _ [][2]shared.UnsafeEnvoyBuffer) {
}
func (p *bidirectionalStreamingFilter) OnHttpStreamComplete(id uint64) {
	if id != p.streamHandle {
		return
	}
	p.handle.SendLocalResponse(200,
		[][2]string{{"x-chunks-received", fmt.Sprintf("%d", p.chunksReceived)}},
		[]byte("bidirectional_success"), "")
}
func (p *bidirectionalStreamingFilter) OnHttpStreamReset(_ uint64, _ shared.HttpStreamResetReason) {
}

// =============================================================================
// Test 3: Multiple Streams
// =============================================================================

type multipleStreamsFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (multipleStreamsFactory) Create(_ shared.HttpFilterConfigHandle, c []byte) (shared.HttpFilterFactory, error) {
	return &multipleStreamsFilterFactory{cluster: string(c)}, nil
}

type multipleStreamsFilterFactory struct {
	shared.EmptyHttpFilterFactory
	cluster string
}

func (f *multipleStreamsFilterFactory) Create(h shared.HttpFilterHandle) shared.HttpFilter {
	return &multipleStreamsFilter{handle: h, cluster: f.cluster}
}

type multipleStreamsFilter struct {
	shared.EmptyHttpFilter
	handle           shared.HttpFilterHandle
	cluster          string
	streamHandles    []uint64
	completedStreams int
}

func (p *multipleStreamsFilter) OnRequestHeaders(_ shared.HeaderMap, _ bool) shared.HeadersStatus {
	for i := 1; i <= 3; i++ {
		path := fmt.Sprintf("/stream%d", i)
		res, id := p.handle.StartHttpStream(p.cluster,
			[][2]string{{":path", path}, {":method", "GET"}, {"host", "example.com"}},
			nil, true, 5000, p)
		if res == shared.HttpCalloutInitSuccess {
			p.streamHandles = append(p.streamHandles, id)
		}
	}
	if len(p.streamHandles) != 3 {
		p.handle.SendLocalResponse(500, [][2]string{{"x-error", "stream_init_failed"}}, nil, "")
	}
	return shared.HeadersStatusStop
}

func (p *multipleStreamsFilter) OnHttpStreamHeaders(_ uint64, _ [][2]shared.UnsafeEnvoyBuffer, _ bool) {
}
func (p *multipleStreamsFilter) OnHttpStreamData(_ uint64, _ []shared.UnsafeEnvoyBuffer, _ bool) {
}
func (p *multipleStreamsFilter) OnHttpStreamTrailers(_ uint64, _ [][2]shared.UnsafeEnvoyBuffer) {
}
func (p *multipleStreamsFilter) OnHttpStreamComplete(id uint64) {
	for _, h := range p.streamHandles {
		if h == id {
			p.completedStreams++
			break
		}
	}
	if p.completedStreams == 3 {
		p.handle.SendLocalResponse(200, [][2]string{{"x-stream", "all_success"}}, nil, "")
	}
}
func (p *multipleStreamsFilter) OnHttpStreamReset(_ uint64, _ shared.HttpStreamResetReason) {}

// =============================================================================
// Test 4: Stream Reset
// =============================================================================

type streamResetFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (streamResetFactory) Create(_ shared.HttpFilterConfigHandle, c []byte) (shared.HttpFilterFactory, error) {
	return &streamResetFilterFactory{cluster: string(c)}, nil
}

type streamResetFilterFactory struct {
	shared.EmptyHttpFilterFactory
	cluster string
}

func (f *streamResetFilterFactory) Create(h shared.HttpFilterHandle) shared.HttpFilter {
	return &streamResetFilter{handle: h, cluster: f.cluster}
}

type streamResetFilter struct {
	shared.EmptyHttpFilter
	handle       shared.HttpFilterHandle
	cluster      string
	streamHandle uint64
	receivedHdrs bool
	resetCalled  bool
}

func (p *streamResetFilter) OnRequestHeaders(_ shared.HeaderMap, _ bool) shared.HeadersStatus {
	res, id := p.handle.StartHttpStream(p.cluster,
		[][2]string{{":path", "/slow"}, {":method", "GET"}, {"host", "example.com"}},
		nil, true, 5000, p)
	if res != shared.HttpCalloutInitSuccess {
		p.handle.SendLocalResponse(500, [][2]string{{"x-error", "stream_init_failed"}}, nil, "")
		return shared.HeadersStatusStop
	}
	p.streamHandle = id
	return shared.HeadersStatusStop
}

func (p *streamResetFilter) OnHttpStreamHeaders(id uint64, _ [][2]shared.UnsafeEnvoyBuffer, _ bool) {
	if id != p.streamHandle {
		return
	}
	p.receivedHdrs = true
	// Immediately reset the stream after receiving headers.
	p.handle.ResetHttpStream(id)
}
func (p *streamResetFilter) OnHttpStreamData(_ uint64, _ []shared.UnsafeEnvoyBuffer, _ bool) {}
func (p *streamResetFilter) OnHttpStreamTrailers(_ uint64, _ [][2]shared.UnsafeEnvoyBuffer)  {}
func (p *streamResetFilter) OnHttpStreamComplete(_ uint64)                                   {}
func (p *streamResetFilter) OnHttpStreamReset(id uint64, _ shared.HttpStreamResetReason) {
	if id != p.streamHandle {
		return
	}
	p.resetCalled = true
	p.handle.SendLocalResponse(200,
		[][2]string{{"x-stream", "reset_ok"}},
		[]byte("stream_was_reset"), "")
}

// =============================================================================
// Test 5: Upstream Reset
// =============================================================================

type upstreamResetFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (upstreamResetFactory) Create(_ shared.HttpFilterConfigHandle, c []byte) (shared.HttpFilterFactory, error) {
	return &upstreamResetFilterFactory{cluster: string(c)}, nil
}

type upstreamResetFilterFactory struct {
	shared.EmptyHttpFilterFactory
	cluster string
}

func (f *upstreamResetFilterFactory) Create(h shared.HttpFilterHandle) shared.HttpFilter {
	return &upstreamResetFilter{handle: h, cluster: f.cluster}
}

type upstreamResetFilter struct {
	shared.EmptyHttpFilter
	handle       shared.HttpFilterHandle
	cluster      string
	streamHandle uint64
}

func (p *upstreamResetFilter) OnRequestHeaders(_ shared.HeaderMap, _ bool) shared.HeadersStatus {
	res, id := p.handle.StartHttpStream(p.cluster,
		[][2]string{{":path", "/reset"}, {":method", "GET"}, {"host", "example.com"}},
		nil, true, 5000, p)
	if res != shared.HttpCalloutInitSuccess {
		p.handle.SendLocalResponse(500, [][2]string{{"x-error", "stream_init_failed"}}, nil, "")
		return shared.HeadersStatusStop
	}
	p.streamHandle = id
	return shared.HeadersStatusStop
}

func (p *upstreamResetFilter) OnHttpStreamHeaders(_ uint64, _ [][2]shared.UnsafeEnvoyBuffer, _ bool) {
}
func (p *upstreamResetFilter) OnHttpStreamData(_ uint64, _ []shared.UnsafeEnvoyBuffer, _ bool) {}
func (p *upstreamResetFilter) OnHttpStreamTrailers(_ uint64, _ [][2]shared.UnsafeEnvoyBuffer)  {}
func (p *upstreamResetFilter) OnHttpStreamComplete(_ uint64)                                   {}
func (p *upstreamResetFilter) OnHttpStreamReset(id uint64, _ shared.HttpStreamResetReason) {
	if id != p.streamHandle {
		return
	}
	p.handle.SendLocalResponse(200,
		[][2]string{{"x-reset", "true"}},
		[]byte("upstream_reset"), "")
}
