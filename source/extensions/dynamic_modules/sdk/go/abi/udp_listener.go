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

type udpListenerFilterConfigWrapper struct {
	pluginFactory shared.UdpListenerFilterFactory
	configHandle  *dymUdpListenerConfigHandle
}

type udpListenerFilterWrapper = dymUdpListenerFilterHandle

var udpListenerConfigManager = newManager[udpListenerFilterConfigWrapper]()
var udpListenerPluginManager = newManager[udpListenerFilterWrapper]()

type dymUdpListenerFilterHandle struct {
	hostPluginPtr   C.envoy_dynamic_module_type_udp_listener_filter_envoy_ptr
	plugin          shared.UdpListenerFilter
	filterDestroyed bool
}

type udpListenerAddressKind int

const (
	udpListenerAddressPeer udpListenerAddressKind = iota
	udpListenerAddressLocal
)

func newDymUdpListenerFilterHandle(
	hostPluginPtr C.envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
) *dymUdpListenerFilterHandle {
	return &dymUdpListenerFilterHandle{hostPluginPtr: hostPluginPtr}
}

func (h *dymUdpListenerFilterHandle) GetDatagramChunks() []shared.UnsafeEnvoyBuffer {
	size := C.envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks_size(
		h.hostPluginPtr,
	)
	if size == 0 {
		return nil
	}
	result := make([]C.envoy_dynamic_module_type_envoy_buffer, size)
	ok := C.envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks(
		h.hostPluginPtr,
		unsafe.SliceData(result),
	)
	if !bool(ok) {
		return nil
	}
	chunks := envoyBufferSliceToUnsafeEnvoyBufferSlice(result)
	runtime.KeepAlive(result)
	return chunks
}

func (h *dymUdpListenerFilterHandle) GetDatagramSize() uint64 {
	return uint64(C.envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_size(
		h.hostPluginPtr,
	))
}

func (h *dymUdpListenerFilterHandle) SetDatagramData(data []byte) bool {
	ret := C.envoy_dynamic_module_callback_udp_listener_filter_set_datagram_data(
		h.hostPluginPtr,
		bytesToModuleBuffer(data),
	)
	runtime.KeepAlive(data)
	return bool(ret)
}

func (h *dymUdpListenerFilterHandle) getAddress(
	kind udpListenerAddressKind,
) (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var address C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	var ret C.bool
	switch kind {
	case udpListenerAddressPeer:
		ret = C.envoy_dynamic_module_callback_udp_listener_filter_get_peer_address(
			h.hostPluginPtr,
			&address,
			&port,
		)
	case udpListenerAddressLocal:
		ret = C.envoy_dynamic_module_callback_udp_listener_filter_get_local_address(
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

func (h *dymUdpListenerFilterHandle) GetPeerAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	return h.getAddress(udpListenerAddressPeer)
}

func (h *dymUdpListenerFilterHandle) GetLocalAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	return h.getAddress(udpListenerAddressLocal)
}

func (h *dymUdpListenerFilterHandle) SendDatagram(
	data []byte,
	peerAddress string,
	peerPort uint32,
) bool {
	// An empty peer address tells Envoy to reuse the current datagram's sender, which it only does
	// for a null buffer, so do not hand it the pointer of an empty Go string.
	peerAddressBuffer := nullModuleBuffer()
	if peerAddress != "" {
		peerAddressBuffer = stringToModuleBuffer(peerAddress)
	}
	ret := C.envoy_dynamic_module_callback_udp_listener_filter_send_datagram(
		h.hostPluginPtr,
		bytesToModuleBuffer(data),
		peerAddressBuffer,
		C.uint32_t(peerPort),
	)
	runtime.KeepAlive(data)
	runtime.KeepAlive(peerAddress)
	return bool(ret)
}

func (h *dymUdpListenerFilterHandle) IncrementCounterValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	return shared.MetricsResult(
		C.envoy_dynamic_module_callback_udp_listener_filter_increment_counter(
			h.hostPluginPtr,
			C.size_t(id),
			C.uint64_t(value),
		),
	)
}

