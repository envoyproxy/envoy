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

type udpListenerFilterConfigWrapper struct {
	factory      shared.UdpListenerFilterFactory
	configHandle *dymUdpListenerConfigHandle
}

var udpListenerConfigManager = newManager[udpListenerFilterConfigWrapper]()
var udpListenerFilterManager = newManager[dymUdpListenerFilterHandle]()

// dymUdpListenerConfigHandle implements shared.UdpListenerFilterConfigHandle.
type dymUdpListenerConfigHandle struct {
	hostConfigPtr C.envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr
}

func (h *dymUdpListenerConfigHandle) DefineCounter(name string) (shared.MetricID, shared.MetricsResult) {
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_udp_listener_filter_config_define_counter(
		h.hostConfigPtr, stringToModuleBuffer(name), &id)
	runtime.KeepAlive(name)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymUdpListenerConfigHandle) DefineGauge(name string) (shared.MetricID, shared.MetricsResult) {
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_udp_listener_filter_config_define_gauge(
		h.hostConfigPtr, stringToModuleBuffer(name), &id)
	runtime.KeepAlive(name)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymUdpListenerConfigHandle) DefineHistogram(name string) (shared.MetricID, shared.MetricsResult) {
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_udp_listener_filter_config_define_histogram(
		h.hostConfigPtr, stringToModuleBuffer(name), &id)
	runtime.KeepAlive(name)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

// dymUdpListenerFilterHandle implements shared.UdpListenerFilterHandle.
type dymUdpListenerFilterHandle struct {
	hostFilterPtr C.envoy_dynamic_module_type_udp_listener_filter_envoy_ptr

	plugin    shared.UdpListenerFilter
	destroyed bool
}

func (h *dymUdpListenerFilterHandle) GetDatagramData() []shared.UnsafeEnvoyBuffer {
	size := C.envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks_size(h.hostFilterPtr)
	if size == 0 {
		return nil
	}
	bufs := make([]C.envoy_dynamic_module_type_envoy_buffer, int(size))
	if !bool(C.envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks(
		h.hostFilterPtr, unsafe.SliceData(bufs))) {
		return nil
	}
	out := envoyBufferSliceToUnsafeEnvoyBufferSlice(bufs)
	runtime.KeepAlive(bufs)
	return out
}

func (h *dymUdpListenerFilterHandle) GetDatagramSize() uint64 {
	return uint64(C.envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_size(h.hostFilterPtr))
}

func (h *dymUdpListenerFilterHandle) SetDatagramData(data []byte) bool {
	ret := C.envoy_dynamic_module_callback_udp_listener_filter_set_datagram_data(
		h.hostFilterPtr, bytesToModuleBuffer(data))
	runtime.KeepAlive(data)
	return bool(ret)
}

func (h *dymUdpListenerFilterHandle) GetPeerAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_udp_listener_filter_get_peer_address(h.hostFilterPtr, &buf, &port)
	if !bool(ok) {
		return shared.UnsafeEnvoyBuffer{}, 0, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), uint32(port), true
}

func (h *dymUdpListenerFilterHandle) GetLocalAddress() (shared.UnsafeEnvoyBuffer, uint32, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var port C.uint32_t
	ok := C.envoy_dynamic_module_callback_udp_listener_filter_get_local_address(h.hostFilterPtr, &buf, &port)
	if !bool(ok) {
		return shared.UnsafeEnvoyBuffer{}, 0, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), uint32(port), true
}

func (h *dymUdpListenerFilterHandle) SendDatagram(data []byte, peerAddress string, peerPort uint32) bool {
	ret := C.envoy_dynamic_module_callback_udp_listener_filter_send_datagram(
		h.hostFilterPtr,
		bytesToModuleBuffer(data),
		stringToModuleBuffer(peerAddress),
		C.uint32_t(peerPort),
	)
	runtime.KeepAlive(data)
	runtime.KeepAlive(peerAddress)
	return bool(ret)
}

