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
	"sync"
	"unsafe"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

// networkFilterConfigWrapper is the module-side object that backs an Envoy
// DynamicModuleNetworkFilterConfig. We keep the wrapper type so the manager pointer remains stable
// across Go GC moves.
type networkFilterConfigWrapper struct {
	factory      shared.NetworkFilterFactory
	configHandle *dymNetworkConfigHandle
}

type networkFilterWrapper = dymNetworkFilterHandle

var networkConfigManager = newManager[networkFilterConfigWrapper]()
var networkFilterManager = newManager[networkFilterWrapper]()

// dymNetworkConfigHandle implements shared.NetworkFilterConfigHandle.
type dymNetworkConfigHandle struct {
	hostConfigPtr C.envoy_dynamic_module_type_network_filter_config_envoy_ptr
	scheduler     *dymScheduler
}

func (h *dymNetworkConfigHandle) DefineCounter(name string) (shared.MetricID, shared.MetricsResult) {
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_network_filter_config_define_counter(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		&id,
	)
	runtime.KeepAlive(name)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymNetworkConfigHandle) DefineGauge(name string) (shared.MetricID, shared.MetricsResult) {
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_network_filter_config_define_gauge(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		&id,
	)
	runtime.KeepAlive(name)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymNetworkConfigHandle) DefineHistogram(name string) (shared.MetricID, shared.MetricsResult) {
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_network_filter_config_define_histogram(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		&id,
	)
	runtime.KeepAlive(name)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymNetworkConfigHandle) GetScheduler() shared.Scheduler {
	if h.scheduler == nil {
		schedulerPtr := C.envoy_dynamic_module_callback_network_filter_config_scheduler_new(
			h.hostConfigPtr)
		h.scheduler = newDymScheduler(
			unsafe.Pointer(schedulerPtr),
			func(p unsafe.Pointer, taskID C.uint64_t) {
				C.envoy_dynamic_module_callback_network_filter_config_scheduler_commit(
					(C.envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr)(p),
					taskID,
				)
			},
		)
		runtime.SetFinalizer(h.scheduler, func(s *dymScheduler) {
			C.envoy_dynamic_module_callback_network_filter_config_scheduler_delete(
				(C.envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr)(s.schedulerPtr),
			)
		})
	}
	return h.scheduler
}

// dymNetworkFilterHandle implements shared.NetworkFilterHandle.
type dymNetworkFilterHandle struct {
	hostFilterPtr C.envoy_dynamic_module_type_network_filter_envoy_ptr

	plugin    shared.NetworkFilter
	scheduler *dymScheduler
	destroyed bool

	calloutCallbacks map[uint64]shared.HttpCalloutCallback
	calloutMu        sync.Mutex
}

// ---- read/write buffers ----

func (h *dymNetworkFilterHandle) GetReadBufferChunks() []shared.UnsafeEnvoyBuffer {
	size := C.envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size(h.hostFilterPtr)
	if size == 0 {
		return nil
	}
	bufs := make([]C.envoy_dynamic_module_type_envoy_buffer, int(size))
	if !bool(C.envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks(
		h.hostFilterPtr, unsafe.SliceData(bufs))) {
		return nil
	}
	out := envoyBufferSliceToUnsafeEnvoyBufferSlice(bufs)
	runtime.KeepAlive(bufs)
	return out
}

func (h *dymNetworkFilterHandle) GetReadBufferSize() uint64 {
	return uint64(C.envoy_dynamic_module_callback_network_filter_get_read_buffer_size(h.hostFilterPtr))
}

func (h *dymNetworkFilterHandle) GetWriteBufferChunks() []shared.UnsafeEnvoyBuffer {
	size := C.envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size(h.hostFilterPtr)
	if size == 0 {
		return nil
	}
	bufs := make([]C.envoy_dynamic_module_type_envoy_buffer, int(size))
	if !bool(C.envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks(
		h.hostFilterPtr, unsafe.SliceData(bufs))) {
		return nil
	}
	out := envoyBufferSliceToUnsafeEnvoyBufferSlice(bufs)
	runtime.KeepAlive(bufs)
	return out
}

