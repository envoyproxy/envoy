// Integration test module for access logger dynamic modules. Mirrors
// test_data/rust/access_log_integration_test.rs.
//
// Registers an access logger that:
//   - Calls every AccessLogContext getter (~50 of them) to ensure the dispatch path is
//     wired and no method panics on real Envoy data.
//   - Captures select getter results into per-config counters/gauges so the C++ driver
//     can assert via /stats that the values match what Envoy actually saw on the wire.
//
// Counters defined (all incremented per Log() call):
//
//	test_log_count                — number of Log() calls observed.
//	test_request_protocol_http2   — incremented when request_protocol == "HTTP/2".
//	test_response_code_200        — incremented when response_code == 200.
//	test_method_get               — incremented when :method == "GET".
//	test_path_test                — incremented when :path == "/test".
//	test_has_route_name           — incremented when xds_route_name returned ok.
//
// Gauges (set per Log() call):
//
//	test_response_code_last       — last observed response_code as uint64.
//	test_bytes_sent_last          — last observed bytes_sent from BytesInfo.
//	test_request_headers_count    — current request header count.
package main

import (
	"sync/atomic"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterAccessLoggerConfigFactories(map[string]shared.AccessLoggerConfigFactory{
		"test_logger": &testLoggerConfigFactory{},
	})
}

func main() {}

// Process-wide counters; the test asserts indirectly by sending requests and checking the
// upstream side completes without crash, so we use these mainly for diagnostics.
var (
	logCount   atomic.Uint32
	flushCount atomic.Uint32
)

type testLoggerConfigFactory struct {
	shared.EmptyAccessLoggerConfigFactory
}

func (f *testLoggerConfigFactory) Create(handle shared.AccessLoggerConfigHandle, _ []byte) (shared.AccessLoggerFactory, error) {
	logCountID, _ := handle.DefineCounter("test_log_count")
	httpProtoID, _ := handle.DefineCounter("test_request_protocol_http2")
	resp200ID, _ := handle.DefineCounter("test_response_code_200")
	methodGetID, _ := handle.DefineCounter("test_method_get")
	pathTestID, _ := handle.DefineCounter("test_path_test")
	hasRouteID, _ := handle.DefineCounter("test_has_route_name")

	respCodeGaugeID, _ := handle.DefineGauge("test_response_code_last")
	bytesSentGaugeID, _ := handle.DefineGauge("test_bytes_sent_last")
	hdrCountGaugeID, _ := handle.DefineGauge("test_request_headers_count")

	return &testLoggerFactory{
		handle:           handle,
		logCountID:       logCountID,
		httpProtoID:      httpProtoID,
		resp200ID:        resp200ID,
		methodGetID:      methodGetID,
		pathTestID:       pathTestID,
		hasRouteID:       hasRouteID,
		respCodeGaugeID:  respCodeGaugeID,
		bytesSentGaugeID: bytesSentGaugeID,
		hdrCountGaugeID:  hdrCountGaugeID,
	}, nil
}

type testLoggerFactory struct {
	shared.EmptyAccessLoggerFactory
	handle           shared.AccessLoggerConfigHandle
	logCountID       shared.MetricID
	httpProtoID      shared.MetricID
	resp200ID        shared.MetricID
	methodGetID      shared.MetricID
	pathTestID       shared.MetricID
	hasRouteID       shared.MetricID
	respCodeGaugeID  shared.MetricID
	bytesSentGaugeID shared.MetricID
	hdrCountGaugeID  shared.MetricID
}

func (f *testLoggerFactory) Create() shared.AccessLogger {
	return &testLogger{factory: f}
}

type testLogger struct {
	shared.EmptyAccessLogger
	factory *testLoggerFactory
}