func (h *dymUdpListenerFilterHandle) IncrementCounter(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_udp_listener_filter_increment_counter(
		h.hostFilterPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymUdpListenerFilterHandle) SetGauge(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_udp_listener_filter_set_gauge(
		h.hostFilterPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymUdpListenerFilterHandle) IncrementGauge(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_udp_listener_filter_increment_gauge(
		h.hostFilterPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymUdpListenerFilterHandle) DecrementGauge(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_udp_listener_filter_decrement_gauge(
		h.hostFilterPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymUdpListenerFilterHandle) RecordHistogramValue(id shared.MetricID, value uint64) shared.MetricsResult {
	return shared.MetricsResult(C.envoy_dynamic_module_callback_udp_listener_filter_record_histogram_value(
		h.hostFilterPtr, C.size_t(uint64(id)), C.uint64_t(value)))
}

func (h *dymUdpListenerFilterHandle) GetWorkerIndex() uint32 {
	return uint32(C.envoy_dynamic_module_callback_udp_listener_filter_get_worker_index(h.hostFilterPtr))
}

// =============================================================================
// Event hooks
// =============================================================================

//export envoy_dynamic_module_on_udp_listener_filter_config_new
func envoy_dynamic_module_on_udp_listener_filter_config_new(
	hostConfigPtr C.envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_udp_listener_filter_config_module_ptr {
	nameStr := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesCopy(config)

	configHandle := &dymUdpListenerConfigHandle{hostConfigPtr: hostConfigPtr}
	configFactory := sdk.GetUdpListenerFilterConfigFactory(nameStr)
	if configFactory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load UDP listener filter configuration for %q: no factory registered", []any{nameStr})
		return nil
	}
	factory, err := configFactory.Create(configHandle, configBytes)
	if err != nil {
		hostLog(shared.LogLevelWarn, "Failed to load UDP listener filter configuration for %q: %v", []any{nameStr, err})
		return nil
	}
	if factory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load UDP listener filter configuration for %q: factory returned nil", []any{nameStr})
		return nil
	}
	wrapper := &udpListenerFilterConfigWrapper{factory: factory, configHandle: configHandle}
	configPtr := udpListenerConfigManager.record(wrapper)
	return C.envoy_dynamic_module_type_udp_listener_filter_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_udp_listener_filter_config_destroy
func envoy_dynamic_module_on_udp_listener_filter_config_destroy(
	configPtr C.envoy_dynamic_module_type_udp_listener_filter_config_module_ptr,
) {
	wrapper := udpListenerConfigManager.unwrap(unsafe.Pointer(configPtr))
	if wrapper == nil {
		return
	}
	wrapper.factory.OnDestroy()
	udpListenerConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_udp_listener_filter_new
func envoy_dynamic_module_on_udp_listener_filter_new(
	configPtr C.envoy_dynamic_module_type_udp_listener_filter_config_module_ptr,
	hostFilterPtr C.envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
) C.envoy_dynamic_module_type_udp_listener_filter_module_ptr {
	cfg := udpListenerConfigManager.unwrap(unsafe.Pointer(configPtr))
	if cfg == nil {
		return nil
	}
	handle := &dymUdpListenerFilterHandle{hostFilterPtr: hostFilterPtr}
	handle.plugin = cfg.factory.Create(handle)
	if handle.plugin == nil {
		return nil
	}
	filterPtr := udpListenerFilterManager.record(handle)
	return C.envoy_dynamic_module_type_udp_listener_filter_module_ptr(filterPtr)
}

//export envoy_dynamic_module_on_udp_listener_filter_on_data
func envoy_dynamic_module_on_udp_listener_filter_on_data(
	_ C.envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
	filterPtr C.envoy_dynamic_module_type_udp_listener_filter_module_ptr,
) C.envoy_dynamic_module_type_on_udp_listener_filter_status {
	h := udpListenerFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.plugin == nil || h.destroyed {
		return 0
	}
	return C.envoy_dynamic_module_type_on_udp_listener_filter_status(h.plugin.OnData(h))
}

//export envoy_dynamic_module_on_udp_listener_filter_destroy
func envoy_dynamic_module_on_udp_listener_filter_destroy(
	filterPtr C.envoy_dynamic_module_type_udp_listener_filter_module_ptr,
) {
	h := udpListenerFilterManager.unwrap(unsafe.Pointer(filterPtr))
	if h == nil || h.destroyed {
		return
	}
	h.destroyed = true
	if h.plugin != nil {
		h.plugin.OnDestroy()
	}
	udpListenerFilterManager.remove(unsafe.Pointer(filterPtr))
}
