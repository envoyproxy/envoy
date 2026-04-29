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

// listenerFilterConfigWrapper holds the module-side state for a listener filter configuration.
type listenerFilterConfigWrapper struct {
	factory      shared.ListenerFilterFactory
	configHandle *dymListenerConfigHandle
}

type listenerFilterWrapper = dymListenerFilterHandle

var listenerConfigManager = newManager[listenerFilterConfigWrapper]()
var listenerFilterManager = newManager[listenerFilterWrapper]()

// dymListenerConfigHandle implements shared.ListenerFilterConfigHandle.
type dymListenerConfigHandle struct {
	hostConfigPtr C.envoy_dynamic_module_type_listener_filter_config_envoy_ptr
	scheduler     *dymScheduler
}

func (h *dymListenerConfigHandle) DefineCounter(name string) (shared.MetricID, shared.MetricsResult) {
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_listener_filter_config_define_counter(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		&id,
	)
	runtime.KeepAlive(name)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymListenerConfigHandle) DefineGauge(name string) (shared.MetricID, shared.MetricsResult) {
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_listener_filter_config_define_gauge(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		&id,
	)
	runtime.KeepAlive(name)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymListenerConfigHandle) DefineHistogram(name string) (shared.MetricID, shared.MetricsResult) {
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_listener_filter_config_define_histogram(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		&id,
	)
	runtime.KeepAlive(name)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymListenerConfigHandle) GetScheduler() shared.Scheduler {
	if h.scheduler == nil {
		schedulerPtr := C.envoy_dynamic_module_callback_listener_filter_config_scheduler_new(
			h.hostConfigPtr)
		h.scheduler = newDymScheduler(
			unsafe.Pointer(schedulerPtr),
			func(p unsafe.Pointer, taskID C.uint64_t) {
				C.envoy_dynamic_module_callback_listener_filter_config_scheduler_commit(
					(C.envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr)(p),
					taskID,
				)
			},
		)
		runtime.SetFinalizer(h.scheduler, func(s *dymScheduler) {
			C.envoy_dynamic_module_callback_listener_filter_config_scheduler_delete(
				(C.envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr)(s.schedulerPtr),
			)
		})
	}
	return h.scheduler
}

// dymListenerFilterHandle implements shared.ListenerFilterHandle.
type dymListenerFilterHandle struct {
	hostFilterPtr C.envoy_dynamic_module_type_listener_filter_envoy_ptr

	plugin    shared.ListenerFilter
	scheduler *dymScheduler
	destroyed bool

	calloutCallbacks map[uint64]shared.HttpCalloutCallback
	calloutMu        sync.Mutex
}

// ---- buffer access ----

func (h *dymListenerFilterHandle) GetBufferChunk() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_listener_filter_get_buffer_chunk(h.hostFilterPtr, &buf)
	if !bool(ret) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ret)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymListenerFilterHandle) DrainBuffer(length uint64) bool {
	return bool(C.envoy_dynamic_module_callback_listener_filter_drain_buffer(
		h.hostFilterPtr, C.size_t(length)))
}

// ---- protocol detection setters ----

func (h *dymListenerFilterHandle) SetDetectedTransportProtocol(protocol string) {
	C.envoy_dynamic_module_callback_listener_filter_set_detected_transport_protocol(
		h.hostFilterPtr, stringToModuleBuffer(protocol))
	runtime.KeepAlive(protocol)
}

func (h *dymListenerFilterHandle) SetRequestedServerName(name string) {
	C.envoy_dynamic_module_callback_listener_filter_set_requested_server_name(
		h.hostFilterPtr, stringToModuleBuffer(name))
	runtime.KeepAlive(name)
}

func (h *dymListenerFilterHandle) SetRequestedApplicationProtocols(protocols []string) {
	if len(protocols) == 0 {
		C.envoy_dynamic_module_callback_listener_filter_set_requested_application_protocols(
			h.hostFilterPtr, nil, 0)
		return
	}
	views := stringArrayToModuleBufferSlice(protocols)
	C.envoy_dynamic_module_callback_listener_filter_set_requested_application_protocols(
		h.hostFilterPtr, unsafe.SliceData(views), C.size_t(len(views)))
	runtime.KeepAlive(protocols)
	runtime.KeepAlive(views)
}

