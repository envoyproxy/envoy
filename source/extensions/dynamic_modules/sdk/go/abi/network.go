package abi

/*
#include <stdbool.h>
#include <stdint.h>
#include "../../../abi/abi.h"
*/
import "C"

import (
	"runtime"
	"unsafe"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

type networkFilterConfigWrapper struct {
	pluginFactory shared.NetworkFilterFactory
	configHandle  *dymNetworkConfigHandle
}

type networkFilterWrapper = dymNetworkFilterHandle

var networkConfigManager = newManager[networkFilterConfigWrapper]()
var networkPluginManager = newManager[networkFilterWrapper]()

type dymNetworkBuffer struct {
	hostPluginPtr C.envoy_dynamic_module_type_network_filter_envoy_ptr
	readSide      bool
}

func (b *dymNetworkBuffer) GetChunks() []shared.UnsafeEnvoyBuffer {
	var chunksSize C.size_t
	if b.readSide {
		chunksSize = C.envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size(
			b.hostPluginPtr,
		)
	} else {
		chunksSize = C.envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size(
			b.hostPluginPtr,
		)
	}

	if chunksSize == 0 {
		return nil
	}

	resultChunks := make([]C.envoy_dynamic_module_type_envoy_buffer, chunksSize)
	var ok C.bool
	if b.readSide {
		ok = C.envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks(
			b.hostPluginPtr,
			unsafe.SliceData(resultChunks),
		)
	} else {
		ok = C.envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks(
			b.hostPluginPtr,
			unsafe.SliceData(resultChunks),
		)
	}
	if !bool(ok) {
		return nil
	}

	finalResult := envoyBufferSliceToUnsafeEnvoyBufferSlice(resultChunks)
	runtime.KeepAlive(resultChunks)
	return finalResult
}

func (b *dymNetworkBuffer) GetSize() uint64 {
	if b.readSide {
		return uint64(C.envoy_dynamic_module_callback_network_filter_get_read_buffer_size(
			b.hostPluginPtr,
		))
	}
	return uint64(C.envoy_dynamic_module_callback_network_filter_get_write_buffer_size(
		b.hostPluginPtr,
	))
}

func (b *dymNetworkBuffer) Drain(numBytes uint64) bool {
	var ok C.bool
	if b.readSide {
		ok = C.envoy_dynamic_module_callback_network_filter_drain_read_buffer(
			b.hostPluginPtr,
			C.size_t(numBytes),
		)
	} else {
		ok = C.envoy_dynamic_module_callback_network_filter_drain_write_buffer(
			b.hostPluginPtr,
			C.size_t(numBytes),
		)
	}
	return bool(ok)
}

func (b *dymNetworkBuffer) Prepend(data []byte) bool {
	var ok C.bool
	if b.readSide {
		ok = C.envoy_dynamic_module_callback_network_filter_prepend_read_buffer(
			b.hostPluginPtr,
			bytesToModuleBuffer(data),
		)
	} else {
		ok = C.envoy_dynamic_module_callback_network_filter_prepend_write_buffer(
			b.hostPluginPtr,
			bytesToModuleBuffer(data),
		)
	}
	runtime.KeepAlive(data)
	return bool(ok)
}

func (b *dymNetworkBuffer) Append(data []byte) bool {
	var ok C.bool
	if b.readSide {
		ok = C.envoy_dynamic_module_callback_network_filter_append_read_buffer(
			b.hostPluginPtr,
			bytesToModuleBuffer(data),
		)
	} else {
		ok = C.envoy_dynamic_module_callback_network_filter_append_write_buffer(
			b.hostPluginPtr,
			bytesToModuleBuffer(data),
		)
	}
	runtime.KeepAlive(data)
	return bool(ok)
}

type dymNetworkFilterHandle struct {
	hostPluginPtr    C.envoy_dynamic_module_type_network_filter_envoy_ptr
	plugin           shared.NetworkFilter
	readBuffer       dymNetworkBuffer
	writeBuffer      dymNetworkBuffer
	calloutCallbacks map[uint64]shared.HttpCalloutCallback
	scheduler        *dymScheduler
	filterDestroyed  bool
}

func newDymNetworkFilterHandle(
	hostPluginPtr C.envoy_dynamic_module_type_network_filter_envoy_ptr,
) *dymNetworkFilterHandle {
	return &dymNetworkFilterHandle{
		hostPluginPtr: hostPluginPtr,
		readBuffer: dymNetworkBuffer{
			hostPluginPtr: hostPluginPtr,
			readSide:      true,
		},
		writeBuffer: dymNetworkBuffer{
			hostPluginPtr: hostPluginPtr,
			readSide:      false,
		},
	}
}

func (h *dymNetworkFilterHandle) ReadBuffer() shared.NetworkBuffer {
	return &h.readBuffer
}

func (h *dymNetworkFilterHandle) WriteBuffer() shared.NetworkBuffer {
	return &h.writeBuffer
}

func (h *dymNetworkFilterHandle) Write(data []byte, endStream bool) {
	C.envoy_dynamic_module_callback_network_filter_write(
		h.hostPluginPtr,
		bytesToModuleBuffer(data),
		C.bool(endStream),
	)
	runtime.KeepAlive(data)
}

func (h *dymNetworkFilterHandle) InjectReadData(data []byte, endStream bool) {
	C.envoy_dynamic_module_callback_network_filter_inject_read_data(
		h.hostPluginPtr,
		bytesToModuleBuffer(data),
		C.bool(endStream),
	)
	runtime.KeepAlive(data)
}

func (h *dymNetworkFilterHandle) InjectWriteData(data []byte, endStream bool) {
	C.envoy_dynamic_module_callback_network_filter_inject_write_data(
		h.hostPluginPtr,
		bytesToModuleBuffer(data),
		C.bool(endStream),
	)
	runtime.KeepAlive(data)
}

func (h *dymNetworkFilterHandle) ContinueReading() {
	C.envoy_dynamic_module_callback_network_filter_continue_reading(h.hostPluginPtr)
}

func (h *dymNetworkFilterHandle) Close(closeType shared.NetworkConnectionCloseType) {
	C.envoy_dynamic_module_callback_network_filter_close(
		h.hostPluginPtr,
		C.envoy_dynamic_module_type_network_connection_close_type(closeType),
	)
}

func (h *dymNetworkFilterHandle) GetConnectionID() uint64 {
	return uint64(C.envoy_dynamic_module_callback_network_filter_get_connection_id(h.hostPluginPtr))
}

func (h *dymNetworkFilterHandle) getAddress(
	kind int,
) (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var address C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	var ret C.bool
	switch kind {
	case 0:
		ret = C.envoy_dynamic_module_callback_network_filter_get_remote_address(
			h.hostPluginPtr,
			&address,
			&port,
		)
	case 1:
		ret = C.envoy_dynamic_module_callback_network_filter_get_local_address(
			h.hostPluginPtr,
			&address,
			&port,
		)
	case 2:
		ret = C.envoy_dynamic_module_callback_network_filter_get_direct_remote_address(
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

func (h *dymNetworkFilterHandle) GetRemoteAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	return h.getAddress(0)
}

func (h *dymNetworkFilterHandle) GetLocalAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	return h.getAddress(1)
}

func (h *dymNetworkFilterHandle) IsSSL() bool {
	return bool(C.envoy_dynamic_module_callback_network_filter_is_ssl(h.hostPluginPtr))
}

func (h *dymNetworkFilterHandle) DisableClose(disabled bool) {
	C.envoy_dynamic_module_callback_network_filter_disable_close(
		h.hostPluginPtr,
		C.bool(disabled),
	)
}

func (h *dymNetworkFilterHandle) CloseWithDetails(
	closeType shared.NetworkConnectionCloseType,
	details string,
) {
	C.envoy_dynamic_module_callback_network_filter_close_with_details(
		h.hostPluginPtr,
		C.envoy_dynamic_module_type_network_connection_close_type(closeType),
		stringToModuleBuffer(details),
	)
	runtime.KeepAlive(details)
}

func (h *dymNetworkFilterHandle) GetRequestedServerName() (shared.UnsafeEnvoyBuffer, bool) {
	var valueView C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_network_filter_get_requested_server_name(
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

func (h *dymNetworkFilterHandle) GetDirectRemoteAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	return h.getAddress(2)
}

func (h *dymNetworkFilterHandle) getSANs(
	kind int,
) []shared.UnsafeEnvoyBuffer {
	var size C.size_t
	switch kind {
	case 0:
		size = C.envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans_size(h.hostPluginPtr)
	case 1:
		size = C.envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans_size(h.hostPluginPtr)
	default:
		return nil
	}
	if size == 0 {
		return nil
	}

	result := make([]C.envoy_dynamic_module_type_envoy_buffer, size)
	var ret C.bool
	switch kind {
	case 0:
		ret = C.envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans(
			h.hostPluginPtr,
			unsafe.SliceData(result),
		)
	case 1:
		ret = C.envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans(
			h.hostPluginPtr,
			unsafe.SliceData(result),
		)
	default:
		return nil
	}
	if !bool(ret) {
		return nil
	}

	finalResult := envoyBufferSliceToUnsafeEnvoyBufferSlice(result)
	runtime.KeepAlive(result)
	return finalResult
}

func (h *dymNetworkFilterHandle) GetSSLURISANs() []shared.UnsafeEnvoyBuffer {
	return h.getSANs(0)
}

func (h *dymNetworkFilterHandle) GetSSLDNSSANs() []shared.UnsafeEnvoyBuffer {
	return h.getSANs(1)
}

func (h *dymNetworkFilterHandle) GetSSLSubject() (shared.UnsafeEnvoyBuffer, bool) {
	var valueView C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_network_filter_get_ssl_subject(
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

func (h *dymNetworkFilterHandle) SetFilterState(key string, value []byte) bool {
	ret := C.envoy_dynamic_module_callback_network_set_filter_state_bytes(
		h.hostPluginPtr,
		stringToModuleBuffer(key),
		bytesToModuleBuffer(value),
	)
	runtime.KeepAlive(key)
	runtime.KeepAlive(value)
	return bool(ret)
}

func (h *dymNetworkFilterHandle) GetFilterState(key string) (shared.UnsafeEnvoyBuffer, bool) {
	var valueView C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_network_get_filter_state_bytes(
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

func (h *dymNetworkFilterHandle) SetFilterStateTyped(key string, value []byte) bool {
	ret := C.envoy_dynamic_module_callback_network_set_filter_state_typed(
		h.hostPluginPtr,
		stringToModuleBuffer(key),
		bytesToModuleBuffer(value),
	)
	runtime.KeepAlive(key)
	runtime.KeepAlive(value)
	return bool(ret)
}

func (h *dymNetworkFilterHandle) GetFilterStateTyped(key string) (shared.UnsafeEnvoyBuffer, bool) {
	var valueView C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_network_get_filter_state_typed(
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

func (h *dymNetworkFilterHandle) GetMetadataString(
	metadataNamespace, key string,
) (shared.UnsafeEnvoyBuffer, bool) {
	var valueView C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_network_get_dynamic_metadata_string(
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

func (h *dymNetworkFilterHandle) GetMetadataNumber(metadataNamespace, key string) (float64, bool) {
	var value C.double
	ret := C.envoy_dynamic_module_callback_network_get_dynamic_metadata_number(
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

func (h *dymNetworkFilterHandle) GetMetadataBool(metadataNamespace, key string) (bool, bool) {
	var value C.bool
	ret := C.envoy_dynamic_module_callback_network_get_dynamic_metadata_bool(
		h.hostPluginPtr,
		stringToModuleBuffer(metadataNamespace),
		stringToModuleBuffer(key),
		&value,
	)
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
	if !bool(ret) {
		return false, false
	}
	return bool(value), true
}

func (h *dymNetworkFilterHandle) SetMetadata(metadataNamespace, key string, value any) {
	var numValue float64
	var isNum bool
	var strValue string
	var isStr bool

	switch v := value.(type) {
	case uint:
		numValue = float64(v)
		isNum = true
	case uint8:
		numValue = float64(v)
		isNum = true
	case uint16:
		numValue = float64(v)
		isNum = true
	case uint32:
		numValue = float64(v)
		isNum = true
	case uint64:
		numValue = float64(v)
		isNum = true
	case int:
		numValue = float64(v)
		isNum = true
	case int8:
		numValue = float64(v)
		isNum = true
	case int16:
		numValue = float64(v)
		isNum = true
	case int32:
		numValue = float64(v)
		isNum = true
	case int64:
		numValue = float64(v)
		isNum = true
	case float32:
		numValue = float64(v)
		isNum = true
	case float64:
		numValue = float64(v)
		isNum = true
	case bool:
		C.envoy_dynamic_module_callback_network_set_dynamic_metadata_bool(
			h.hostPluginPtr,
			stringToModuleBuffer(metadataNamespace),
			stringToModuleBuffer(key),
			C.bool(v),
		)
		runtime.KeepAlive(metadataNamespace)
		runtime.KeepAlive(key)
		return
	case string:
		strValue = v
		isStr = true
	}

	if isNum {
		C.envoy_dynamic_module_callback_network_set_dynamic_metadata_number(
			h.hostPluginPtr,
			stringToModuleBuffer(metadataNamespace),
			stringToModuleBuffer(key),
			C.double(numValue),
		)
	} else if isStr {
		C.envoy_dynamic_module_callback_network_set_dynamic_metadata_string(
			h.hostPluginPtr,
			stringToModuleBuffer(metadataNamespace),
			stringToModuleBuffer(key),
			stringToModuleBuffer(strValue),
		)
	}
	runtime.KeepAlive(metadataNamespace)
	runtime.KeepAlive(key)
	runtime.KeepAlive(strValue)
}

func (h *dymNetworkFilterHandle) SetSocketOptionInt(
	level, name int64,
	state shared.SocketOptionState,
	value int64,
) {
	C.envoy_dynamic_module_callback_network_set_socket_option_int(
		h.hostPluginPtr,
		C.int64_t(level),
		C.int64_t(name),
		C.envoy_dynamic_module_type_socket_option_state(state),
		C.int64_t(value),
	)
}

func (h *dymNetworkFilterHandle) SetSocketOptionBytes(
	level, name int64,
	state shared.SocketOptionState,
	value []byte,
) {
	C.envoy_dynamic_module_callback_network_set_socket_option_bytes(
		h.hostPluginPtr,
		C.int64_t(level),
		C.int64_t(name),
		C.envoy_dynamic_module_type_socket_option_state(state),
		bytesToModuleBuffer(value),
	)
	runtime.KeepAlive(value)
}

func (h *dymNetworkFilterHandle) GetSocketOptionInt(
	level, name int64,
	state shared.SocketOptionState,
) (int64, bool) {
	var value C.int64_t
	ret := C.envoy_dynamic_module_callback_network_get_socket_option_int(
		h.hostPluginPtr,
		C.int64_t(level),
		C.int64_t(name),
		C.envoy_dynamic_module_type_socket_option_state(state),
		&value,
	)
	if !bool(ret) {
		return 0, false
	}
	return int64(value), true
}

func (h *dymNetworkFilterHandle) GetSocketOptionBytes(
	level, name int64,
	state shared.SocketOptionState,
) (shared.UnsafeEnvoyBuffer, bool) {
	var valueView C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_network_get_socket_option_bytes(
		h.hostPluginPtr,
		C.int64_t(level),
		C.int64_t(name),
		C.envoy_dynamic_module_type_socket_option_state(state),
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

func (h *dymNetworkFilterHandle) GetSocketOptions() []shared.SocketOption {
	size := C.envoy_dynamic_module_callback_network_get_socket_options_size(h.hostPluginPtr)
	if size == 0 {
		return nil
	}

	result := make([]C.envoy_dynamic_module_type_socket_option, size)
	C.envoy_dynamic_module_callback_network_get_socket_options(
		h.hostPluginPtr,
		unsafe.SliceData(result),
	)

	finalResult := make([]shared.SocketOption, 0, len(result))
	for _, option := range result {
		finalResult = append(finalResult, shared.SocketOption{
			Level:      int64(option.level),
			Name:       int64(option.name),
			State:      shared.SocketOptionState(option.state),
			ValueType:  shared.SocketOptionValueType(option.value_type),
			IntValue:   int64(option.int_value),
			BytesValue: envoyBufferToUnsafeEnvoyBuffer(option.byte_value),
		})
	}
	runtime.KeepAlive(result)
	return finalResult
}

func (h *dymNetworkFilterHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

func (h *dymNetworkFilterHandle) HttpCallout(
	cluster string, headers [][2]string, body []byte, timeoutMs uint64,
	cb shared.HttpCalloutCallback,
) (shared.HttpCalloutInitResult, uint64) {
	headerViews := headersToModuleHttpHeaderSlice(headers)
	var calloutID C.uint64_t

	result := C.envoy_dynamic_module_callback_network_filter_http_callout(
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

func (h *dymNetworkFilterHandle) RecordHistogramValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	result := C.envoy_dynamic_module_callback_network_filter_record_histogram_value(
		h.hostPluginPtr,
		C.size_t(id),
		C.uint64_t(value),
	)
	return shared.MetricsResult(result)
}

func (h *dymNetworkFilterHandle) SetGaugeValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	result := C.envoy_dynamic_module_callback_network_filter_set_gauge(
		h.hostPluginPtr,
		C.size_t(id),
		C.uint64_t(value),
	)
	return shared.MetricsResult(result)
}

func (h *dymNetworkFilterHandle) IncrementGaugeValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	result := C.envoy_dynamic_module_callback_network_filter_increment_gauge(
		h.hostPluginPtr,
		C.size_t(id),
		C.uint64_t(value),
	)
	return shared.MetricsResult(result)
}

func (h *dymNetworkFilterHandle) DecrementGaugeValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	result := C.envoy_dynamic_module_callback_network_filter_decrement_gauge(
		h.hostPluginPtr,
		C.size_t(id),
		C.uint64_t(value),
	)
	return shared.MetricsResult(result)
}

func (h *dymNetworkFilterHandle) IncrementCounterValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	result := C.envoy_dynamic_module_callback_network_filter_increment_counter(
		h.hostPluginPtr,
		C.size_t(id),
		C.uint64_t(value),
	)
	return shared.MetricsResult(result)
}

func (h *dymNetworkFilterHandle) GetClusterHostCounts(
	cluster string,
	priority uint32,
) (shared.ClusterHostCounts, bool) {
	var total C.size_t
	var healthy C.size_t
	var degraded C.size_t
	ret := C.envoy_dynamic_module_callback_network_filter_get_cluster_host_count(
		h.hostPluginPtr,
		stringToModuleBuffer(cluster),
		C.uint32_t(priority),
		&total,
		&healthy,
		&degraded,
	)
	runtime.KeepAlive(cluster)
	if !bool(ret) {
		return shared.ClusterHostCounts{}, false
	}
	return shared.ClusterHostCounts{
		Total:    uint64(total),
		Healthy:  uint64(healthy),
		Degraded: uint64(degraded),
	}, true
}

func (h *dymNetworkFilterHandle) GetUpstreamHostAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var address C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ret := C.envoy_dynamic_module_callback_network_filter_get_upstream_host_address(
		h.hostPluginPtr,
		&address,
		&port,
	)
	if !bool(ret) {
		return shared.UnsafeEnvoyBuffer{}, 0, false
	}
	if address.ptr == nil || address.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, uint32(port), true
	}
	return envoyBufferToUnsafeEnvoyBuffer(address), uint32(port), true
}

func (h *dymNetworkFilterHandle) GetUpstreamHostHostname() (shared.UnsafeEnvoyBuffer, bool) {
	var valueView C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_network_filter_get_upstream_host_hostname(
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

func (h *dymNetworkFilterHandle) GetUpstreamHostCluster() (shared.UnsafeEnvoyBuffer, bool) {
	var valueView C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_network_filter_get_upstream_host_cluster(
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

func (h *dymNetworkFilterHandle) HasUpstreamHost() bool {
	return bool(C.envoy_dynamic_module_callback_network_filter_has_upstream_host(h.hostPluginPtr))
}

func (h *dymNetworkFilterHandle) StartUpstreamSecureTransport() bool {
	return bool(C.envoy_dynamic_module_callback_network_filter_start_upstream_secure_transport(
		h.hostPluginPtr,
	))
}

func (h *dymNetworkFilterHandle) GetConnectionState() shared.NetworkConnectionState {
	return shared.NetworkConnectionState(
		C.envoy_dynamic_module_callback_network_filter_get_connection_state(h.hostPluginPtr),
	)
}

func (h *dymNetworkFilterHandle) ReadDisable(disable bool) shared.NetworkReadDisableStatus {
	return shared.NetworkReadDisableStatus(
		C.envoy_dynamic_module_callback_network_filter_read_disable(
			h.hostPluginPtr,
			C.bool(disable),
		),
	)
}

func (h *dymNetworkFilterHandle) ReadEnabled() bool {
	return bool(C.envoy_dynamic_module_callback_network_filter_read_enabled(h.hostPluginPtr))
}

func (h *dymNetworkFilterHandle) IsHalfCloseEnabled() bool {
	return bool(C.envoy_dynamic_module_callback_network_filter_is_half_close_enabled(
		h.hostPluginPtr,
	))
}

func (h *dymNetworkFilterHandle) EnableHalfClose(enabled bool) {
	C.envoy_dynamic_module_callback_network_filter_enable_half_close(
		h.hostPluginPtr,
		C.bool(enabled),
	)
}

func (h *dymNetworkFilterHandle) GetBufferLimit() uint32 {
	return uint32(C.envoy_dynamic_module_callback_network_filter_get_buffer_limit(h.hostPluginPtr))
}

func (h *dymNetworkFilterHandle) SetBufferLimits(limit uint32) {
	C.envoy_dynamic_module_callback_network_filter_set_buffer_limits(
		h.hostPluginPtr,
		C.uint32_t(limit),
	)
}

func (h *dymNetworkFilterHandle) AboveHighWatermark() bool {
	return bool(C.envoy_dynamic_module_callback_network_filter_above_high_watermark(
		h.hostPluginPtr,
	))
}

func (h *dymNetworkFilterHandle) GetScheduler() shared.Scheduler {
	if h.scheduler == nil {
		schedulerPtr := C.envoy_dynamic_module_callback_network_filter_scheduler_new(h.hostPluginPtr)
		h.scheduler = newDymScheduler(
			unsafe.Pointer(schedulerPtr),
			func(schedulerPtr unsafe.Pointer, taskID C.uint64_t) {
				C.envoy_dynamic_module_callback_network_filter_scheduler_commit(
					C.envoy_dynamic_module_type_network_filter_scheduler_module_ptr(schedulerPtr),
					taskID,
				)
			},
		)

		runtime.SetFinalizer(h.scheduler, func(s *dymScheduler) {
			C.envoy_dynamic_module_callback_network_filter_scheduler_delete(
				C.envoy_dynamic_module_type_network_filter_scheduler_module_ptr(s.schedulerPtr),
			)
		})
	}
	return h.scheduler
}

func (h *dymNetworkFilterHandle) GetWorkerIndex() uint32 {
	return uint32(C.envoy_dynamic_module_callback_network_filter_get_worker_index(h.hostPluginPtr))
}

type dymNetworkConfigHandle struct {
	hostConfigPtr C.envoy_dynamic_module_type_network_filter_config_envoy_ptr
	scheduler     *dymScheduler
}

func (h *dymNetworkConfigHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

func (h *dymNetworkConfigHandle) DefineHistogram(name string) (shared.MetricID, shared.MetricsResult) {
	var metricID C.size_t
	result := C.envoy_dynamic_module_callback_network_filter_config_define_histogram(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		&metricID,
	)
	runtime.KeepAlive(name)
	return shared.MetricID(metricID), shared.MetricsResult(result)
}

func (h *dymNetworkConfigHandle) DefineGauge(name string) (shared.MetricID, shared.MetricsResult) {
	var metricID C.size_t
	result := C.envoy_dynamic_module_callback_network_filter_config_define_gauge(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		&metricID,
	)
	runtime.KeepAlive(name)
	return shared.MetricID(metricID), shared.MetricsResult(result)
}

func (h *dymNetworkConfigHandle) DefineCounter(name string) (shared.MetricID, shared.MetricsResult) {
	var metricID C.size_t
	result := C.envoy_dynamic_module_callback_network_filter_config_define_counter(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		&metricID,
	)
	runtime.KeepAlive(name)
	return shared.MetricID(metricID), shared.MetricsResult(result)
}

func (h *dymNetworkConfigHandle) GetScheduler() shared.Scheduler {
	if h.scheduler == nil {
		schedulerPtr := C.envoy_dynamic_module_callback_network_filter_config_scheduler_new(
			h.hostConfigPtr,
		)
		h.scheduler = newDymScheduler(
			unsafe.Pointer(schedulerPtr),
			func(schedulerPtr unsafe.Pointer, taskID C.uint64_t) {
				C.envoy_dynamic_module_callback_network_filter_config_scheduler_commit(
					C.envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr(
						schedulerPtr,
					),
					taskID,
				)
			},
		)

		runtime.SetFinalizer(h.scheduler, func(s *dymScheduler) {
			C.envoy_dynamic_module_callback_network_filter_config_scheduler_delete(
				C.envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr(
					s.schedulerPtr,
				),
			)
		})
	}
	return h.scheduler
}

//export envoy_dynamic_module_on_network_filter_config_new
func envoy_dynamic_module_on_network_filter_config_new(
	hostConfigPtr C.envoy_dynamic_module_type_network_filter_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_network_filter_config_module_ptr {
	nameString := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesUnsafe(config)

	configHandle := &dymNetworkConfigHandle{hostConfigPtr: hostConfigPtr}
	factory, err := sdk.NewNetworkFilterFactory(configHandle, nameString, configBytes)
	if err != nil {
		configHandle.Log(shared.LogLevelWarn,
			"Failed to load network filter configuration for %q: %v", nameString, err)
		return nil
	}
	if factory == nil {
		configHandle.Log(shared.LogLevelWarn,
			"Failed to load network filter configuration for %q: network filter factory is nil", nameString)
		return nil
	}

	configPtr := networkConfigManager.record(&networkFilterConfigWrapper{
		pluginFactory: factory,
		configHandle:  configHandle,
	})
	return C.envoy_dynamic_module_type_network_filter_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_network_filter_config_destroy
func envoy_dynamic_module_on_network_filter_config_destroy(
	configPtr C.envoy_dynamic_module_type_network_filter_config_module_ptr,
) {
	configWrapper := networkConfigManager.unwrap(unsafe.Pointer(configPtr))
	if configWrapper == nil {
		return
	}
	configWrapper.configHandle.scheduler = nil
	configWrapper.pluginFactory.OnDestroy()
	networkConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_network_filter_new
func envoy_dynamic_module_on_network_filter_new(
	configPtr C.envoy_dynamic_module_type_network_filter_config_module_ptr,
	hostPluginPtr C.envoy_dynamic_module_type_network_filter_envoy_ptr,
) C.envoy_dynamic_module_type_network_filter_module_ptr {
	configWrapper := networkConfigManager.unwrap(unsafe.Pointer(configPtr))
	if configWrapper == nil {
		return nil
	}

	filterWrapper := newDymNetworkFilterHandle(hostPluginPtr)
	filterWrapper.plugin = configWrapper.pluginFactory.Create(filterWrapper)
	if filterWrapper.plugin == nil {
		return nil
	}

	filterPtr := networkPluginManager.record(filterWrapper)
	return C.envoy_dynamic_module_type_network_filter_module_ptr(filterPtr)
}

//export envoy_dynamic_module_on_network_filter_new_connection
func envoy_dynamic_module_on_network_filter_new_connection(
	filterEnvoyPtr C.envoy_dynamic_module_type_network_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
) C.envoy_dynamic_module_type_on_network_filter_data_status {
	_ = filterEnvoyPtr
	filterWrapper := networkPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.plugin == nil || filterWrapper.filterDestroyed {
		return C.envoy_dynamic_module_type_on_network_filter_data_status(
			shared.NetworkFilterStatusContinue,
		)
	}
	return C.envoy_dynamic_module_type_on_network_filter_data_status(
		filterWrapper.plugin.OnNewConnection(),
	)
}

//export envoy_dynamic_module_on_network_filter_read
func envoy_dynamic_module_on_network_filter_read(
	filterEnvoyPtr C.envoy_dynamic_module_type_network_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
	dataLength C.size_t,
	endStream C.bool,
) C.envoy_dynamic_module_type_on_network_filter_data_status {
	_ = filterEnvoyPtr
	_ = dataLength
	filterWrapper := networkPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.plugin == nil || filterWrapper.filterDestroyed {
		return C.envoy_dynamic_module_type_on_network_filter_data_status(
			shared.NetworkFilterStatusContinue,
		)
	}
	return C.envoy_dynamic_module_type_on_network_filter_data_status(
		filterWrapper.plugin.OnRead(&filterWrapper.readBuffer, bool(endStream)),
	)
}

//export envoy_dynamic_module_on_network_filter_write
func envoy_dynamic_module_on_network_filter_write(
	filterEnvoyPtr C.envoy_dynamic_module_type_network_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
	dataLength C.size_t,
	endStream C.bool,
) C.envoy_dynamic_module_type_on_network_filter_data_status {
	_ = filterEnvoyPtr
	_ = dataLength
	filterWrapper := networkPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.plugin == nil || filterWrapper.filterDestroyed {
		return C.envoy_dynamic_module_type_on_network_filter_data_status(
			shared.NetworkFilterStatusContinue,
		)
	}
	return C.envoy_dynamic_module_type_on_network_filter_data_status(
		filterWrapper.plugin.OnWrite(&filterWrapper.writeBuffer, bool(endStream)),
	)
}

//export envoy_dynamic_module_on_network_filter_event
func envoy_dynamic_module_on_network_filter_event(
	filterEnvoyPtr C.envoy_dynamic_module_type_network_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
	event C.envoy_dynamic_module_type_network_connection_event,
) {
	_ = filterEnvoyPtr
	filterWrapper := networkPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.plugin == nil || filterWrapper.filterDestroyed {
		return
	}
	filterWrapper.plugin.OnEvent(shared.NetworkConnectionEvent(event))
}

//export envoy_dynamic_module_on_network_filter_destroy
func envoy_dynamic_module_on_network_filter_destroy(
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
) {
	filterWrapper := networkPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.filterDestroyed {
		return
	}
	filterWrapper.filterDestroyed = true
	filterWrapper.scheduler = nil
	if filterWrapper.plugin != nil {
		filterWrapper.plugin.OnDestroy()
	}
	networkPluginManager.remove(unsafe.Pointer(filterPtr))
}

//export envoy_dynamic_module_on_network_filter_http_callout_done
func envoy_dynamic_module_on_network_filter_http_callout_done(
	filterEnvoyPtr C.envoy_dynamic_module_type_network_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
	calloutID C.uint64_t,
	result C.envoy_dynamic_module_type_http_callout_result,
	headers *C.envoy_dynamic_module_type_envoy_http_header,
	headersSize C.size_t,
	chunks *C.envoy_dynamic_module_type_envoy_buffer,
	chunksSize C.size_t,
) {
	_ = filterEnvoyPtr
	filterWrapper := networkPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.filterDestroyed {
		return
	}

	resultHeaders := envoyHttpHeaderSliceToUnsafeHeaderSlice(unsafe.Slice(headers, int(headersSize)))
	resultChunks := envoyBufferSliceToUnsafeEnvoyBufferSlice(unsafe.Slice(chunks, int(chunksSize)))

	cb := filterWrapper.calloutCallbacks[uint64(calloutID)]
	if cb != nil {
		delete(filterWrapper.calloutCallbacks, uint64(calloutID))
		cb.OnHttpCalloutDone(uint64(calloutID), shared.HttpCalloutResult(result), resultHeaders, resultChunks)
	}
}

//export envoy_dynamic_module_on_network_filter_scheduled
func envoy_dynamic_module_on_network_filter_scheduled(
	filterEnvoyPtr C.envoy_dynamic_module_type_network_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
	taskID C.uint64_t,
) {
	_ = filterEnvoyPtr
	filterWrapper := networkPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.scheduler == nil || filterWrapper.filterDestroyed {
		return
	}
	filterWrapper.scheduler.onScheduled(uint64(taskID))
}

//export envoy_dynamic_module_on_network_filter_config_scheduled
func envoy_dynamic_module_on_network_filter_config_scheduled(
	configPtr C.envoy_dynamic_module_type_network_filter_config_module_ptr,
	taskID C.uint64_t,
) {
	configWrapper := networkConfigManager.unwrap(unsafe.Pointer(configPtr))
	if configWrapper == nil || configWrapper.configHandle == nil || configWrapper.configHandle.scheduler == nil {
		return
	}
	configWrapper.configHandle.scheduler.onScheduled(uint64(taskID))
}

//export envoy_dynamic_module_on_network_filter_above_write_buffer_high_watermark
func envoy_dynamic_module_on_network_filter_above_write_buffer_high_watermark(
	filterEnvoyPtr C.envoy_dynamic_module_type_network_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
) {
	_ = filterEnvoyPtr
	filterWrapper := networkPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.plugin == nil || filterWrapper.filterDestroyed {
		return
	}
	filterWrapper.plugin.OnAboveWriteBufferHighWatermark()
}

//export envoy_dynamic_module_on_network_filter_below_write_buffer_low_watermark
func envoy_dynamic_module_on_network_filter_below_write_buffer_low_watermark(
	filterEnvoyPtr C.envoy_dynamic_module_type_network_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_network_filter_module_ptr,
) {
	_ = filterEnvoyPtr
	filterWrapper := networkPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.plugin == nil || filterWrapper.filterDestroyed {
		return
	}
	filterWrapper.plugin.OnBelowWriteBufferLowWatermark()
}
