// Integration test module for access logger dynamic modules. Mirrors
// test_data/rust/access_log_integration_test.rs.
//
// Registers an access logger that:
//   - Defines a counter at config time and increments it per Log call.
//   - Calls every AccessLogContext getter (~50 of them) to ensure the dispatch path is
//     wired and no method panics on real Envoy data.
//   - Tracks log/flush counts for assertion-by-presence by the C++ test driver.
//
// The test passes if Envoy successfully sends multiple HTTP requests through the access
// logger without crashing — i.e. all the getters return cleanly.
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
	counterID, _ := handle.DefineCounter("test_log_count")
	return &testLoggerFactory{counterID: counterID, handle: handle}, nil
}

type testLoggerFactory struct {
	shared.EmptyAccessLoggerFactory
	counterID shared.MetricID
	handle    shared.AccessLoggerConfigHandle
}

func (f *testLoggerFactory) Create() shared.AccessLogger {
	return &testLogger{counterID: f.counterID, handle: f.handle}
}

type testLogger struct {
	shared.EmptyAccessLogger
	counterID shared.MetricID
	handle    shared.AccessLoggerConfigHandle
}

func (l *testLogger) Log(ctx shared.AccessLogContext, _ shared.AccessLogType) {
	logCount.Add(1)
	l.handle.IncrementCounter(l.counterID, 1)

	// Exercise every AccessLogContext getter so the dispatch path for each ABI callback
	// is hit on every request. Discard return values — the goal is to flush out crashes,
	// not to assert specific data.
	_ = ctx.GetWorkerIndex()

	// Header bulk + per-key access.
	for _, ht := range []shared.HttpHeaderType{
		shared.HttpHeaderTypeRequestHeader,
		shared.HttpHeaderTypeResponseHeader,
		shared.HttpHeaderTypeResponseTrailer,
	} {
		_ = ctx.GetHeadersSize(ht)
		_ = ctx.GetHeaders(ht)
		_, _, _ = ctx.GetHeaderValue(ht, ":method", 0)
	}

	// Attribute getters across all three return types.
	for _, id := range []shared.AttributeID{
		shared.AttributeIDRequestProtocol,
		shared.AttributeIDXdsRouteName,
		shared.AttributeIDRequestPath,
	} {
		_, _ = ctx.GetAttributeString(id)
	}
	_, _ = ctx.GetAttributeNumber(shared.AttributeIDResponseCode)
	_, _ = ctx.GetAttributeNumber(shared.AttributeIDConnectionId)
	_, _ = ctx.GetAttributeBool(shared.AttributeIDConnectionMTLS)

	// Response flags.
	_ = ctx.HasResponseFlag(shared.ResponseFlagNoRouteFound)
	_ = ctx.GetResponseFlags()

	// Timing + bytes.
	_ = ctx.GetTimingInfo()
	_ = ctx.GetBytesInfo()

	// Addresses (downstream + direct + upstream, both sides).
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
}

func (l *testLogger) Flush() {
	flushCount.Add(1)
}