func (h *dymListenerFilterHandle) SetJa3Hash(hash string) {
	C.envoy_dynamic_module_callback_listener_filter_set_ja3_hash(
		h.hostFilterPtr, stringToModuleBuffer(hash))
	runtime.KeepAlive(hash)
}

func (h *dymListenerFilterHandle) SetJa4Hash(hash string) {
	C.envoy_dynamic_module_callback_listener_filter_set_ja4_hash(
		h.hostFilterPtr, stringToModuleBuffer(hash))
	runtime.KeepAlive(hash)
}

// ---- protocol detection getters / SSL info ----

func (h *dymListenerFilterHandle) GetRequestedServerName() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	if !bool(C.envoy_dynamic_module_callback_listener_filter_get_requested_server_name(h.hostFilterPtr, &buf)) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymListenerFilterHandle) GetDetectedTransportProtocol() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	if !bool(C.envoy_dynamic_module_callback_listener_filter_get_detected_transport_protocol(h.hostFilterPtr, &buf)) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymListenerFilterHandle) GetRequestedApplicationProtocols() []shared.UnsafeEnvoyBuffer {
	size := C.envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols_size(h.hostFilterPtr)
	if size == 0 {
		return nil
	}
	bufs := make([]C.envoy_dynamic_module_type_envoy_buffer, int(size))
	if !bool(C.envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols(
		h.hostFilterPtr, unsafe.SliceData(bufs))) {
		return nil
	}
	out := envoyBufferSliceToUnsafeEnvoyBufferSlice(bufs)
	runtime.KeepAlive(bufs)
	return out
}

func (h *dymListenerFilterHandle) GetJa3Hash() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	if !bool(C.envoy_dynamic_module_callback_listener_filter_get_ja3_hash(h.hostFilterPtr, &buf)) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymListenerFilterHandle) GetJa4Hash() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	if !bool(C.envoy_dynamic_module_callback_listener_filter_get_ja4_hash(h.hostFilterPtr, &buf)) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymListenerFilterHandle) IsSSL() bool {
	return bool(C.envoy_dynamic_module_callback_listener_filter_is_ssl(h.hostFilterPtr))
}

func (h *dymListenerFilterHandle) GetSSLURISans() []shared.UnsafeEnvoyBuffer {
	size := C.envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans_size(h.hostFilterPtr)
	if size == 0 {
		return nil
	}
	bufs := make([]C.envoy_dynamic_module_type_envoy_buffer, int(size))
	if !bool(C.envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans(h.hostFilterPtr, unsafe.SliceData(bufs))) {
		return nil
	}
	out := envoyBufferSliceToUnsafeEnvoyBufferSlice(bufs)
	runtime.KeepAlive(bufs)
	return out
}

func (h *dymListenerFilterHandle) GetSSLDNSSans() []shared.UnsafeEnvoyBuffer {
	size := C.envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans_size(h.hostFilterPtr)
	if size == 0 {
		return nil
	}
	bufs := make([]C.envoy_dynamic_module_type_envoy_buffer, int(size))
	if !bool(C.envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans(h.hostFilterPtr, unsafe.SliceData(bufs))) {
		return nil
	}
	out := envoyBufferSliceToUnsafeEnvoyBufferSlice(bufs)
	runtime.KeepAlive(bufs)
	return out
}

func (h *dymListenerFilterHandle) GetSSLSubject() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	if !bool(C.envoy_dynamic_module_callback_listener_filter_get_ssl_subject(h.hostFilterPtr, &buf)) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

// ---- addresses ----

func listenerAddressOrEmpty(buf C.envoy_dynamic_module_type_envoy_buffer, port C.uint32_t, ok C.bool) (shared.UnsafeEnvoyBuffer, uint32, bool) {
	if !bool(ok) {
		return shared.UnsafeEnvoyBuffer{}, 0, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), uint32(port), true
}

