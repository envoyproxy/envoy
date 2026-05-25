package abi

/*
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "../../../abi/abi.h"
*/
import "C"

import (
	"runtime"
	"unsafe"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

type listenerFilterConfigWrapper struct {
	pluginFactory shared.ListenerFilterFactory
	configHandle  *dymListenerConfigHandle
}

type listenerFilterWrapper = dymListenerFilterHandle

var listenerConfigManager = newManager[listenerFilterConfigWrapper]()
var listenerPluginManager = newManager[listenerFilterWrapper]()

type dymListenerFilterHandle struct {
	hostPluginPtr    C.envoy_dynamic_module_type_listener_filter_envoy_ptr
	plugin           shared.ListenerFilter
	scheduler        *dymScheduler
	calloutCallbacks map[uint64]shared.HttpCalloutCallback
	filterDestroyed  bool
}

type listenerStringValueKind int

const (
	listenerStringValueRequestedServerName listenerStringValueKind = iota
	listenerStringValueDetectedTransportProtocol
	listenerStringValueJA3Hash
	listenerStringValueJA4Hash
	listenerStringValueSSLSubject
)

type listenerAddressKind int

const (
	listenerAddressRemote listenerAddressKind = iota
	listenerAddressDirectRemote
	listenerAddressLocal
	listenerAddressDirectLocal
	listenerAddressOriginalDst
)

type listenerBufferSliceKind int

const (
	listenerBufferSliceRequestedApplicationProtocols listenerBufferSliceKind = iota
	listenerBufferSliceSSLURISANs
	listenerBufferSliceSSLDNSSANs
)

func newDymListenerFilterHandle(
	hostPluginPtr C.envoy_dynamic_module_type_listener_filter_envoy_ptr,
) *dymListenerFilterHandle {
	return &dymListenerFilterHandle{hostPluginPtr: hostPluginPtr}
}

func (h *dymListenerFilterHandle) GetBufferChunk() (shared.UnsafeEnvoyBuffer, bool) {
	var valueView C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_listener_filter_get_buffer_chunk(
		h.hostPluginPtr,
		&valueView,
	)
	if !bool(ret) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	if valueView.ptr == nil || valueView.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, true
	}
	return envoyBufferToUnsafeEnvoyBuffer(valueView), true
}

func (h *dymListenerFilterHandle) DrainBuffer(length uint64) bool {
	return bool(C.envoy_dynamic_module_callback_listener_filter_drain_buffer(
		h.hostPluginPtr,
		C.size_t(length),
	))
}

func (h *dymListenerFilterHandle) SetDetectedTransportProtocol(protocol string) {
	C.envoy_dynamic_module_callback_listener_filter_set_detected_transport_protocol(
		h.hostPluginPtr,
		stringToModuleBuffer(protocol),
	)
	runtime.KeepAlive(protocol)
}

func (h *dymListenerFilterHandle) SetRequestedServerName(name string) {
	C.envoy_dynamic_module_callback_listener_filter_set_requested_server_name(
		h.hostPluginPtr,
		stringToModuleBuffer(name),
	)
	runtime.KeepAlive(name)
}

func (h *dymListenerFilterHandle) SetRequestedApplicationProtocols(protocols []string) {
	bufferViews := stringArrayToModuleBufferSlice(protocols)
	C.envoy_dynamic_module_callback_listener_filter_set_requested_application_protocols(
		h.hostPluginPtr,
		unsafe.SliceData(bufferViews),
		C.size_t(len(bufferViews)),
	)
	runtime.KeepAlive(protocols)
	runtime.KeepAlive(bufferViews)
}

func (h *dymListenerFilterHandle) SetJA3Hash(hash string) {
	C.envoy_dynamic_module_callback_listener_filter_set_ja3_hash(
		h.hostPluginPtr,
		stringToModuleBuffer(hash),
	)
	runtime.KeepAlive(hash)
}

func (h *dymListenerFilterHandle) SetJA4Hash(hash string) {
	C.envoy_dynamic_module_callback_listener_filter_set_ja4_hash(
		h.hostPluginPtr,
		stringToModuleBuffer(hash),
	)
	runtime.KeepAlive(hash)
}