func (h *dymNetworkFilterHandle) GetWriteBufferSize() uint64 {
	return uint64(C.envoy_dynamic_module_callback_network_filter_get_write_buffer_size(h.hostFilterPtr))
}

func (h *dymNetworkFilterHandle) DrainReadBuffer(length uint64) bool {
	return bool(C.envoy_dynamic_module_callback_network_filter_drain_read_buffer(
		h.hostFilterPtr, C.size_t(length)))
}

func (h *dymNetworkFilterHandle) DrainWriteBuffer(length uint64) bool {
	return bool(C.envoy_dynamic_module_callback_network_filter_drain_write_buffer(
		h.hostFilterPtr, C.size_t(length)))
}

func (h *dymNetworkFilterHandle) PrependReadBuffer(data []byte) bool {
	ret := C.envoy_dynamic_module_callback_network_filter_prepend_read_buffer(
		h.hostFilterPtr, bytesToModuleBuffer(data))
	runtime.KeepAlive(data)
	return bool(ret)
}

func (h *dymNetworkFilterHandle) AppendReadBuffer(data []byte) bool {
	ret := C.envoy_dynamic_module_callback_network_filter_append_read_buffer(
		h.hostFilterPtr, bytesToModuleBuffer(data))
	runtime.KeepAlive(data)
	return bool(ret)
}

func (h *dymNetworkFilterHandle) PrependWriteBuffer(data []byte) bool {
	ret := C.envoy_dynamic_module_callback_network_filter_prepend_write_buffer(
		h.hostFilterPtr, bytesToModuleBuffer(data))
	runtime.KeepAlive(data)
	return bool(ret)
}

func (h *dymNetworkFilterHandle) AppendWriteBuffer(data []byte) bool {
	ret := C.envoy_dynamic_module_callback_network_filter_append_write_buffer(
		h.hostFilterPtr, bytesToModuleBuffer(data))
	runtime.KeepAlive(data)
	return bool(ret)
}

func (h *dymNetworkFilterHandle) Write(data []byte, endOfStream bool) {
	C.envoy_dynamic_module_callback_network_filter_write(
		h.hostFilterPtr, bytesToModuleBuffer(data), C.bool(endOfStream))
	runtime.KeepAlive(data)
}

func (h *dymNetworkFilterHandle) InjectReadData(data []byte, endOfStream bool) {
	C.envoy_dynamic_module_callback_network_filter_inject_read_data(
		h.hostFilterPtr, bytesToModuleBuffer(data), C.bool(endOfStream))
	runtime.KeepAlive(data)
}

func (h *dymNetworkFilterHandle) InjectWriteData(data []byte, endOfStream bool) {
	C.envoy_dynamic_module_callback_network_filter_inject_write_data(
		h.hostFilterPtr, bytesToModuleBuffer(data), C.bool(endOfStream))
	runtime.KeepAlive(data)
}

func (h *dymNetworkFilterHandle) ContinueReading() {
	C.envoy_dynamic_module_callback_network_filter_continue_reading(h.hostFilterPtr)
}

// ---- connection control ----

func (h *dymNetworkFilterHandle) Close(closeType shared.NetworkConnectionCloseType) {
	C.envoy_dynamic_module_callback_network_filter_close(
		h.hostFilterPtr,
		C.envoy_dynamic_module_type_network_connection_close_type(closeType),
	)
}

func (h *dymNetworkFilterHandle) CloseWithDetails(closeType shared.NetworkConnectionCloseType, details string) {
	C.envoy_dynamic_module_callback_network_filter_close_with_details(
		h.hostFilterPtr,
		C.envoy_dynamic_module_type_network_connection_close_type(closeType),
		stringToModuleBuffer(details),
	)
	runtime.KeepAlive(details)
}