func (h *dymListenerFilterHandle) GetRemoteAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_listener_filter_get_remote_address(h.hostFilterPtr, &buf, &port)
	return listenerAddressOrEmpty(buf, port, ok)
}

func (h *dymListenerFilterHandle) GetDirectRemoteAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_listener_filter_get_direct_remote_address(h.hostFilterPtr, &buf, &port)
	return listenerAddressOrEmpty(buf, port, ok)
}

func (h *dymListenerFilterHandle) GetLocalAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_listener_filter_get_local_address(h.hostFilterPtr, &buf, &port)
	return listenerAddressOrEmpty(buf, port, ok)
}

func (h *dymListenerFilterHandle) GetDirectLocalAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_listener_filter_get_direct_local_address(h.hostFilterPtr, &buf, &port)
	return listenerAddressOrEmpty(buf, port, ok)
}

func (h *dymListenerFilterHandle) GetOriginalDst() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_listener_filter_get_original_dst(h.hostFilterPtr, &buf, &port)
	return listenerAddressOrEmpty(buf, port, ok)
}

func (h *dymListenerFilterHandle) GetAddressType() shared.AddressType {
	return shared.AddressType(C.envoy_dynamic_module_callback_listener_filter_get_address_type(h.hostFilterPtr))
}

func (h *dymListenerFilterHandle) IsLocalAddressRestored() bool {
	return bool(C.envoy_dynamic_module_callback_listener_filter_is_local_address_restored(h.hostFilterPtr))
}

func (h *dymListenerFilterHandle) SetRemoteAddress(address string, port uint32, isIPv6 bool) bool {
	ret := C.envoy_dynamic_module_callback_listener_filter_set_remote_address(
		h.hostFilterPtr, stringToModuleBuffer(address), C.uint32_t(port), C.bool(isIPv6))
	runtime.KeepAlive(address)
	return bool(ret)
}

func (h *dymListenerFilterHandle) RestoreLocalAddress(address string, port uint32, isIPv6 bool) bool {
	ret := C.envoy_dynamic_module_callback_listener_filter_restore_local_address(
		h.hostFilterPtr, stringToModuleBuffer(address), C.uint32_t(port), C.bool(isIPv6))
	runtime.KeepAlive(address)
	return bool(ret)
}

// ---- filter chain control ----

func (h *dymListenerFilterHandle) ContinueFilterChain(success bool) {
	C.envoy_dynamic_module_callback_listener_filter_continue_filter_chain(h.hostFilterPtr, C.bool(success))
}

func (h *dymListenerFilterHandle) UseOriginalDst(useOriginalDst bool) {
	C.envoy_dynamic_module_callback_listener_filter_use_original_dst(h.hostFilterPtr, C.bool(useOriginalDst))
}

func (h *dymListenerFilterHandle) CloseSocket(details string) {
	C.envoy_dynamic_module_callback_listener_filter_close_socket(h.hostFilterPtr, stringToModuleBuffer(details))
	runtime.KeepAlive(details)
}

func (h *dymListenerFilterHandle) WriteToSocket(data []byte) int64 {
	ret := C.envoy_dynamic_module_callback_listener_filter_write_to_socket(
		h.hostFilterPtr, bytesToModuleBuffer(data))
	runtime.KeepAlive(data)
	return int64(ret)
}

// ---- socket fd / options ----

func (h *dymListenerFilterHandle) GetSocketFD() int64 {
	return int64(C.envoy_dynamic_module_callback_listener_filter_get_socket_fd(h.hostFilterPtr))
}

func (h *dymListenerFilterHandle) SetSocketOptionInt(level, name, value int64) bool {
	return bool(C.envoy_dynamic_module_callback_listener_filter_set_socket_option_int(
		h.hostFilterPtr, C.int64_t(level), C.int64_t(name), C.int64_t(value)))
}