func (h *dymListenerFilterHandle) getStringValue(
	kind listenerStringValueKind,
) (shared.UnsafeEnvoyBuffer, bool) {
	var valueView C.envoy_dynamic_module_type_envoy_buffer
	var ret C.bool
	switch kind {
	case listenerStringValueRequestedServerName:
		ret = C.envoy_dynamic_module_callback_listener_filter_get_requested_server_name(
			h.hostPluginPtr,
			&valueView,
		)
	case listenerStringValueDetectedTransportProtocol:
		ret = C.envoy_dynamic_module_callback_listener_filter_get_detected_transport_protocol(
			h.hostPluginPtr,
			&valueView,
		)
	case listenerStringValueJA3Hash:
		ret = C.envoy_dynamic_module_callback_listener_filter_get_ja3_hash(
			h.hostPluginPtr,
			&valueView,
		)
	case listenerStringValueJA4Hash:
		ret = C.envoy_dynamic_module_callback_listener_filter_get_ja4_hash(
			h.hostPluginPtr,
			&valueView,
		)
	case listenerStringValueSSLSubject:
		ret = C.envoy_dynamic_module_callback_listener_filter_get_ssl_subject(
			h.hostPluginPtr,
			&valueView,
		)
	default:
		return shared.UnsafeEnvoyBuffer{}, false
	}
	if !bool(ret) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	if valueView.ptr == nil || valueView.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, true
	}
	return envoyBufferToUnsafeEnvoyBuffer(valueView), true
}

func (h *dymListenerFilterHandle) GetRequestedServerName() (shared.UnsafeEnvoyBuffer, bool) {
	return h.getStringValue(listenerStringValueRequestedServerName)
}

func (h *dymListenerFilterHandle) GetDetectedTransportProtocol() (shared.UnsafeEnvoyBuffer, bool) {
	return h.getStringValue(listenerStringValueDetectedTransportProtocol)
}

func (h *dymListenerFilterHandle) getBufferSlice(
	kind listenerBufferSliceKind,
) []shared.UnsafeEnvoyBuffer {
	var size C.size_t
	switch kind {
	case listenerBufferSliceRequestedApplicationProtocols:
		size = C.envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols_size(
			h.hostPluginPtr,
		)
	case listenerBufferSliceSSLURISANs:
		size = C.envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans_size(h.hostPluginPtr)
	case listenerBufferSliceSSLDNSSANs:
		size = C.envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans_size(h.hostPluginPtr)
	default:
		return nil
	}
	if size == 0 {
		return nil
	}
	result := make([]C.envoy_dynamic_module_type_envoy_buffer, size)
	var ok C.bool
	switch kind {
	case listenerBufferSliceRequestedApplicationProtocols:
		ok = C.envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols(
			h.hostPluginPtr,
			unsafe.SliceData(result),
		)
	case listenerBufferSliceSSLURISANs:
		ok = C.envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans(
			h.hostPluginPtr,
			unsafe.SliceData(result),
		)
	case listenerBufferSliceSSLDNSSANs:
		ok = C.envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans(
			h.hostPluginPtr,
			unsafe.SliceData(result),
		)
	}
	if !bool(ok) {
		return nil
	}
	finalResult := envoyBufferSliceToUnsafeEnvoyBufferSlice(result)
	runtime.KeepAlive(result)
	return finalResult
}

func (h *dymListenerFilterHandle) GetRequestedApplicationProtocols() []shared.UnsafeEnvoyBuffer {
	return h.getBufferSlice(listenerBufferSliceRequestedApplicationProtocols)
}

func (h *dymListenerFilterHandle) GetJA3Hash() (shared.UnsafeEnvoyBuffer, bool) {
	return h.getStringValue(listenerStringValueJA3Hash)
}

func (h *dymListenerFilterHandle) GetJA4Hash() (shared.UnsafeEnvoyBuffer, bool) {
	return h.getStringValue(listenerStringValueJA4Hash)
}