func (h *dymNetworkFilterHandle) DisableClose(disabled bool) {
	C.envoy_dynamic_module_callback_network_filter_disable_close(
		h.hostFilterPtr, C.bool(disabled))
}

func (h *dymNetworkFilterHandle) GetConnectionID() uint64 {
	return uint64(C.envoy_dynamic_module_callback_network_filter_get_connection_id(h.hostFilterPtr))
}

func (h *dymNetworkFilterHandle) GetConnectionState() shared.NetworkConnectionState {
	return shared.NetworkConnectionState(
		C.envoy_dynamic_module_callback_network_filter_get_connection_state(h.hostFilterPtr))
}

func (h *dymNetworkFilterHandle) ReadDisable(disable bool) shared.NetworkReadDisableStatus {
	return shared.NetworkReadDisableStatus(
		C.envoy_dynamic_module_callback_network_filter_read_disable(
			h.hostFilterPtr, C.bool(disable)))
}

func (h *dymNetworkFilterHandle) ReadEnabled() bool {
	return bool(C.envoy_dynamic_module_callback_network_filter_read_enabled(h.hostFilterPtr))
}

func (h *dymNetworkFilterHandle) IsHalfCloseEnabled() bool {
	return bool(C.envoy_dynamic_module_callback_network_filter_is_half_close_enabled(h.hostFilterPtr))
}

func (h *dymNetworkFilterHandle) EnableHalfClose(enabled bool) {
	C.envoy_dynamic_module_callback_network_filter_enable_half_close(
		h.hostFilterPtr, C.bool(enabled))
}

func (h *dymNetworkFilterHandle) GetBufferLimit() uint32 {
	return uint32(C.envoy_dynamic_module_callback_network_filter_get_buffer_limit(h.hostFilterPtr))
}

func (h *dymNetworkFilterHandle) SetBufferLimits(limit uint32) {
	C.envoy_dynamic_module_callback_network_filter_set_buffer_limits(
		h.hostFilterPtr, C.uint32_t(limit))
}

func (h *dymNetworkFilterHandle) AboveHighWatermark() bool {
	return bool(C.envoy_dynamic_module_callback_network_filter_above_high_watermark(h.hostFilterPtr))
}

// ---- connection metadata ----

func addressOrEmpty(buf C.envoy_dynamic_module_type_envoy_buffer, port C.uint32_t, ok C.bool) (shared.UnsafeEnvoyBuffer, uint32, bool) {
	if !bool(ok) {
		return shared.UnsafeEnvoyBuffer{}, 0, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), uint32(port), true
}

func (h *dymNetworkFilterHandle) GetRemoteAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_network_filter_get_remote_address(h.hostFilterPtr, &buf, &port)
	return addressOrEmpty(buf, port, ok)
}

func (h *dymNetworkFilterHandle) GetLocalAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_network_filter_get_local_address(h.hostFilterPtr, &buf, &port)
	return addressOrEmpty(buf, port, ok)
}

func (h *dymNetworkFilterHandle) GetDirectRemoteAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_network_filter_get_direct_remote_address(h.hostFilterPtr, &buf, &port)
	return addressOrEmpty(buf, port, ok)
}

func (h *dymNetworkFilterHandle) GetRequestedServerName() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	if !bool(C.envoy_dynamic_module_callback_network_filter_get_requested_server_name(h.hostFilterPtr, &buf)) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymNetworkFilterHandle) IsSSL() bool {
	return bool(C.envoy_dynamic_module_callback_network_filter_is_ssl(h.hostFilterPtr))
}

func (h *dymNetworkFilterHandle) GetSSLURISans() []shared.UnsafeEnvoyBuffer {
	size := C.envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans_size(h.hostFilterPtr)
	if size == 0 {
		return nil
	}
	bufs := make([]C.envoy_dynamic_module_type_envoy_buffer, int(size))
	if !bool(C.envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans(h.hostFilterPtr, unsafe.SliceData(bufs))) {
		return nil
	}
	out := envoyBufferSliceToUnsafeEnvoyBufferSlice(bufs)
	runtime.KeepAlive(bufs)
	return out
}