func (h *dymListenerFilterHandle) SetSocketOptionBytes(level, name int64, value []byte) bool {
	ret := C.envoy_dynamic_module_callback_listener_filter_set_socket_option_bytes(
		h.hostFilterPtr, C.int64_t(level), C.int64_t(name), bytesToModuleBuffer(value))
	runtime.KeepAlive(value)
	return bool(ret)
}

func (h *dymListenerFilterHandle) GetSocketOptionInt(level, name int64) (int64, bool) {
	var v C.int64_t
	ret := C.envoy_dynamic_module_callback_listener_filter_get_socket_option_int(
		h.hostFilterPtr, C.int64_t(level), C.int64_t(name), &v)
	if !bool(ret) {
		return 0, false
	}
	return int64(v), true
}

func (h *dymListenerFilterHandle) GetSocketOptionBytes(level, name int64, maxSize uint64) ([]byte, bool) {
	if maxSize == 0 {
		return nil, false
	}
	buf := make([]byte, int(maxSize))
	var actual C.size_t
	ret := C.envoy_dynamic_module_callback_listener_filter_get_socket_option_bytes(
		h.hostFilterPtr, C.int64_t(level), C.int64_t(name),
		(*C.char)(unsafe.Pointer(unsafe.SliceData(buf))), C.size_t(maxSize), &actual)
	if !bool(ret) {
		return nil, false
	}
	if uint64(actual) < uint64(len(buf)) {
		buf = buf[:int(actual)]
	}
	return buf, true
}

// ---- filter state & dynamic metadata ----

func (h *dymListenerFilterHandle) SetFilterState(key, value string) bool {
	ret := C.envoy_dynamic_module_callback_listener_filter_set_filter_state(
		h.hostFilterPtr, stringToModuleBuffer(key), stringToModuleBuffer(value))
	runtime.KeepAlive(key)
	runtime.KeepAlive(value)
	return bool(ret)
}

func (h *dymListenerFilterHandle) GetFilterState(key string) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_listener_filter_get_filter_state(
		h.hostFilterPtr, stringToModuleBuffer(key), &buf)
	runtime.KeepAlive(key)
	if !bool(ret) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ret)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymListenerFilterHandle) SetDynamicMetadataString(metadataNamespace, key, value string) {
	C.envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string(
		h.hostFilterPtr, stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key), stringToModuleBuffer(value))
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
	runtime.KeepAlive(value)
}

func (h *dymListenerFilterHandle) GetDynamicMetadataString(metadataNamespace, key string) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_string(
		h.hostFilterPtr, stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key), &buf)
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
	if !bool(ret) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ret)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymListenerFilterHandle) SetDynamicMetadataNumber(metadataNamespace, key string, value float64) {
	C.envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_number(
		h.hostFilterPtr, stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key), C.double(value))
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
}

func (h *dymListenerFilterHandle) GetDynamicMetadataNumber(metadataNamespace, key string) (float64, bool) {
	var v C.double
	ret := C.envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_number(
		h.hostFilterPtr, stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key), &v)
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
	if !bool(ret) {
		return 0, false
	}
	return float64(v), true
}

// ---- stream info ----

func (h *dymListenerFilterHandle) SetDownstreamTransportFailureReason(reason string) {
	C.envoy_dynamic_module_callback_listener_filter_set_downstream_transport_failure_reason(
		h.hostFilterPtr, stringToModuleBuffer(reason))
	runtime.KeepAlive(reason)
}

func (h *dymListenerFilterHandle) GetConnectionStartTimeMs() uint64 {
	return uint64(C.envoy_dynamic_module_callback_listener_filter_get_connection_start_time_ms(h.hostFilterPtr))
}

func (h *dymListenerFilterHandle) MaxReadBytes() uint64 {
	return uint64(C.envoy_dynamic_module_callback_listener_filter_max_read_bytes(h.hostFilterPtr))
}

// ---- HTTP callout ----