func (h *dymUdpListenerFilterHandle) SetGaugeValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	return shared.MetricsResult(
		C.envoy_dynamic_module_callback_udp_listener_filter_set_gauge(
			h.hostPluginPtr,
			C.size_t(id),
			C.uint64_t(value),
		),
	)
}

func (h *dymUdpListenerFilterHandle) IncrementGaugeValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	return shared.MetricsResult(
		C.envoy_dynamic_module_callback_udp_listener_filter_increment_gauge(
			h.hostPluginPtr,
			C.size_t(id),
			C.uint64_t(value),
		),
	)
}

func (h *dymUdpListenerFilterHandle) DecrementGaugeValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	return shared.MetricsResult(
		C.envoy_dynamic_module_callback_udp_listener_filter_decrement_gauge(
			h.hostPluginPtr,
			C.size_t(id),
			C.uint64_t(value),
		),
	)
}

func (h *dymUdpListenerFilterHandle) RecordHistogramValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	return shared.MetricsResult(
		C.envoy_dynamic_module_callback_udp_listener_filter_record_histogram_value(
			h.hostPluginPtr,
			C.size_t(id),
			C.uint64_t(value),
		),
	)
}

func (h *dymUdpListenerFilterHandle) GetWorkerIndex() uint32 {
	return uint32(C.envoy_dynamic_module_callback_udp_listener_filter_get_worker_index(
		h.hostPluginPtr,
	))
}

func (h *dymUdpListenerFilterHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

type dymUdpListenerConfigHandle struct {
	hostConfigPtr C.envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr
}

func (h *dymUdpListenerConfigHandle) DefineHistogram(
	name string,
) (shared.MetricID, shared.MetricsResult) {
	var metricID C.size_t
	result := C.envoy_dynamic_module_callback_udp_listener_filter_config_define_histogram(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		&metricID,
	)
	runtime.KeepAlive(name)
	return shared.MetricID(metricID), shared.MetricsResult(result)
}

func (h *dymUdpListenerConfigHandle) DefineGauge(
	name string,
) (shared.MetricID, shared.MetricsResult) {
	var metricID C.size_t
	result := C.envoy_dynamic_module_callback_udp_listener_filter_config_define_gauge(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		&metricID,
	)
	runtime.KeepAlive(name)
	return shared.MetricID(metricID), shared.MetricsResult(result)
}

func (h *dymUdpListenerConfigHandle) DefineCounter(
	name string,
) (shared.MetricID, shared.MetricsResult) {
	var metricID C.size_t
	result := C.envoy_dynamic_module_callback_udp_listener_filter_config_define_counter(
		h.hostConfigPtr,
		stringToModuleBuffer(name),
		&metricID,
	)
	runtime.KeepAlive(name)
	return shared.MetricID(metricID), shared.MetricsResult(result)
}

func (h *dymUdpListenerConfigHandle) IncrementCounterValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	return shared.MetricsResult(
		C.envoy_dynamic_module_callback_udp_listener_filter_config_increment_counter(
			h.hostConfigPtr,
			C.size_t(id),
			C.uint64_t(value),
		),
	)
}

func (h *dymUdpListenerConfigHandle) SetGaugeValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	return shared.MetricsResult(
		C.envoy_dynamic_module_callback_udp_listener_filter_config_set_gauge(
			h.hostConfigPtr,
			C.size_t(id),
			C.uint64_t(value),
		),
	)
}

func (h *dymUdpListenerConfigHandle) IncrementGaugeValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	return shared.MetricsResult(
		C.envoy_dynamic_module_callback_udp_listener_filter_config_increment_gauge(
			h.hostConfigPtr,
			C.size_t(id),
			C.uint64_t(value),
		),
	)
}

func (h *dymUdpListenerConfigHandle) DecrementGaugeValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	return shared.MetricsResult(
		C.envoy_dynamic_module_callback_udp_listener_filter_config_decrement_gauge(
			h.hostConfigPtr,
			C.size_t(id),
			C.uint64_t(value),
		),
	)
}