func (h *dymListenerFilterHandle) IsSSL() bool {
	return bool(C.envoy_dynamic_module_callback_listener_filter_is_ssl(h.hostPluginPtr))
}

func (h *dymListenerFilterHandle) GetSSLURISANs() []shared.UnsafeEnvoyBuffer {
	return h.getBufferSlice(listenerBufferSliceSSLURISANs)
}

func (h *dymListenerFilterHandle) GetSSLDNSSANs() []shared.UnsafeEnvoyBuffer {
	return h.getBufferSlice(listenerBufferSliceSSLDNSSANs)
}

func (h *dymListenerFilterHandle) GetSSLSubject() (shared.UnsafeEnvoyBuffer, bool) {
	return h.getStringValue(listenerStringValueSSLSubject)
}

func (h *dymListenerFilterHandle) getAddress(
	kind listenerAddressKind,
) (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var address C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	var ret C.bool
	switch kind {
	case listenerAddressRemote:
		ret = C.envoy_dynamic_module_callback_listener_filter_get_remote_address(
			h.hostPluginPtr,
			&address,
			&port,
		)
	case listenerAddressDirectRemote:
		ret = C.envoy_dynamic_module_callback_listener_filter_get_direct_remote_address(
			h.hostPluginPtr,
			&address,
			&port,
		)
	case listenerAddressLocal:
		ret = C.envoy_dynamic_module_callback_listener_filter_get_local_address(
			h.hostPluginPtr,
			&address,
			&port,
		)
	case listenerAddressDirectLocal:
		ret = C.envoy_dynamic_module_callback_listener_filter_get_direct_local_address(
			h.hostPluginPtr,
			&address,
			&port,
		)
	case listenerAddressOriginalDst:
		ret = C.envoy_dynamic_module_callback_listener_filter_get_original_dst(
			h.hostPluginPtr,
			&address,
			&port,
		)
	default:
		return shared.UnsafeEnvoyBuffer{}, 0, false
	}
	if !bool(ret) {
		return shared.UnsafeEnvoyBuffer{}, 0, false
	}
	if address.ptr == nil || address.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, uint32(port), true
	}
	return envoyBufferToUnsafeEnvoyBuffer(address), uint32(port), true
}

func (h *dymListenerFilterHandle) GetRemoteAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	return h.getAddress(listenerAddressRemote)
}

func (h *dymListenerFilterHandle) GetDirectRemoteAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	return h.getAddress(listenerAddressDirectRemote)
}

func (h *dymListenerFilterHandle) GetLocalAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	return h.getAddress(listenerAddressLocal)
}

func (h *dymListenerFilterHandle) GetDirectLocalAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	return h.getAddress(listenerAddressDirectLocal)
}

func (h *dymListenerFilterHandle) GetOriginalDst() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	return h.getAddress(listenerAddressOriginalDst)
}

func (h *dymListenerFilterHandle) GetAddressType() shared.ListenerAddressType {
	return shared.ListenerAddressType(
		C.envoy_dynamic_module_callback_listener_filter_get_address_type(h.hostPluginPtr),
	)
}

func (h *dymListenerFilterHandle) IsLocalAddressRestored() bool {
	return bool(C.envoy_dynamic_module_callback_listener_filter_is_local_address_restored(h.hostPluginPtr))
}

func (h *dymListenerFilterHandle) SetRemoteAddress(address string, port uint32, isIPv6 bool) bool {
	ret := C.envoy_dynamic_module_callback_listener_filter_set_remote_address(
		h.hostPluginPtr,
		stringToModuleBuffer(address),
		C.uint32_t(port),
		C.bool(isIPv6),
	)
	runtime.KeepAlive(address)
	return bool(ret)
}

func (h *dymListenerFilterHandle) RestoreLocalAddress(address string, port uint32, isIPv6 bool) bool {
	ret := C.envoy_dynamic_module_callback_listener_filter_restore_local_address(
		h.hostPluginPtr,
		stringToModuleBuffer(address),
		C.uint32_t(port),
		C.bool(isIPv6),
	)
	runtime.KeepAlive(address)
	return bool(ret)
}