func (h *dymNetworkFilterHandle) GetSSLDNSSans() []shared.UnsafeEnvoyBuffer {
	size := C.envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans_size(h.hostFilterPtr)
	if size == 0 {
		return nil
	}
	bufs := make([]C.envoy_dynamic_module_type_envoy_buffer, int(size))
	if !bool(C.envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans(h.hostFilterPtr, unsafe.SliceData(bufs))) {
		return nil
	}
	out := envoyBufferSliceToUnsafeEnvoyBufferSlice(bufs)
	runtime.KeepAlive(bufs)
	return out
}

func (h *dymNetworkFilterHandle) GetSSLSubject() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	if !bool(C.envoy_dynamic_module_callback_network_filter_get_ssl_subject(h.hostFilterPtr, &buf)) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

// ---- filter state ----

func (h *dymNetworkFilterHandle) SetFilterState(key string, value []byte) bool {
	ret := C.envoy_dynamic_module_callback_network_set_filter_state_bytes(
		h.hostFilterPtr, stringToModuleBuffer(key), bytesToModuleBuffer(value))
	runtime.KeepAlive(key)
	runtime.KeepAlive(value)
	return bool(ret)
}

func (h *dymNetworkFilterHandle) GetFilterState(key string) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_network_get_filter_state_bytes(
		h.hostFilterPtr, stringToModuleBuffer(key), &buf)
	runtime.KeepAlive(key)
	if !bool(ret) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ret)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymNetworkFilterHandle) SetFilterStateTyped(key string, value []byte) bool {
	ret := C.envoy_dynamic_module_callback_network_set_filter_state_typed(
		h.hostFilterPtr, stringToModuleBuffer(key), bytesToModuleBuffer(value))
	runtime.KeepAlive(key)
	runtime.KeepAlive(value)
	return bool(ret)
}

func (h *dymNetworkFilterHandle) GetFilterStateTyped(key string) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_network_get_filter_state_typed(
		h.hostFilterPtr, stringToModuleBuffer(key), &buf)
	runtime.KeepAlive(key)
	if !bool(ret) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ret)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

// ---- dynamic metadata ----

func (h *dymNetworkFilterHandle) SetDynamicMetadataString(metadataNamespace, key, value string) {
	C.envoy_dynamic_module_callback_network_set_dynamic_metadata_string(
		h.hostFilterPtr,
		stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key),
		stringToModuleBuffer(value),
	)
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
	runtime.KeepAlive(value)
}

func (h *dymNetworkFilterHandle) GetDynamicMetadataString(metadataNamespace, key string) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_network_get_dynamic_metadata_string(
		h.hostFilterPtr,
		stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key),
		&buf,
	)
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
	if !bool(ret) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ret)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymNetworkFilterHandle) SetDynamicMetadataNumber(metadataNamespace, key string, value float64) {
	C.envoy_dynamic_module_callback_network_set_dynamic_metadata_number(
		h.hostFilterPtr,
		stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key),
		C.double(value),
	)
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
}

func (h *dymNetworkFilterHandle) GetDynamicMetadataNumber(metadataNamespace, key string) (float64, bool) {
	var v C.double
	ret := C.envoy_dynamic_module_callback_network_get_dynamic_metadata_number(
		h.hostFilterPtr,
		stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key),
		&v,
	)
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
	if !bool(ret) {
		return 0, false
	}
	return float64(v), true
}

func (h *dymNetworkFilterHandle) SetDynamicMetadataBool(metadataNamespace, key string, value bool) {
	C.envoy_dynamic_module_callback_network_set_dynamic_metadata_bool(
		h.hostFilterPtr,
		stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key),
		C.bool(value),
	)
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
}