func (h *dymListenerFilterHandle) HttpCallout(
	clusterName string, headers [][2]string, body []byte, timeoutMs uint64,
	cb shared.HttpCalloutCallback,
) (shared.HttpCalloutInitResult, uint64) {
	headerViews := headersToModuleHttpHeaderSlice(headers)
	var calloutID C.uint64_t

	result := C.envoy_dynamic_module_callback_listener_filter_http_callout(
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

func (h *dymListenerFilterHandle) IncrementCounter(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_listener_filter_increment_counter(
		h.hostFilterPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymListenerFilterHandle) SetGauge(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_listener_filter_set_gauge(
		h.hostFilterPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymListenerFilterHandle) IncrementGauge(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_listener_filter_increment_gauge(
		h.hostFilterPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymListenerFilterHandle) DecrementGauge(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_listener_filter_decrement_gauge(
		h.hostFilterPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymListenerFilterHandle) RecordHistogramValue(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_listener_filter_record_histogram_value(
		h.hostFilterPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

// ---- scheduling / misc ----

func (h *dymListenerFilterHandle) GetScheduler() shared.Scheduler {
	if h.scheduler == nil {
		schedulerPtr := C.envoy_dynamic_module_callback_listener_filter_scheduler_new(h.hostFilterPtr)
		h.scheduler = newDymScheduler(
			unsafe.Pointer(schedulerPtr),
			func(p unsafe.Pointer, taskID C.uint64_t) {
				C.envoy_dynamic_module_callback_listener_filter_scheduler_commit(
					(C.envoy_dynamic_module_type_listener_filter_scheduler_module_ptr)(p),
					taskID,
				)
			},
		)
		runtime.SetFinalizer(h.scheduler, func(s *dymScheduler) {
			C.envoy_dynamic_module_callback_listener_filter_scheduler_delete(
				(C.envoy_dynamic_module_type_listener_filter_scheduler_module_ptr)(s.schedulerPtr),
			)
		})
	}
	return h.scheduler
}

func (h *dymListenerFilterHandle) GetWorkerIndex() uint32 {
	return uint32(C.envoy_dynamic_module_callback_listener_filter_get_worker_index(h.hostFilterPtr))
}

// =============================================================================
// Event hooks (//export entry points called by Envoy)
// =============================================================================

//export envoy_dynamic_module_on_listener_filter_config_new
func envoy_dynamic_module_on_listener_filter_config_new(
	hostConfigPtr C.envoy_dynamic_module_type_listener_filter_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_listener_filter_config_module_ptr {
	nameStr := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesUnsafe(config)

	configHandle := &dymListenerConfigHandle{hostConfigPtr: hostConfigPtr}
	configFactory := sdk.GetListenerFilterConfigFactory(nameStr)
	if configFactory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load listener filter configuration: no factory for %s", []any{nameStr})
		return nil
	}
	factory, err := configFactory.Create(configHandle, configBytes)
	if err != nil || factory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load listener filter configuration: %v", []any{err})
		return nil
	}
	wrapper := &listenerFilterConfigWrapper{factory: factory, configHandle: configHandle}
	configPtr := listenerConfigManager.record(wrapper)
	return C.envoy_dynamic_module_type_listener_filter_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_listener_filter_config_destroy
func envoy_dynamic_module_on_listener_filter_config_destroy(
	configPtr C.envoy_dynamic_module_type_listener_filter_config_module_ptr,
) {
	wrapper := listenerConfigManager.unwrap(unsafe.Pointer(configPtr))
	if wrapper == nil {
		return
	}
	wrapper.configHandle.scheduler = nil
	wrapper.factory.OnDestroy()
	listenerConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_listener_filter_new
func envoy_dynamic_module_on_listener_filter_new(
	configPtr C.envoy_dynamic_module_type_listener_filter_config_module_ptr,
	hostFilterPtr C.envoy_dynamic_module_type_listener_filter_envoy_ptr,
) C.envoy_dynamic_module_type_listener_filter_module_ptr {
	cfg := listenerConfigManager.unwrap(unsafe.Pointer(configPtr))
	if cfg == nil {
		return nil
	}
	handle := &dymListenerFilterHandle{hostFilterPtr: hostFilterPtr}
	handle.plugin = cfg.factory.Create(handle)
	if handle.plugin == nil {
		return nil
	}
	filterPtr := listenerFilterManager.record(handle)
	return C.envoy_dynamic_module_type_listener_filter_module_ptr(filterPtr)
}

//export envoy_dynamic_module_on_listener_filter_on_accept
func envoy_dynamic_module_on_listener_filter_on_accept(
	_ C.envoy_dynamic_module_type_listener_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_listener_filter_module_ptr,
) C.envoy_dynamic_module_type_on_listener_filter_status {
	h := listenerFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.plugin == nil || h.destroyed {
		return 0
	}
	return C.envoy_dynamic_module_type_on_listener_filter_status(h.plugin.OnAccept(h))
}

//export envoy_dynamic_module_on_listener_filter_on_data
func envoy_dynamic_module_on_listener_filter_on_data(
	_ C.envoy_dynamic_module_type_listener_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_listener_filter_module_ptr,
	_ignoredDataLen C.size_t,
) C.envoy_dynamic_module_type_on_listener_filter_status {
	h := listenerFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.plugin == nil || h.destroyed {
		return 0
	}
	return C.envoy_dynamic_module_type_on_listener_filter_status(h.plugin.OnData(h))
}

//export envoy_dynamic_module_on_listener_filter_on_close
func envoy_dynamic_module_on_listener_filter_on_close(
	_ C.envoy_dynamic_module_type_listener_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_listener_filter_module_ptr,
) {
	h := listenerFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.plugin == nil || h.destroyed {
		return
	}
	h.plugin.OnClose(h)
}

//export envoy_dynamic_module_on_listener_filter_get_max_read_bytes
func envoy_dynamic_module_on_listener_filter_get_max_read_bytes(
	_ C.envoy_dynamic_module_type_listener_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_listener_filter_module_ptr,
) C.size_t {
	h := listenerFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.plugin == nil || h.destroyed {
		return 0
	}
	return C.size_t(h.plugin.GetMaxReadBytes())
}

//export envoy_dynamic_module_on_listener_filter_destroy
func envoy_dynamic_module_on_listener_filter_destroy(
	filterPtr C.envoy_dynamic_module_type_listener_filter_module_ptr,
) {
	h := listenerFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.destroyed {
		return
	}
	h.destroyed = true
	if h.plugin != nil {
		h.plugin.OnDestroy()
	}
	h.scheduler = nil
	listenerFilterManager.remove(unsafe.Pointer(filterPtr))
}

//export envoy_dynamic_module_on_listener_filter_scheduled
func envoy_dynamic_module_on_listener_filter_scheduled(
	_ C.envoy_dynamic_module_type_listener_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_listener_filter_module_ptr,
	eventID C.uint64_t,
) {
	h := listenerFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.destroyed || h.scheduler == nil {
		return
	}
	h.scheduler.onScheduled(uint64(eventID))
}

//export envoy_dynamic_module_on_listener_filter_config_scheduled
func envoy_dynamic_module_on_listener_filter_config_scheduled(
	_ C.envoy_dynamic_module_type_listener_filter_config_envoy_ptr,
	configPtr C.envoy_dynamic_module_type_listener_filter_config_module_ptr,
	eventID C.uint64_t,
) {
	cfg := listenerConfigManager.unwrap(unsafe.Pointer(configPtr))
	if cfg == nil || cfg.configHandle == nil || cfg.configHandle.scheduler == nil {
		return
	}
	cfg.configHandle.scheduler.onScheduled(uint64(eventID))
}

//export envoy_dynamic_module_on_listener_filter_http_callout_done
func envoy_dynamic_module_on_listener_filter_http_callout_done(
	_ C.envoy_dynamic_module_type_listener_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_listener_filter_module_ptr,
	calloutID C.uint64_t,
	result C.envoy_dynamic_module_type_http_callout_result,
	headers *C.envoy_dynamic_module_type_envoy_http_header,
	headersSize C.size_t,
	chunks *C.envoy_dynamic_module_type_envoy_buffer,
	chunksSize C.size_t,
) {
	h := listenerFilterManager.unwrap(unsafe.Pointer(filterPtr))
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
