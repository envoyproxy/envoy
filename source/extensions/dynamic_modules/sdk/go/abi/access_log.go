package abi

/*
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup
#cgo linux LDFLAGS: -Wl,--unresolved-symbols=ignore-all
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../../abi/abi.h"
*/
import "C"
import (
	"runtime"
	"unsafe"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

type accessLoggerConfigWrapper struct {
	factory      shared.AccessLoggerFactory
	configHandle *dymAccessLoggerConfigHandle

	// destroyed is set during config_destroy so any late logger creation becomes a
	// no-op instead of re-entering user code.
	destroyed bool
}

type accessLoggerWrapper struct {
	logger shared.AccessLogger

	// destroyed is set during logger_destroy so any late log / flush callbacks become
	// no-ops.
	destroyed bool
}

var accessLoggerConfigManager = newManager[accessLoggerConfigWrapper]()
var accessLoggerManager = newManager[accessLoggerWrapper]()

// dymAccessLoggerConfigHandle implements shared.AccessLoggerConfigHandle.
type dymAccessLoggerConfigHandle struct {
	hostConfigPtr C.envoy_dynamic_module_type_access_logger_config_envoy_ptr
}

func (h *dymAccessLoggerConfigHandle) DefineCounter(name string) (shared.MetricID, shared.MetricsResult) {
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_access_logger_config_define_counter(
		h.hostConfigPtr, stringToModuleBuffer(name), &id)
	runtime.KeepAlive(name)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymAccessLoggerConfigHandle) DefineGauge(name string) (shared.MetricID, shared.MetricsResult) {
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_access_logger_config_define_gauge(
		h.hostConfigPtr, stringToModuleBuffer(name), &id)
	runtime.KeepAlive(name)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymAccessLoggerConfigHandle) DefineHistogram(name string) (shared.MetricID, shared.MetricsResult) {
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_access_logger_config_define_histogram(
		h.hostConfigPtr, stringToModuleBuffer(name), &id)
	runtime.KeepAlive(name)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymAccessLoggerConfigHandle) IncrementCounter(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_access_logger_increment_counter(
		h.hostConfigPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymAccessLoggerConfigHandle) SetGauge(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_access_logger_set_gauge(
		h.hostConfigPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymAccessLoggerConfigHandle) IncrementGauge(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_access_logger_increment_gauge(
		h.hostConfigPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymAccessLoggerConfigHandle) DecrementGauge(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_access_logger_decrement_gauge(
		h.hostConfigPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymAccessLoggerConfigHandle) RecordHistogramValue(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_access_logger_record_histogram_value(
		h.hostConfigPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

// dymAccessLogContext implements shared.AccessLogContext. It is bound to a single Log call —
// the underlying envoy ptr is only valid for that callback.
type dymAccessLogContext struct {
	hostLoggerPtr C.envoy_dynamic_module_type_access_logger_envoy_ptr
}

// helper: returns (UnsafeEnvoyBuffer, true) when the callback returned true and produced a
// non-empty buffer.
func accessLogStrResult(buf C.envoy_dynamic_module_type_envoy_buffer, ok C.bool) (shared.UnsafeEnvoyBuffer, bool) {
	if !bool(ok) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ok)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func accessLogAddrResult(buf C.envoy_dynamic_module_type_envoy_buffer, port C.uint32_t, ok C.bool) (shared.UnsafeEnvoyBuffer, uint32, bool) {
	if !bool(ok) {
		return shared.UnsafeEnvoyBuffer{}, 0, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), uint32(port), true
}

// ---- headers ----

func (c *dymAccessLogContext) GetHeadersSize(headerType shared.HttpHeaderType) uint64 {
	return uint64(C.envoy_dynamic_module_callback_access_logger_get_headers_size(
		c.hostLoggerPtr, C.envoy_dynamic_module_type_http_header_type(headerType)))
}

func (c *dymAccessLogContext) GetHeaders(headerType shared.HttpHeaderType) [][2]shared.UnsafeEnvoyBuffer {
	size := C.envoy_dynamic_module_callback_access_logger_get_headers_size(
		c.hostLoggerPtr, C.envoy_dynamic_module_type_http_header_type(headerType))
	if size == 0 {
		return nil
	}
	hdrs := make([]C.envoy_dynamic_module_type_envoy_http_header, int(size))
	if !bool(C.envoy_dynamic_module_callback_access_logger_get_headers(
		c.hostLoggerPtr, C.envoy_dynamic_module_type_http_header_type(headerType),
		unsafe.SliceData(hdrs))) {
		return nil
	}
	out := envoyHttpHeaderSliceToUnsafeHeaderSlice(hdrs)
	runtime.KeepAlive(hdrs)
	return out
}

func (c *dymAccessLogContext) GetHeaderValue(headerType shared.HttpHeaderType, key string, index uint64) (shared.UnsafeEnvoyBuffer, uint64, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var total C.size_t
	ret := C.envoy_dynamic_module_callback_access_logger_get_header_value(
		c.hostLoggerPtr,
		C.envoy_dynamic_module_type_http_header_type(headerType),
		stringToModuleBuffer(key),
		&buf,
		C.size_t(index),
		&total,
	)
	runtime.KeepAlive(key)
	if !bool(ret) {
		return shared.UnsafeEnvoyBuffer{}, uint64(total), false
	}
	if buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, uint64(total), true
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), uint64(total), true
}

// ---- attribute accessors ----

func (c *dymAccessLogContext) GetAttributeString(id shared.AttributeID) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_attribute_string(
		c.hostLoggerPtr, C.envoy_dynamic_module_type_attribute_id(id), &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetAttributeNumber(id shared.AttributeID) (uint64, bool) {
	var v C.uint64_t
	ok := C.envoy_dynamic_module_callback_access_logger_get_attribute_int(
		c.hostLoggerPtr, C.envoy_dynamic_module_type_attribute_id(id), &v)
	if !bool(ok) {
		return 0, false
	}
	return uint64(v), true
}

func (c *dymAccessLogContext) GetAttributeBool(id shared.AttributeID) (bool, bool) {
	var v C.bool
	ok := C.envoy_dynamic_module_callback_access_logger_get_attribute_bool(
		c.hostLoggerPtr, C.envoy_dynamic_module_type_attribute_id(id), &v)
	if !bool(ok) {
		return false, false
	}
	return bool(v), true
}

// ---- response flags / timing / bytes ----

func (c *dymAccessLogContext) HasResponseFlag(flag shared.ResponseFlag) bool {
	return bool(C.envoy_dynamic_module_callback_access_logger_has_response_flag(
		c.hostLoggerPtr, C.envoy_dynamic_module_type_response_flag(flag)))
}

func (c *dymAccessLogContext) GetResponseFlags() uint64 {
	return uint64(C.envoy_dynamic_module_callback_access_logger_get_response_flags(c.hostLoggerPtr))
}

func (c *dymAccessLogContext) GetTimingInfo() shared.AccessLogTimingInfo {
	var t C.envoy_dynamic_module_type_timing_info
	C.envoy_dynamic_module_callback_access_logger_get_timing_info(c.hostLoggerPtr, &t)
	return shared.AccessLogTimingInfo{
		StartTimeUnixNs:               int64(t.start_time_unix_ns),
		RequestCompleteDurationNs:     int64(t.request_complete_duration_ns),
		FirstUpstreamTxByteSentNs:     int64(t.first_upstream_tx_byte_sent_ns),
		LastUpstreamTxByteSentNs:      int64(t.last_upstream_tx_byte_sent_ns),
		FirstUpstreamRxByteReceivedNs: int64(t.first_upstream_rx_byte_received_ns),
		LastUpstreamRxByteReceivedNs:  int64(t.last_upstream_rx_byte_received_ns),
		FirstDownstreamTxByteSentNs:   int64(t.first_downstream_tx_byte_sent_ns),
		LastDownstreamTxByteSentNs:    int64(t.last_downstream_tx_byte_sent_ns),
	}
}

func (c *dymAccessLogContext) GetBytesInfo() shared.AccessLogBytesInfo {
	var b C.envoy_dynamic_module_type_bytes_info
	C.envoy_dynamic_module_callback_access_logger_get_bytes_info(c.hostLoggerPtr, &b)
	return shared.AccessLogBytesInfo{
		BytesReceived:     uint64(b.bytes_received),
		BytesSent:         uint64(b.bytes_sent),
		WireBytesReceived: uint64(b.wire_bytes_received),
		WireBytesSent:     uint64(b.wire_bytes_sent),
	}
}

// ---- addresses ----

func (c *dymAccessLogContext) GetDownstreamRemoteAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_access_logger_get_downstream_remote_address(c.hostLoggerPtr, &buf, &port)
	return accessLogAddrResult(buf, port, ok)
}

func (c *dymAccessLogContext) GetDownstreamLocalAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_access_logger_get_downstream_local_address(c.hostLoggerPtr, &buf, &port)
	return accessLogAddrResult(buf, port, ok)
}

func (c *dymAccessLogContext) GetDownstreamDirectRemoteAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_access_logger_get_downstream_direct_remote_address(c.hostLoggerPtr, &buf, &port)
	return accessLogAddrResult(buf, port, ok)
}

func (c *dymAccessLogContext) GetDownstreamDirectLocalAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_access_logger_get_downstream_direct_local_address(c.hostLoggerPtr, &buf, &port)
	return accessLogAddrResult(buf, port, ok)
}

func (c *dymAccessLogContext) GetUpstreamRemoteAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_access_logger_get_upstream_remote_address(c.hostLoggerPtr, &buf, &port)
	return accessLogAddrResult(buf, port, ok)
}

func (c *dymAccessLogContext) GetUpstreamLocalAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_access_logger_get_upstream_local_address(c.hostLoggerPtr, &buf, &port)
	return accessLogAddrResult(buf, port, ok)
}

// ---- upstream info ----

func (c *dymAccessLogContext) GetUpstreamCluster() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_upstream_cluster(c.hostLoggerPtr, &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetUpstreamHost() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_upstream_host(c.hostLoggerPtr, &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetUpstreamConnectionID() uint64 {
	return uint64(C.envoy_dynamic_module_callback_access_logger_get_upstream_connection_id(c.hostLoggerPtr))
}

func (c *dymAccessLogContext) GetUpstreamTLSCipher() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_upstream_tls_cipher(c.hostLoggerPtr, &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetUpstreamTLSSessionID() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_upstream_tls_session_id(c.hostLoggerPtr, &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetUpstreamPeerIssuer() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_upstream_peer_issuer(c.hostLoggerPtr, &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetUpstreamPeerCertValidityStart() int64 {
	return int64(C.envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_v_start(c.hostLoggerPtr))
}

func (c *dymAccessLogContext) GetUpstreamPeerCertValidityEnd() int64 {
	return int64(C.envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_v_end(c.hostLoggerPtr))
}

func (c *dymAccessLogContext) sansViaSize(
	sizeFn func(C.envoy_dynamic_module_type_access_logger_envoy_ptr) C.size_t,
	getFn func(C.envoy_dynamic_module_type_access_logger_envoy_ptr, *C.envoy_dynamic_module_type_envoy_buffer) C.bool,
) []shared.UnsafeEnvoyBuffer {
	size := sizeFn(c.hostLoggerPtr)
	if size == 0 {
		return nil
	}
	bufs := make([]C.envoy_dynamic_module_type_envoy_buffer, int(size))
	if !bool(getFn(c.hostLoggerPtr, unsafe.SliceData(bufs))) {
		return nil
	}
	out := envoyBufferSliceToUnsafeEnvoyBufferSlice(bufs)
	runtime.KeepAlive(bufs)
	return out
}

func (c *dymAccessLogContext) GetUpstreamPeerURISans() []shared.UnsafeEnvoyBuffer {
	return c.sansViaSize(
		func(p C.envoy_dynamic_module_type_access_logger_envoy_ptr) C.size_t {
			return C.envoy_dynamic_module_callback_access_logger_get_upstream_peer_uri_san_size(p)
		},
		func(p C.envoy_dynamic_module_type_access_logger_envoy_ptr, b *C.envoy_dynamic_module_type_envoy_buffer) C.bool {
			return C.envoy_dynamic_module_callback_access_logger_get_upstream_peer_uri_san(p, b)
		},
	)
}

func (c *dymAccessLogContext) GetUpstreamLocalURISans() []shared.UnsafeEnvoyBuffer {
	return c.sansViaSize(
		func(p C.envoy_dynamic_module_type_access_logger_envoy_ptr) C.size_t {
			return C.envoy_dynamic_module_callback_access_logger_get_upstream_local_uri_san_size(p)
		},
		func(p C.envoy_dynamic_module_type_access_logger_envoy_ptr, b *C.envoy_dynamic_module_type_envoy_buffer) C.bool {
			return C.envoy_dynamic_module_callback_access_logger_get_upstream_local_uri_san(p, b)
		},
	)
}

func (c *dymAccessLogContext) GetUpstreamPeerDNSSans() []shared.UnsafeEnvoyBuffer {
	return c.sansViaSize(
		func(p C.envoy_dynamic_module_type_access_logger_envoy_ptr) C.size_t {
			return C.envoy_dynamic_module_callback_access_logger_get_upstream_peer_dns_san_size(p)
		},
		func(p C.envoy_dynamic_module_type_access_logger_envoy_ptr, b *C.envoy_dynamic_module_type_envoy_buffer) C.bool {
			return C.envoy_dynamic_module_callback_access_logger_get_upstream_peer_dns_san(p, b)
		},
	)
}

func (c *dymAccessLogContext) GetUpstreamLocalDNSSans() []shared.UnsafeEnvoyBuffer {
	return c.sansViaSize(
		func(p C.envoy_dynamic_module_type_access_logger_envoy_ptr) C.size_t {
			return C.envoy_dynamic_module_callback_access_logger_get_upstream_local_dns_san_size(p)
		},
		func(p C.envoy_dynamic_module_type_access_logger_envoy_ptr, b *C.envoy_dynamic_module_type_envoy_buffer) C.bool {
			return C.envoy_dynamic_module_callback_access_logger_get_upstream_local_dns_san(p, b)
		},
	)
}

// ---- downstream connection / TLS info ----

func (c *dymAccessLogContext) GetDownstreamTLSCipher() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_downstream_tls_cipher(c.hostLoggerPtr, &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetDownstreamTLSSessionID() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_downstream_tls_session_id(c.hostLoggerPtr, &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetDownstreamPeerIssuer() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_downstream_peer_issuer(c.hostLoggerPtr, &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetDownstreamPeerSerial() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_downstream_peer_serial(c.hostLoggerPtr, &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetDownstreamPeerFingerprint1() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_downstream_peer_fingerprint_1(c.hostLoggerPtr, &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetDownstreamPeerCertPresented() bool {
	return bool(C.envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_presented(c.hostLoggerPtr))
}

func (c *dymAccessLogContext) GetDownstreamPeerCertValidated() bool {
	return bool(C.envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_validated(c.hostLoggerPtr))
}

func (c *dymAccessLogContext) GetDownstreamPeerCertValidityStart() int64 {
	return int64(C.envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_v_start(c.hostLoggerPtr))
}

func (c *dymAccessLogContext) GetDownstreamPeerCertValidityEnd() int64 {
	return int64(C.envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_v_end(c.hostLoggerPtr))
}

func (c *dymAccessLogContext) GetDownstreamPeerURISans() []shared.UnsafeEnvoyBuffer {
	return c.sansViaSize(
		func(p C.envoy_dynamic_module_type_access_logger_envoy_ptr) C.size_t {
			return C.envoy_dynamic_module_callback_access_logger_get_downstream_peer_uri_san_size(p)
		},
		func(p C.envoy_dynamic_module_type_access_logger_envoy_ptr, b *C.envoy_dynamic_module_type_envoy_buffer) C.bool {
			return C.envoy_dynamic_module_callback_access_logger_get_downstream_peer_uri_san(p, b)
		},
	)
}

func (c *dymAccessLogContext) GetDownstreamLocalURISans() []shared.UnsafeEnvoyBuffer {
	return c.sansViaSize(
		func(p C.envoy_dynamic_module_type_access_logger_envoy_ptr) C.size_t {
			return C.envoy_dynamic_module_callback_access_logger_get_downstream_local_uri_san_size(p)
		},
		func(p C.envoy_dynamic_module_type_access_logger_envoy_ptr, b *C.envoy_dynamic_module_type_envoy_buffer) C.bool {
			return C.envoy_dynamic_module_callback_access_logger_get_downstream_local_uri_san(p, b)
		},
	)
}

func (c *dymAccessLogContext) GetDownstreamPeerDNSSans() []shared.UnsafeEnvoyBuffer {
	return c.sansViaSize(
		func(p C.envoy_dynamic_module_type_access_logger_envoy_ptr) C.size_t {
			return C.envoy_dynamic_module_callback_access_logger_get_downstream_peer_dns_san_size(p)
		},
		func(p C.envoy_dynamic_module_type_access_logger_envoy_ptr, b *C.envoy_dynamic_module_type_envoy_buffer) C.bool {
			return C.envoy_dynamic_module_callback_access_logger_get_downstream_peer_dns_san(p, b)
		},
	)
}

func (c *dymAccessLogContext) GetDownstreamLocalDNSSans() []shared.UnsafeEnvoyBuffer {
	return c.sansViaSize(
		func(p C.envoy_dynamic_module_type_access_logger_envoy_ptr) C.size_t {
			return C.envoy_dynamic_module_callback_access_logger_get_downstream_local_dns_san_size(p)
		},
		func(p C.envoy_dynamic_module_type_access_logger_envoy_ptr, b *C.envoy_dynamic_module_type_envoy_buffer) C.bool {
			return C.envoy_dynamic_module_callback_access_logger_get_downstream_local_dns_san(p, b)
		},
	)
}

// ---- metadata / filter state / tracing ----

func (c *dymAccessLogContext) GetDynamicMetadata(filterName, path string) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_dynamic_metadata(
		c.hostLoggerPtr, stringToModuleBuffer(filterName), stringToModuleBuffer(path), &buf)
	runtime.KeepAlive(filterName)
	runtime.KeepAlive(path)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetFilterState(key string) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_filter_state(
		c.hostLoggerPtr, stringToModuleBuffer(key), &buf)
	runtime.KeepAlive(key)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetLocalReplyBody() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_local_reply_body(c.hostLoggerPtr, &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetTraceID() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_trace_id(c.hostLoggerPtr, &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetSpanID() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_span_id(c.hostLoggerPtr, &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) IsTraceSampled() bool {
	return bool(C.envoy_dynamic_module_callback_access_logger_is_trace_sampled(c.hostLoggerPtr))
}

// ---- additional stream info ----

func (c *dymAccessLogContext) GetJA3Hash() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_ja3_hash(c.hostLoggerPtr, &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetJA4Hash() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_ja4_hash(c.hostLoggerPtr, &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetRequestHeadersBytes() uint64 {
	return uint64(C.envoy_dynamic_module_callback_access_logger_get_request_headers_bytes(c.hostLoggerPtr))
}

func (c *dymAccessLogContext) GetResponseHeadersBytes() uint64 {
	return uint64(C.envoy_dynamic_module_callback_access_logger_get_response_headers_bytes(c.hostLoggerPtr))
}

func (c *dymAccessLogContext) GetResponseTrailersBytes() uint64 {
	return uint64(C.envoy_dynamic_module_callback_access_logger_get_response_trailers_bytes(c.hostLoggerPtr))
}

func (c *dymAccessLogContext) GetUpstreamProtocol() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_access_logger_get_upstream_protocol(c.hostLoggerPtr, &buf)
	return accessLogStrResult(buf, ok)
}

func (c *dymAccessLogContext) GetUpstreamPoolReadyDurationNs() int64 {
	return int64(C.envoy_dynamic_module_callback_access_logger_get_upstream_pool_ready_duration_ns(c.hostLoggerPtr))
}

func (c *dymAccessLogContext) GetWorkerIndex() uint32 {
	return uint32(C.envoy_dynamic_module_callback_access_logger_get_worker_index(c.hostLoggerPtr))
}

// =============================================================================
// Event hooks
// =============================================================================

//export envoy_dynamic_module_on_access_logger_config_new
func envoy_dynamic_module_on_access_logger_config_new(
	hostConfigPtr C.envoy_dynamic_module_type_access_logger_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_access_logger_config_module_ptr {
	nameStr := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesCopy(config)

	configHandle := &dymAccessLoggerConfigHandle{hostConfigPtr: hostConfigPtr}
	configFactory := sdk.GetAccessLoggerConfigFactory(nameStr)
	if configFactory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load access logger configuration for %q: no factory registered", []any{nameStr})
		return nil
	}
	factory, err := configFactory.Create(configHandle, configBytes)
	if err != nil {
		hostLog(shared.LogLevelWarn, "Failed to load access logger configuration for %q: %v", []any{nameStr, err})
		return nil
	}
	if factory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load access logger configuration for %q: factory returned nil", []any{nameStr})
		return nil
	}
	wrapper := &accessLoggerConfigWrapper{factory: factory, configHandle: configHandle}
	configPtr := accessLoggerConfigManager.record(wrapper)
	return C.envoy_dynamic_module_type_access_logger_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_access_logger_config_destroy
func envoy_dynamic_module_on_access_logger_config_destroy(
	configPtr C.envoy_dynamic_module_type_access_logger_config_module_ptr,
) {
	wrapper := accessLoggerConfigManager.unwrap(unsafe.Pointer(configPtr))
	if wrapper == nil || wrapper.destroyed {
		return
	}
	wrapper.destroyed = true
	wrapper.factory.OnDestroy()
	accessLoggerConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_access_logger_new
func envoy_dynamic_module_on_access_logger_new(
	configPtr C.envoy_dynamic_module_type_access_logger_config_module_ptr,
	_ C.envoy_dynamic_module_type_access_logger_envoy_ptr,
) C.envoy_dynamic_module_type_access_logger_module_ptr {
	cfg := accessLoggerConfigManager.unwrap(unsafe.Pointer(configPtr))
	if cfg == nil || cfg.destroyed {
		return nil
	}
	logger := cfg.factory.Create()
	if logger == nil {
		return nil
	}
	wrapper := &accessLoggerWrapper{logger: logger}
	loggerPtr := accessLoggerManager.record(wrapper)
	return C.envoy_dynamic_module_type_access_logger_module_ptr(loggerPtr)
}

//export envoy_dynamic_module_on_access_logger_log
func envoy_dynamic_module_on_access_logger_log(
	hostLoggerPtr C.envoy_dynamic_module_type_access_logger_envoy_ptr,
	loggerPtr C.envoy_dynamic_module_type_access_logger_module_ptr,
	logType C.envoy_dynamic_module_type_access_log_type,
) {
	w := accessLoggerManager.unwrap(unsafe.Pointer(loggerPtr))
	if w == nil || w.logger == nil || w.destroyed {
		return
	}
	ctx := &dymAccessLogContext{hostLoggerPtr: hostLoggerPtr}
	w.logger.Log(ctx, shared.AccessLogType(logType))
}

//export envoy_dynamic_module_on_access_logger_destroy
func envoy_dynamic_module_on_access_logger_destroy(
	loggerPtr C.envoy_dynamic_module_type_access_logger_module_ptr,
) {
	w := accessLoggerManager.unwrap(unsafe.Pointer(loggerPtr))
	if w == nil || w.destroyed {
		return
	}
	w.destroyed = true
	if w.logger != nil {
		w.logger.OnDestroy()
	}
	accessLoggerManager.remove(unsafe.Pointer(loggerPtr))
}

//export envoy_dynamic_module_on_access_logger_flush
func envoy_dynamic_module_on_access_logger_flush(
	loggerPtr C.envoy_dynamic_module_type_access_logger_module_ptr,
) {
	w := accessLoggerManager.unwrap(unsafe.Pointer(loggerPtr))
	if w == nil || w.logger == nil || w.destroyed {
		return
	}
	w.logger.Flush()
}