func (h *dymNetworkFilterHandle) GetDynamicMetadataBool(metadataNamespace, key string) (bool, bool) {
	var v C.bool
	ret := C.envoy_dynamic_module_callback_network_get_dynamic_metadata_bool(
		h.hostFilterPtr,
		stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key),
		&v,
	)
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
	if !bool(ret) {
		return false, false
	}
	return bool(v), true
}

// ---- socket options ----

func (h *dymNetworkFilterHandle) SetSocketOptionInt(level, name int64, state shared.SocketOptionState, value int64) {
	C.envoy_dynamic_module_callback_network_set_socket_option_int(
		h.hostFilterPtr,
		C.int64_t(level),
		C.int64_t(name),
		C.envoy_dynamic_module_type_socket_option_state(state),
		C.int64_t(value),
	)
}

func (h *dymNetworkFilterHandle) SetSocketOptionBytes(level, name int64, state shared.SocketOptionState, value []byte) {
	C.envoy_dynamic_module_callback_network_set_socket_option_bytes(
		h.hostFilterPtr,
		C.int64_t(level),
		C.int64_t(name),
		C.envoy_dynamic_module_type_socket_option_state(state),
		bytesToModuleBuffer(value),
	)
	runtime.KeepAlive(value)
}

func (h *dymNetworkFilterHandle) GetSocketOptionInt(level, name int64, state shared.SocketOptionState) (int64, bool) {
	var v C.int64_t
	ret := C.envoy_dynamic_module_callback_network_get_socket_option_int(
		h.hostFilterPtr,
		C.int64_t(level),
		C.int64_t(name),
		C.envoy_dynamic_module_type_socket_option_state(state),
		&v,
	)
	if !bool(ret) {
		return 0, false
	}
	return int64(v), true
}

func (h *dymNetworkFilterHandle) GetSocketOptionBytes(level, name int64, state shared.SocketOptionState) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_network_get_socket_option_bytes(
		h.hostFilterPtr,
		C.int64_t(level),
		C.int64_t(name),
		C.envoy_dynamic_module_type_socket_option_state(state),
		&buf,
	)
	if !bool(ret) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ret)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymNetworkFilterHandle) GetSocketOptions() []shared.SocketOption {
	size := C.envoy_dynamic_module_callback_network_get_socket_options_size(h.hostFilterPtr)
	if size == 0 {
		return nil
	}
	raw := make([]C.envoy_dynamic_module_type_socket_option, int(size))
	C.envoy_dynamic_module_callback_network_get_socket_options(h.hostFilterPtr, unsafe.SliceData(raw))
	out := make([]shared.SocketOption, int(size))
	for i := range raw {
		out[i] = shared.SocketOption{
			Level: int64(raw[i].level),
			Name:  int64(raw[i].name),
			State: shared.SocketOptionState(raw[i].state),
		}
		switch raw[i].value_type {
		case C.envoy_dynamic_module_type_socket_option_value_type_Int:
			out[i].Value = shared.SocketOptionValue{Int: int64(raw[i].int_value)}
		case C.envoy_dynamic_module_type_socket_option_value_type_Bytes:
			out[i].Value = shared.SocketOptionValue{
				IsBytes: true,
				Bytes:   envoyBufferToUnsafeEnvoyBuffer(raw[i].byte_value),
			}
		}
	}
	runtime.KeepAlive(raw)
	return out
}

// ---- HTTP callout ----