func (l *testLogger) Log(ctx shared.AccessLogContext, _ shared.AccessLogType) {
	logCount.Add(1)
	f := l.factory
	f.handle.IncrementCounter(f.logCountID, 1)

	// Exercise every AccessLogContext getter so the dispatch path for each ABI callback
	// is hit on every request. We additionally record values from a small subset into
	// counters/gauges so the C++ driver can assert correctness via /stats.

	_ = ctx.GetWorkerIndex()

	// :method header — bump counter when GET.
	if v, n, ok := ctx.GetHeaderValue(shared.HttpHeaderTypeRequestHeader, ":method", 0); ok && n > 0 {
		if v.ToUnsafeString() == "GET" {
			f.handle.IncrementCounter(f.methodGetID, 1)
		}
	}
	// :path — bump counter when "/test".
	if v, n, ok := ctx.GetHeaderValue(shared.HttpHeaderTypeRequestHeader, ":path", 0); ok && n > 0 {
		if v.ToUnsafeString() == "/test" {
			f.handle.IncrementCounter(f.pathTestID, 1)
		}
	}

	// Bulk header iteration on all three maps to make sure dispatch survives.
	for _, ht := range []shared.HttpHeaderType{
		shared.HttpHeaderTypeRequestHeader,
		shared.HttpHeaderTypeResponseHeader,
		shared.HttpHeaderTypeResponseTrailer,
	} {
		_ = ctx.GetHeadersSize(ht)
		_ = ctx.GetHeaders(ht)
	}
	// Record request header count as a gauge.
	f.handle.SetGauge(f.hdrCountGaugeID, ctx.GetHeadersSize(shared.HttpHeaderTypeRequestHeader))

	// Attribute getters across all three return types.
	if v, ok := ctx.GetAttributeString(shared.AttributeIDRequestProtocol); ok {
		if v.ToUnsafeString() == "HTTP/2" {
			f.handle.IncrementCounter(f.httpProtoID, 1)
		}
	}
	_, _ = ctx.GetAttributeString(shared.AttributeIDXdsRouteName)
	_, _ = ctx.GetAttributeString(shared.AttributeIDRequestPath)
	if code, ok := ctx.GetAttributeNumber(shared.AttributeIDResponseCode); ok {
		f.handle.SetGauge(f.respCodeGaugeID, uint64(code))
		if uint64(code) == 200 {
			f.handle.IncrementCounter(f.resp200ID, 1)
		}
	}
	_, _ = ctx.GetAttributeNumber(shared.AttributeIDConnectionId)
	_, _ = ctx.GetAttributeBool(shared.AttributeIDConnectionMTLS)

	// Response flags.
	_ = ctx.HasResponseFlag(shared.ResponseFlagNoRouteFound)
	_ = ctx.GetResponseFlags()

	// Timing + bytes — record bytes_sent into a gauge.
	_ = ctx.GetTimingInfo()
	bi := ctx.GetBytesInfo()
	f.handle.SetGauge(f.bytesSentGaugeID, bi.BytesSent)

	// Addresses.
	_, _, _ = ctx.GetDownstreamRemoteAddress()
	_, _, _ = ctx.GetDownstreamLocalAddress()
	_, _, _ = ctx.GetDownstreamDirectRemoteAddress()
	_, _, _ = ctx.GetDownstreamDirectLocalAddress()
	_, _, _ = ctx.GetUpstreamRemoteAddress()
	_, _, _ = ctx.GetUpstreamLocalAddress()

	// Upstream info.
	_, _ = ctx.GetUpstreamCluster()
	_, _ = ctx.GetUpstreamHost()
	_ = ctx.GetUpstreamConnectionID()
	_, _ = ctx.GetUpstreamTLSCipher()
	_, _ = ctx.GetUpstreamTLSSessionID()
	_, _ = ctx.GetUpstreamPeerIssuer()
	_ = ctx.GetUpstreamPeerCertValidityStart()
	_ = ctx.GetUpstreamPeerCertValidityEnd()
	_ = ctx.GetUpstreamPeerURISans()
	_ = ctx.GetUpstreamLocalURISans()
	_ = ctx.GetUpstreamPeerDNSSans()
	_ = ctx.GetUpstreamLocalDNSSans()

	// Downstream TLS / connection info.
	_, _ = ctx.GetDownstreamTLSCipher()
	_, _ = ctx.GetDownstreamTLSSessionID()
	_, _ = ctx.GetDownstreamPeerIssuer()
	_, _ = ctx.GetDownstreamPeerSerial()
	_, _ = ctx.GetDownstreamPeerFingerprint1()
	_ = ctx.GetDownstreamPeerCertPresented()
	_ = ctx.GetDownstreamPeerCertValidated()
	_ = ctx.GetDownstreamPeerCertValidityStart()
	_ = ctx.GetDownstreamPeerCertValidityEnd()
	_ = ctx.GetDownstreamPeerURISans()
	_ = ctx.GetDownstreamLocalURISans()
	_ = ctx.GetDownstreamPeerDNSSans()
	_ = ctx.GetDownstreamLocalDNSSans()

	// Metadata / filter state / tracing.
	_, _ = ctx.GetDynamicMetadata("envoy.filters.http.dynamic_modules", "test_key")
	_, _ = ctx.GetFilterState("test_key")
	_, _ = ctx.GetLocalReplyBody()
	_, _ = ctx.GetTraceID()
	_, _ = ctx.GetSpanID()
	_ = ctx.IsTraceSampled()

	// Misc.
	_, _ = ctx.GetJA3Hash()
	_, _ = ctx.GetJA4Hash()
	_ = ctx.GetRequestHeadersBytes()
	_ = ctx.GetResponseHeadersBytes()
	_ = ctx.GetResponseTrailersBytes()
	_, _ = ctx.GetUpstreamProtocol()
	_ = ctx.GetUpstreamPoolReadyDurationNs()

	// xds_route_name presence check (route is configured on the test).
	if _, ok := ctx.GetAttributeString(shared.AttributeIDXdsRouteName); ok {
		f.handle.IncrementCounter(f.hasRouteID, 1)
	}
}

func (l *testLogger) Flush() {
	flushCount.Add(1)
}