func (h *dymListenerFilterHandle) ContinueFilterChain(success bool) {
	C.envoy_dynamic_module_callback_listener_filter_continue_filter_chain(
		h.hostPluginPtr,
		C.bool(success),
	)
}

func (h *dymListenerFilterHandle) UseOriginalDst(useOriginalDst bool) {
	C.envoy_dynamic_module_callback_listener_filter_use_original_dst(
		h.hostPluginPtr,
		C.bool(useOriginalDst),
	)
}

func (h *dymListenerFilterHandle) CloseSocket(details string) {
	C.envoy_dynamic_module_callback_listener_filter_close_socket(
		h.hostPluginPtr,
		stringToModuleBuffer(details),
	)
	runtime.KeepAlive(details)
}

func (h *dymListenerFilterHandle) WriteToSocket(data []byte) int64 {
	ret := C.envoy_dynamic_module_callback_listener_filter_write_to_socket(
		h.hostPluginPtr,
		bytesToModuleBuffer(data),
	)
	runtime.KeepAlive(data)
	return int64(ret)
}

func (h *dymListenerFilterHandle) GetSocketFD() int64 {
	return int64(C.envoy_dynamic_module_callback_listener_filter_get_socket_fd(h.hostPluginPtr))
}

func (h *dymListenerFilterHandle) SetSocketOptionInt(level, name, value int64) bool {
	return bool(C.envoy_dynamic_module_callback_listener_filter_set_socket_option_int(
		h.hostPluginPtr,
		C.int64_t(level),
		C.int64_t(name),
		C.int64_t(value),
	))
}

func (h *dymListenerFilterHandle) SetSocketOptionBytes(level, name int64, value []byte) bool {
	ret := C.envoy_dynamic_module_callback_listener_filter_set_socket_option_bytes(
		h.hostPluginPtr,
		C.int64_t(level),
		C.int64_t(name),
		bytesToModuleBuffer(value),
	)
	runtime.KeepAlive(value)
	return bool(ret)
}

func (h *dymListenerFilterHandle) GetSocketOptionInt(level, name int64) (int64, bool) {
	var value C.int64_t
	ret := C.envoy_dynamic_module_callback_listener_filter_get_socket_option_int(
		h.hostPluginPtr,
		C.int64_t(level),
		C.int64_t(name),
		&value,
	)
	if !bool(ret) {
		return 0, false
	}
	return int64(value), true
}

func (h *dymListenerFilterHandle) GetSocketOptionBytes(
	level, name int64,
	maxSize uint64,
) ([]byte, bool) {
	value := make([]byte, maxSize)
	var actualSize C.size_t
	ret := C.envoy_dynamic_module_callback_listener_filter_get_socket_option_bytes(
		h.hostPluginPtr,
		C.int64_t(level),
		C.int64_t(name),
		(*C.char)(unsafe.Pointer(unsafe.SliceData(value))),
		C.size_t(len(value)),
		&actualSize,
	)
	if !bool(ret) {
		return nil, false
	}
	return value[:int(actualSize)], true
}

func (h *dymListenerFilterHandle) SetDynamicMetadataString(
	metadataNamespace, key, value string,
) {
	C.envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string(
		h.hostPluginPtr,
		stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key),
		stringToModuleBuffer(value),
	)
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
	runtime.KeepAlive(value)
}

func (h *dymListenerFilterHandle) GetDynamicMetadataString(
	metadataNamespace, key string,
) (shared.UnsafeEnvoyBuffer, bool) {
	var valueView C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_string(
		h.hostPluginPtr,
		stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key),
		&valueView,
	)
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
	if !bool(ret) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	if valueView.ptr == nil || valueView.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, true
	}
	return envoyBufferToUnsafeEnvoyBuffer(valueView), true
}