func (h *dymUdpListenerConfigHandle) RecordHistogramValue(
	id shared.MetricID,
	value uint64,
) shared.MetricsResult {
	return shared.MetricsResult(
		C.envoy_dynamic_module_callback_udp_listener_filter_config_record_histogram_value(
			h.hostConfigPtr,
			C.size_t(id),
			C.uint64_t(value),
		),
	)
}

func (h *dymUdpListenerConfigHandle) Log(level shared.LogLevel, format string, args ...any) {
	hostLog(level, format, args)
}

//export envoy_dynamic_module_on_udp_listener_filter_config_new
func envoy_dynamic_module_on_udp_listener_filter_config_new(
	hostConfigPtr C.envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_udp_listener_filter_config_module_ptr {
	nameString := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesUnsafe(config)

	configHandle := &dymUdpListenerConfigHandle{hostConfigPtr: hostConfigPtr}
	factory, err := sdk.NewUdpListenerFilterFactory(configHandle, nameString, configBytes)
	if err != nil {
		configHandle.Log(shared.LogLevelWarn,
			"Failed to load UDP listener filter configuration for %q: %v", nameString, err)
		return nil
	}
	if factory == nil {
		configHandle.Log(shared.LogLevelWarn,
			"Failed to load UDP listener filter configuration for %q: UDP listener filter factory is nil",
			nameString)
		return nil
	}

	configPtr := udpListenerConfigManager.record(&udpListenerFilterConfigWrapper{
		pluginFactory: factory,
		configHandle:  configHandle,
	})
	return C.envoy_dynamic_module_type_udp_listener_filter_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_udp_listener_filter_config_destroy
func envoy_dynamic_module_on_udp_listener_filter_config_destroy(
	configPtr C.envoy_dynamic_module_type_udp_listener_filter_config_module_ptr,
) {
	configWrapper := udpListenerConfigManager.unwrap(unsafe.Pointer(configPtr))
	if configWrapper == nil {
		return
	}
	configWrapper.pluginFactory.OnDestroy()
	udpListenerConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_udp_listener_filter_new
func envoy_dynamic_module_on_udp_listener_filter_new(
	configPtr C.envoy_dynamic_module_type_udp_listener_filter_config_module_ptr,
	hostPluginPtr C.envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
) C.envoy_dynamic_module_type_udp_listener_filter_module_ptr {
	configWrapper := udpListenerConfigManager.unwrap(unsafe.Pointer(configPtr))
	if configWrapper == nil {
		return nil
	}

	filterWrapper := newDymUdpListenerFilterHandle(hostPluginPtr)
	filterWrapper.plugin = configWrapper.pluginFactory.Create(filterWrapper)
	if filterWrapper.plugin == nil {
		return nil
	}
	filterPtr := udpListenerPluginManager.record(filterWrapper)
	return C.envoy_dynamic_module_type_udp_listener_filter_module_ptr(filterPtr)
}

//export envoy_dynamic_module_on_udp_listener_filter_on_data
func envoy_dynamic_module_on_udp_listener_filter_on_data(
	filterEnvoyPtr C.envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_udp_listener_filter_module_ptr,
) C.envoy_dynamic_module_type_on_udp_listener_filter_status {
	_ = filterEnvoyPtr
	filterWrapper := udpListenerPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.plugin == nil || filterWrapper.filterDestroyed {
		return C.envoy_dynamic_module_type_on_udp_listener_filter_status(
			shared.UdpListenerFilterStatusContinue,
		)
	}
	return C.envoy_dynamic_module_type_on_udp_listener_filter_status(
		filterWrapper.plugin.OnData(),
	)
}

//export envoy_dynamic_module_on_udp_listener_filter_destroy
func envoy_dynamic_module_on_udp_listener_filter_destroy(
	filterPtr C.envoy_dynamic_module_type_udp_listener_filter_module_ptr,
) {
	filterWrapper := udpListenerPluginManager.unwrap(unsafe.Pointer(filterPtr))
	if filterWrapper == nil || filterWrapper.filterDestroyed {
		return
	}
	filterWrapper.filterDestroyed = true
	if filterWrapper.plugin != nil {
		filterWrapper.plugin.OnDestroy()
	}
	udpListenerPluginManager.remove(unsafe.Pointer(filterPtr))
}