func (h *dymNetworkFilterHandle) HttpCallout(
	clusterName string, headers [][2]string, body []byte, timeoutMs uint64,
	cb shared.HttpCalloutCallback,
) (shared.HttpCalloutInitResult, uint64) {
	headerViews := headersToModuleHttpHeaderSlice(headers)
	var calloutID C.uint64_t

	result := C.envoy_dynamic_module_callback_network_filter_http_callout(
		h.hostFilterPtr,
		&calloutID,
		stringToModuleBuffer(clusterName),
		unsafe.SliceData(headerViews),
		C.size_t(len(headerViews)),
		bytesToModuleBuffer(body),
		C.uint64_t(timeoutMs),
	)
	runtime.KeepAlive(clusterName)
	runtime.KeepAlive(headers)
	runtime.KeepAlive(headerViews)
	runtime.KeepAlive(body)

	goResult := shared.HttpCalloutInitResult(result)
	if goResult != shared.HttpCalloutInitSuccess {
		return goResult, 0
	}
	h.calloutMu.Lock()
	if h.calloutCallbacks == nil {
		h.calloutCallbacks = make(map[uint64]shared.HttpCalloutCallback)
	}
	h.calloutCallbacks[uint64(calloutID)] = cb
	h.calloutMu.Unlock()
	return goResult, uint64(calloutID)
}

// ---- metrics ----