func (h *dymListenerFilterHandle) SetDynamicMetadataNumber(
	metadataNamespace, key string,
	value float64,
) {
	C.envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_number(
		h.hostPluginPtr,
		stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key),
		C.double(value),
	)
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
}

func (h *dymListenerFilterHandle) GetDynamicMetadataNumber(
	metadataNamespace, key string,
) (float64, bool) {
	var value C.double
	ret := C.envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_number(
		h.hostPluginPtr,
		stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key),
		&value,
	)
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
	if !bool(ret) {
		return 0, false
	}
	return float64(value), true
}

func (h *dymListenerFilterHandle) SetFilterState(key string, value []byte) bool {
	ret := C.envoy_dynamic_module_callback_listener_filter_set_filter_state(
		h.hostPluginPtr,
		stringToModuleBuffer(key),
		bytesToModuleBuffer(value),
	)
	runtime.KeepAlive(key)
	runtime.KeepAlive(value)
	return bool(ret)
}

func (h *dymListenerFilterHandle) GetFilterState(key string) (shared.UnsafeEnvoyBuffer, bool) {
	var valueView C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_listener_filter_get_filter_state(
		h.hostPluginPtr,
		stringToModuleBuffer(key),
		&valueView,
	)
	runtime.KeepAlive(key)
	if !bool(ret) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	if valueView.ptr == nil || valueView.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, true
	}
	return envoyBufferToUnsafeEnvoyBuffer(valueView), true
}

func (h *dymListenerFilterHandle) SetDownstreamTransportFailureReason(reason string) {
	C.envoy_dynamic_module_callback_listener_filter_set_downstream_transport_failure_reason(
		h.hostPluginPtr,
		stringToModuleBuffer(reason),
	)
	runtime.KeepAlive(reason)
}

func (h *dymListenerFilterHandle) GetConnectionStartTimeMs() uint64 {
	return uint64(C.envoy_dynamic_module_callback_listener_filter_get_connection_start_time_ms(
		h.hostPluginPtr,
	))
}

func (h *dymListenerFilterHandle) GetCurrentMaxReadBytes() uint64 {
	return uint64(C.envoy_dynamic_module_callback_listener_filter_max_read_bytes(h.hostPluginPtr))
}

func (h *dymListenerFilterHandle) HttpCallout(
	cluster string,
	headers [][2]string,
	body []byte,
	timeoutMs uint64,
	cb shared.HttpCalloutCallback,
) (shared.HttpCalloutInitResult, uint64) {
	headerViews := headersToModuleHttpHeaderSlice(headers)
	var calloutID C.uint64_t
	result := C.envoy_dynamic_module_callback_listener_filter_http_callout(
		h.hostPluginPtr,
		&calloutID,
		stringToModuleBuffer(cluster),
		unsafe.SliceData(headerViews),
		C.size_t(len(headerViews)),
		bytesToModuleBuffer(body),
		C.uint64_t(timeoutMs),
	)
	runtime.KeepAlive(cluster)
	runtime.KeepAlive(headers)
	runtime.KeepAlive(body)
	runtime.KeepAlive(headerViews)

	goResult := shared.HttpCalloutInitResult(result)
	if goResult != shared.HttpCalloutInitSuccess {
		return goResult, 0
	}
	if h.calloutCallbacks == nil {
		h.calloutCallbacks = make(map[uint64]shared.HttpCalloutCallback)
	}
	h.calloutCallbacks[uint64(calloutID)] = cb
	return goResult, uint64(calloutID)
}

func (h *dymListenerFilterHandle) RecordHistogramValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	return shared.MetricsResult(
		C.envoy_dynamic_module_callback_listener_filter_record_histogram_value(
			h.hostPluginPtr,
			C.size_t(id),
			C.uint64_t(value),
		),
	)
}

func (h *dymListenerFilterHandle) SetGaugeValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	return shared.MetricsResult(
		C.envoy_dynamic_module_callback_listener_filter_set_gauge(
			h.hostPluginPtr,
			C.size_t(id),
			C.uint64_t(value),
		),
	)
}

func (h *dymListenerFilterHandle) IncrementGaugeValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	return shared.MetricsResult(
		C.envoy_dynamic_module_callback_listener_filter_increment_gauge(
			h.hostPluginPtr,
			C.size_t(id),
			C.uint64_t(value),
		),
	)
}

func (h *dymListenerFilterHandle) DecrementGaugeValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	return shared.MetricsResult(
		C.envoy_dynamic_module_callback_listener_filter_decrement_gauge(
			h.hostPluginPtr,
			C.size_t(id),
			C.uint64_t(value),
		),
	)
}

func (h *dymListenerFilterHandle) IncrementCounterValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	return shared.MetricsResult(
		C.envoy_dynamic_module_callback_listener_filter_increment_counter(
			h.hostPluginPtr,
			C.size_t(id),
			C.uint64_t(value),
		),
	)
}

func (h *dymListenerFilterHandle) GetScheduler() shared.Scheduler {
	if h.scheduler == nil {
		schedulerPtr := C.envoy_dynamic_module_callback_listener_filter_scheduler_new(
			h.hostPluginPtr,
		)
		h.scheduler = newDymScheduler(
			unsafe.Pointer(schedulerPtr),
			func(schedulerPtr unsafe.Pointer, taskID C.uint64_t) {
				C.envoy_dynamic_module_callback_listener_filter_scheduler_commit(
					C.envoy_dynamic_module_type_listener_filter_scheduler_module_ptr(schedulerPtr),
					taskID,
				)
			},
		)
		runtime.SetFinalizer(h.scheduler, func(s *dymScheduler) {
			C.envoy_dynamic_module_callback_listener_filter_scheduler_delete(
				C.envoy_dynamic_module_type_listener_filter_scheduler_module_ptr(s.schedulerPtr),
			)
		})
	}
	return h.scheduler
}

func (h *dymListenerFilterHandle) GetWorkerIndex() uint32 {
	return uint32(C.envoy_dynamic_module_callback_listener_filter_get_worker_index(h.hostPluginPtr))
}