func (h *dymNetworkFilterHandle) IncrementCounter(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_network_filter_increment_counter(
		h.hostFilterPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymNetworkFilterHandle) SetGauge(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_network_filter_set_gauge(
		h.hostFilterPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymNetworkFilterHandle) IncrementGauge(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_network_filter_increment_gauge(
		h.hostFilterPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymNetworkFilterHandle) DecrementGauge(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_network_filter_decrement_gauge(
		h.hostFilterPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymNetworkFilterHandle) RecordHistogramValue(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_network_filter_record_histogram_value(
		h.hostFilterPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

// ---- upstream/cluster info ----

func (h *dymNetworkFilterHandle) GetClusterHostCount(clusterName string, priority uint32) (shared.ClusterHostCount, bool) {
	var total, healthy, degraded C.size_t
	ret := C.envoy_dynamic_module_callback_network_filter_get_cluster_host_count(
		h.hostFilterPtr,
		stringToModuleBuffer(clusterName),
		C.uint32_t(priority),
		&total, &healthy, &degraded,
	)
	runtime.KeepAlive(clusterName)
	if !bool(ret) {
		return shared.ClusterHostCount{}, false
	}
	return shared.ClusterHostCount{
		Total: uint64(total), Healthy: uint64(healthy), Degraded: uint64(degraded),
	}, true
}

func (h *dymNetworkFilterHandle) GetUpstreamHostAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_network_filter_get_upstream_host_address(h.hostFilterPtr, &buf, &port)
	return addressOrEmpty(buf, port, ok)
}

func (h *dymNetworkFilterHandle) GetUpstreamHostHostname() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	if !bool(C.envoy_dynamic_module_callback_network_filter_get_upstream_host_hostname(h.hostFilterPtr, &buf)) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymNetworkFilterHandle) GetUpstreamHostCluster() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	if !bool(C.envoy_dynamic_module_callback_network_filter_get_upstream_host_cluster(h.hostFilterPtr, &buf)) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymNetworkFilterHandle) HasUpstreamHost() bool {
	return bool(C.envoy_dynamic_module_callback_network_filter_has_upstream_host(h.hostFilterPtr))
}

func (h *dymNetworkFilterHandle) StartUpstreamSecureTransport() bool {
	return bool(C.envoy_dynamic_module_callback_network_filter_start_upstream_secure_transport(h.hostFilterPtr))
}

// ---- scheduler / misc ----

func (h *dymNetworkFilterHandle) GetScheduler() shared.Scheduler {
	if h.scheduler == nil {
		schedulerPtr := C.envoy_dynamic_module_callback_network_filter_scheduler_new(h.hostFilterPtr)
		h.scheduler = newDymScheduler(
			unsafe.Pointer(schedulerPtr),
			func(p unsafe.Pointer, taskID C.uint64_t) {
				C.envoy_dynamic_module_callback_network_filter_scheduler_commit(
					(C.envoy_dynamic_module_type_network_filter_scheduler_module_ptr)(p),
					taskID,
				)
			},
		)
		runtime.SetFinalizer(h.scheduler, func(s *dymScheduler) {
			C.envoy_dynamic_module_callback_network_filter_scheduler_delete(
				(C.envoy_dynamic_module_type_network_filter_scheduler_module_ptr)(s.schedulerPtr),
			)
		})
	}
	return h.scheduler
}

func (h *dymNetworkFilterHandle) GetWorkerIndex() uint32 {
	return uint32(C.envoy_dynamic_module_callback_network_filter_get_worker_index(h.hostFilterPtr))
}

// =============================================================================
// Event hooks (//export entry points called by Envoy through cgo)
// =============================================================================

//export envoy_dynamic_module_on_network_filter_config_new
func envoy_dynamic_module_on_network_filter_config_new(
	hostConfigPtr C.envoy_dynamic_module_type_network_filter_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_network_filter_config_module_ptr {
	nameStr := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesUnsafe(config)

	configHandle := &dymNetworkConfigHandle{hostConfigPtr: hostConfigPtr}
	configFactory := sdk.GetNetworkFilterConfigFactory(nameStr)
	if configFactory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load network filter configuration: no factory for %s", []any{nameStr})
		return nil
	}
	factory, err := configFactory.Create(configHandle, configBytes)
	if err != nil || factory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load network filter configuration: %v", []any{err})
		return nil
	}
	wrapper := &networkFilterConfigWrapper{factory: factory, configHandle: configHandle}
	configPtr := networkConfigManager.record(wrapper)
	return C.envoy_dynamic_module_type_network_filter_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_network_filter_config_destroy
func envoy_dynamic_module_on_network_filter_config_destroy(
	configPtr C.envoy_dynamic_module_type_network_filter_config_module_ptr,
) {
	wrapper := networkConfigManager.unwrap(unsafe.Pointer(configPtr))
	if wrapper == nil {
		return
	}
	wrapper.configHandle.scheduler = nil
	wrapper.factory.OnDestroy()
	networkConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_network_filter_new
func envoy_dynamic_module_on_network_filter_new(
	configPtr C.envoy_dynamic_module_type_network_filter_config_module_ptr,
	hostFilterPtr C.envoy_dynamic_module_type_network_filter_envoy_ptr,
) C.envoy_dynamic_module_type_network_filter_module_ptr {
	cfg := networkConfigManager.unwrap(unsafe.Pointer(configPtr))
	if cfg == nil {
		return nil
	}
	handle := &dymNetworkFilterHandle{hostFilterPtr: hostFilterPtr}
	handle.plugin = cfg.factory.Create(handle)
	if handle.plugin == nil {
		return nil
	}
	filterPtr := networkFilterManager.record(handle)
	return C.envoy_dynamic_module_type_network_filter_module_ptr(filterPtr)
}

//export envoy_dynamic_module_on_network_filter_new_connection
func envoy_dynamic_module_on_network_filter_new_connection(
	_ C.envoy_dynamic_module_type_network_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
) C.envoy_dynamic_module_type_on_network_filter_data_status {
	h := networkFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.plugin == nil || h.destroyed {
		return 0
	}
	return C.envoy_dynamic_module_type_on_network_filter_data_status(h.plugin.OnNewConnection(h))
}

//export envoy_dynamic_module_on_network_filter_read
func envoy_dynamic_module_on_network_filter_read(
	_ C.envoy_dynamic_module_type_network_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
	dataLength C.size_t,
	endStream C.bool,
) C.envoy_dynamic_module_type_on_network_filter_data_status {
	h := networkFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.plugin == nil || h.destroyed {
		return 0
	}
	return C.envoy_dynamic_module_type_on_network_filter_data_status(
		h.plugin.OnRead(h, uint64(dataLength), bool(endStream)))
}

//export envoy_dynamic_module_on_network_filter_write
func envoy_dynamic_module_on_network_filter_write(
	_ C.envoy_dynamic_module_type_network_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
	dataLength C.size_t,
	endStream C.bool,
) C.envoy_dynamic_module_type_on_network_filter_data_status {
	h := networkFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.plugin == nil || h.destroyed {
		return 0
	}
	return C.envoy_dynamic_module_type_on_network_filter_data_status(
		h.plugin.OnWrite(h, uint64(dataLength), bool(endStream)))
}

//export envoy_dynamic_module_on_network_filter_event
func envoy_dynamic_module_on_network_filter_event(
	_ C.envoy_dynamic_module_type_network_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
	event C.envoy_dynamic_module_type_network_connection_event,
) {
	h := networkFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.plugin == nil || h.destroyed {
		return
	}
	h.plugin.OnEvent(h, shared.NetworkConnectionEvent(event))
}

//export envoy_dynamic_module_on_network_filter_destroy
func envoy_dynamic_module_on_network_filter_destroy(
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
) {
	h := networkFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.destroyed {
		return
	}
	h.destroyed = true
	if h.plugin != nil {
		h.plugin.OnDestroy()
	}
	h.scheduler = nil
	networkFilterManager.remove(unsafe.Pointer(filterPtr))
}

//export envoy_dynamic_module_on_network_filter_http_callout_done
func envoy_dynamic_module_on_network_filter_http_callout_done(
	_ C.envoy_dynamic_module_type_network_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
	calloutID C.uint64_t,
	result C.envoy_dynamic_module_type_http_callout_result,
	headers *C.envoy_dynamic_module_type_envoy_http_header,
	headersSize C.size_t,
	chunks *C.envoy_dynamic_module_type_envoy_buffer,
	chunksSize C.size_t,
) {
	h := networkFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.destroyed {
		return
	}
	resultHeaders := envoyHttpHeaderSliceToUnsafeHeaderSlice(unsafe.Slice(headers, int(headersSize)))
	resultChunks := envoyBufferSliceToUnsafeEnvoyBufferSlice(unsafe.Slice(chunks, int(chunksSize)))

	h.calloutMu.Lock()
	cb := h.calloutCallbacks[uint64(calloutID)]
	delete(h.calloutCallbacks, uint64(calloutID))
	h.calloutMu.Unlock()
	if cb != nil {
		cb.OnHttpCalloutDone(uint64(calloutID), shared.HttpCalloutResult(result), resultHeaders, resultChunks)
	}
}

//export envoy_dynamic_module_on_network_filter_scheduled
func envoy_dynamic_module_on_network_filter_scheduled(
	_ C.envoy_dynamic_module_type_network_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
	eventID C.uint64_t,
) {
	h := networkFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.destroyed || h.scheduler == nil {
		return
	}
	h.scheduler.onScheduled(uint64(eventID))
}

//export envoy_dynamic_module_on_network_filter_config_scheduled
func envoy_dynamic_module_on_network_filter_config_scheduled(
	configPtr C.envoy_dynamic_module_type_network_filter_config_module_ptr,
	eventID C.uint64_t,
) {
	cfg := networkConfigManager.unwrap(unsafe.Pointer(configPtr))
	if cfg == nil || cfg.configHandle == nil || cfg.configHandle.scheduler == nil {
		return
	}
	cfg.configHandle.scheduler.onScheduled(uint64(eventID))
}

//export envoy_dynamic_module_on_network_filter_above_write_buffer_high_watermark
func envoy_dynamic_module_on_network_filter_above_write_buffer_high_watermark(
	_ C.envoy_dynamic_module_type_network_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
) {
	h := networkFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.plugin == nil || h.destroyed {
		return
	}
	h.plugin.OnAboveWriteBufferHighWatermark(h)
}

//export envoy_dynamic_module_on_network_filter_below_write_buffer_low_watermark
func envoy_dynamic_module_on_network_filter_below_write_buffer_low_watermark(
	_ C.envoy_dynamic_module_type_network_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
) {
	h := networkFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.plugin == nil || h.destroyed {
		return
	}
	h.plugin.OnBelowWriteBufferLowWatermark(h)
}