func (h *dymListenerFilterHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

type dymListenerConfigHandle struct {
	hostConfigPtr C.envoy_dynamic_module_type_listener_filter_config_envoy_ptr
	scheduler     *dymScheduler
}

func (h *dymListenerConfigHandle) DefineHistogram(
	name string,
) (shared.MetricID, shared.MetricsResult) {
	var metricID C.size_t
	result := C.envoy_dynamic_module_callback_listener_filter_config_define_histogram(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		&metricID,
	)
	runtime.KeepAlive(name)
	return shared.MetricID(metricID), shared.MetricsResult(result)
}

func (h *dymListenerConfigHandle) DefineGauge(
	name string,
) (shared.MetricID, shared.MetricsResult) {
	var metricID C.size_t
	result := C.envoy_dynamic_module_callback_listener_filter_config_define_gauge(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		&metricID,
	)
	runtime.KeepAlive(name)
	return shared.MetricID(metricID), shared.MetricsResult(result)
}

func (h *dymListenerConfigHandle) DefineCounter(
	name string,
) (shared.MetricID, shared.MetricsResult) {
	var metricID C.size_t
	result := C.envoy_dynamic_module_callback_listener_filter_config_define_counter(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		&metricID,
	)
	runtime.KeepAlive(name)
	return shared.MetricID(metricID), shared.MetricsResult(result)
}

func (h *dymListenerConfigHandle) GetScheduler() shared.Scheduler {
	if h.scheduler == nil {
		schedulerPtr := C.envoy_dynamic_module_callback_listener_filter_config_scheduler_new(
			h.hostConfigPtr,
		)
		h.scheduler = newDymScheduler(
			unsafe.Pointer(schedulerPtr),
			func(schedulerPtr unsafe.Pointer, taskID C.uint64_t) {
				C.envoy_dynamic_module_callback_listener_filter_config_scheduler_commit(
					C.envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr(
						schedulerPtr,
					),
					taskID,
				)
			},
		)
		runtime.SetFinalizer(h.scheduler, func(s *dymScheduler) {
			C.envoy_dynamic_module_callback_listener_filter_config_scheduler_delete(
				C.envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr(
					s.schedulerPtr,
				),
			)
		})
	}
	return h.scheduler
}

func (h *dymListenerConfigHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

//export envoy_dynamic_module_on_listener_filter_config_new
func envoy_dynamic_module_on_listener_filter_config_new(
	hostConfigPtr C.envoy_dynamic_module_type_listener_filter_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_listener_filter_config_module_ptr {
	nameString := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesUnsafe(config)

	configHandle := &dymListenerConfigHandle{hostConfigPtr: hostConfigPtr}
	factory, err := sdk.NewListenerFilterFactory(configHandle, nameString, configBytes)
	if err != nil {
		configHandle.Log(shared.LogLevelWarn,
			"Failed to load listener filter configuration for %q: %v", nameString, err)
		return nil
	}
	if factory == nil {
		configHandle.Log(shared.LogLevelWarn,
			"Failed to load listener filter configuration for %q: listener filter factory is nil", nameString)
		return nil
	}

	configPtr := listenerConfigManager.record(&listenerFilterConfigWrapper{
		pluginFactory: factory,
		configHandle:  configHandle,
	})
	return C.envoy_dynamic_module_type_listener_filter_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_listener_filter_config_destroy
func envoy_dynamic_module_on_listener_filter_config_destroy(
	configPtr C.envoy_dynamic_module_type_listener_filter_config_module_ptr,
) {
	configWrapper := listenerConfigManager.unwrap(unsafe.Pointer(configPtr))
	if configWrapper == nil {
		return
	}
	configWrapper.configHandle.scheduler = nil
	configWrapper.pluginFactory.OnDestroy()
	listenerConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_listener_filter_new
func envoy_dynamic_module_on_listener_filter_new(
	configPtr C.envoy_dynamic_module_type_listener_filter_config_module_ptr,
	hostPluginPtr C.envoy_dynamic_module_type_listener_filter_envoy_ptr,
) C.envoy_dynamic_module_type_listener_filter_module_ptr {
	configWrapper := listenerConfigManager.unwrap(unsafe.Pointer(configPtr))
	if configWrapper == nil {
		return nil
	}

	filterWrapper := newDymListenerFilterHandle(hostPluginPtr)
	filterWrapper.plugin = configWrapper.pluginFactory.Create(filterWrapper)
	if filterWrapper.plugin == nil {
		return nil
	}
	filterPtr := listenerPluginManager.record(filterWrapper)
	return C.envoy_dynamic_module_type_listener_filter_module_ptr(filterPtr)
}

//export envoy_dynamic_module_on_listener_filter_on_accept
func envoy_dynamic_module_on_listener_filter_on_accept(
	filterEnvoyPtr C.envoy_dynamic_module_type_listener_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_listener_filter_module_ptr,
) C.envoy_dynamic_module_type_on_listener_filter_status {
	_ = filterEnvoyPtr
	filterWrapper := listenerPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.plugin == nil || filterWrapper.filterDestroyed {
		return C.envoy_dynamic_module_type_on_listener_filter_status(
			shared.ListenerFilterStatusContinue,
		)
	}
	return C.envoy_dynamic_module_type_on_listener_filter_status(filterWrapper.plugin.OnAccept())
}

//export envoy_dynamic_module_on_listener_filter_on_data
func envoy_dynamic_module_on_listener_filter_on_data(
	filterEnvoyPtr C.envoy_dynamic_module_type_listener_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_listener_filter_module_ptr,
	dataLength C.size_t,
) C.envoy_dynamic_module_type_on_listener_filter_status {
	_ = filterEnvoyPtr
	filterWrapper := listenerPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.plugin == nil || filterWrapper.filterDestroyed {
		return C.envoy_dynamic_module_type_on_listener_filter_status(
			shared.ListenerFilterStatusContinue,
		)
	}
	return C.envoy_dynamic_module_type_on_listener_filter_status(
		filterWrapper.plugin.OnData(uint64(dataLength)),
	)
}

//export envoy_dynamic_module_on_listener_filter_on_close
func envoy_dynamic_module_on_listener_filter_on_close(
	filterEnvoyPtr C.envoy_dynamic_module_type_listener_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_listener_filter_module_ptr,
) {
	_ = filterEnvoyPtr
	filterWrapper := listenerPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.plugin == nil || filterWrapper.filterDestroyed {
		return
	}
	filterWrapper.plugin.OnClose()
}

//export envoy_dynamic_module_on_listener_filter_get_max_read_bytes
func envoy_dynamic_module_on_listener_filter_get_max_read_bytes(
	filterEnvoyPtr C.envoy_dynamic_module_type_listener_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_listener_filter_module_ptr,
) C.size_t {
	_ = filterEnvoyPtr
	filterWrapper := listenerPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.plugin == nil || filterWrapper.filterDestroyed {
		return 0
	}
	return C.size_t(filterWrapper.plugin.MaxReadBytes())
}

//export envoy_dynamic_module_on_listener_filter_destroy
func envoy_dynamic_module_on_listener_filter_destroy(
	filterPtr C.envoy_dynamic_module_type_listener_filter_module_ptr,
) {
	filterWrapper := listenerPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.filterDestroyed {
		return
	}
	filterWrapper.filterDestroyed = true
	filterWrapper.scheduler = nil
	if filterWrapper.plugin != nil {
		filterWrapper.plugin.OnDestroy()
	}
	listenerPluginManager.remove(unsafe.Pointer(filterPtr))
}

//export envoy_dynamic_module_on_listener_filter_http_callout_done
func envoy_dynamic_module_on_listener_filter_http_callout_done(
	filterEnvoyPtr C.envoy_dynamic_module_type_listener_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_listener_filter_module_ptr,
	calloutID C.uint64_t,
	result C.envoy_dynamic_module_type_http_callout_result,
	headers *C.envoy_dynamic_module_type_envoy_http_header,
	headersSize C.size_t,
	chunks *C.envoy_dynamic_module_type_envoy_buffer,
	chunksSize C.size_t,
) {
	_ = filterEnvoyPtr
	filterWrapper := listenerPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.filterDestroyed {
		return
	}
	cb := filterWrapper.calloutCallbacks[uint64(calloutID)]
	if cb == nil {
		return
	}
	delete(filterWrapper.calloutCallbacks, uint64(calloutID))
	resultHeaders := envoyHttpHeaderSliceToUnsafeHeaderSlice(unsafe.Slice(headers, int(headersSize)))
	resultChunks := envoyBufferSliceToUnsafeEnvoyBufferSlice(unsafe.Slice(chunks, int(chunksSize)))
	cb.OnHttpCalloutDone(uint64(calloutID), shared.HttpCalloutResult(result), resultHeaders, resultChunks)
}

//export envoy_dynamic_module_on_listener_filter_scheduled
func envoy_dynamic_module_on_listener_filter_scheduled(
	filterEnvoyPtr C.envoy_dynamic_module_type_listener_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_listener_filter_module_ptr,
	taskID C.uint64_t,
) {
	_ = filterEnvoyPtr
	filterWrapper := listenerPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.scheduler == nil || filterWrapper.filterDestroyed {
		return
	}
	filterWrapper.scheduler.onScheduled(uint64(taskID))
}

//export envoy_dynamic_module_on_listener_filter_config_scheduled
func envoy_dynamic_module_on_listener_filter_config_scheduled(
	hostConfigPtr C.envoy_dynamic_module_type_listener_filter_config_envoy_ptr,
	configPtr C.envoy_dynamic_module_type_listener_filter_config_module_ptr,
	taskID C.uint64_t,
) {
	_ = hostConfigPtr
	configWrapper := listenerConfigManager.unwrap(unsafe.Pointer(configPtr))
	if configWrapper == nil || configWrapper.configHandle == nil || configWrapper.configHandle.scheduler == nil {
		return
	}
	configWrapper.configHandle.scheduler.onScheduled(uint64(taskID))
}
