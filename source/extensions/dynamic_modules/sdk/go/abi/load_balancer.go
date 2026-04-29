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

type lbConfigWrapper struct {
	factory      shared.LoadBalancerFactory
	configHandle *dymLbConfigHandle
}

type lbWrapper = dymLoadBalancerHandle

var lbConfigManager = newManager[lbConfigWrapper]()
var lbManager = newManager[lbWrapper]()

// dymLbConfigHandle implements shared.LoadBalancerConfigHandle using the labeled-metric ABI.
type dymLbConfigHandle struct {
	hostConfigPtr C.envoy_dynamic_module_type_lb_config_envoy_ptr
}

func (h *dymLbConfigHandle) DefineCounter(name string, labelNames []string) (shared.MetricID, shared.MetricsResult) {
	labels := stringSlicesToModuleBuffers(labelNames)
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_lb_config_define_counter(
		h.hostConfigPtr, stringToModuleBuffer(name),
		bufferSlicePtr(labels), C.size_t(len(labels)), &id,
	)
	runtime.KeepAlive(name)
	runtime.KeepAlive(labelNames)
	runtime.KeepAlive(labels)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymLbConfigHandle) IncrementCounter(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_lb_config_increment_counter(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymLbConfigHandle) DefineGauge(name string, labelNames []string) (shared.MetricID, shared.MetricsResult) {
	labels := stringSlicesToModuleBuffers(labelNames)
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_lb_config_define_gauge(
		h.hostConfigPtr, stringToModuleBuffer(name),
		bufferSlicePtr(labels), C.size_t(len(labels)), &id,
	)
	runtime.KeepAlive(name)
	runtime.KeepAlive(labelNames)
	runtime.KeepAlive(labels)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymLbConfigHandle) SetGauge(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_lb_config_set_gauge(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymLbConfigHandle) IncrementGauge(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_lb_config_increment_gauge(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymLbConfigHandle) DecrementGauge(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_lb_config_decrement_gauge(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymLbConfigHandle) DefineHistogram(name string, labelNames []string) (shared.MetricID, shared.MetricsResult) {
	labels := stringSlicesToModuleBuffers(labelNames)
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_lb_config_define_histogram(
		h.hostConfigPtr, stringToModuleBuffer(name),
		bufferSlicePtr(labels), C.size_t(len(labels)), &id,
	)
	runtime.KeepAlive(name)
	runtime.KeepAlive(labelNames)
	runtime.KeepAlive(labels)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymLbConfigHandle) RecordHistogramValue(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_lb_config_record_histogram_value(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

// dymLoadBalancerHandle implements shared.LoadBalancerHandle.
type dymLoadBalancerHandle struct {
	hostLbPtr C.envoy_dynamic_module_type_lb_envoy_ptr

	plugin    shared.LoadBalancer
	destroyed bool
}

func (h *dymLoadBalancerHandle) GetClusterName() shared.UnsafeEnvoyBuffer {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	C.envoy_dynamic_module_callback_lb_get_cluster_name(h.hostLbPtr, &buf)
	if buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf)
}

func (h *dymLoadBalancerHandle) GetHostsCount(priority uint32) uint64 {
	return uint64(C.envoy_dynamic_module_callback_lb_get_hosts_count(h.hostLbPtr, C.uint32_t(priority)))
}

func (h *dymLoadBalancerHandle) GetHealthyHostsCount(priority uint32) uint64 {
	return uint64(C.envoy_dynamic_module_callback_lb_get_healthy_hosts_count(h.hostLbPtr, C.uint32_t(priority)))
}

func (h *dymLoadBalancerHandle) GetDegradedHostsCount(priority uint32) uint64 {
	return uint64(C.envoy_dynamic_module_callback_lb_get_degraded_hosts_count(h.hostLbPtr, C.uint32_t(priority)))
}

func (h *dymLoadBalancerHandle) GetPrioritySetSize() uint64 {
	return uint64(C.envoy_dynamic_module_callback_lb_get_priority_set_size(h.hostLbPtr))
}

func (h *dymLoadBalancerHandle) GetHealthyHostAddress(priority uint32, index uint64) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_lb_get_healthy_host_address(
		h.hostLbPtr, C.uint32_t(priority), C.size_t(index), &buf)
	if !bool(ok) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ok)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymLoadBalancerHandle) GetHealthyHostWeight(priority uint32, index uint64) uint32 {
	return uint32(C.envoy_dynamic_module_callback_lb_get_healthy_host_weight(
		h.hostLbPtr, C.uint32_t(priority), C.size_t(index)))
}

func (h *dymLoadBalancerHandle) GetHostHealth(priority uint32, index uint64) shared.HostHealth {
	return shared.HostHealth(C.envoy_dynamic_module_callback_lb_get_host_health(
		h.hostLbPtr, C.uint32_t(priority), C.size_t(index)))
}

func (h *dymLoadBalancerHandle) GetHostHealthByAddress(address string) (shared.HostHealth, bool) {
	var v C.envoy_dynamic_module_type_host_health
	ok := C.envoy_dynamic_module_callback_lb_get_host_health_by_address(
		h.hostLbPtr, stringToModuleBuffer(address), &v)
	runtime.KeepAlive(address)
	if !bool(ok) {
		return 0, false
	}
	return shared.HostHealth(v), true
}

func (h *dymLoadBalancerHandle) GetHostAddress(priority uint32, index uint64) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_lb_get_host_address(
		h.hostLbPtr, C.uint32_t(priority), C.size_t(index), &buf)
	if !bool(ok) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ok)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymLoadBalancerHandle) GetHostWeight(priority uint32, index uint64) uint32 {
	return uint32(C.envoy_dynamic_module_callback_lb_get_host_weight(
		h.hostLbPtr, C.uint32_t(priority), C.size_t(index)))
}

func (h *dymLoadBalancerHandle) GetHostLocality(priority uint32, index uint64) (shared.UnsafeEnvoyBuffer, shared.UnsafeEnvoyBuffer, shared.UnsafeEnvoyBuffer, bool) {
	var region, zone, subZone C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_lb_get_host_locality(
		h.hostLbPtr, C.uint32_t(priority), C.size_t(index), &region, &zone, &subZone)
	if !bool(ok) {
		return shared.UnsafeEnvoyBuffer{}, shared.UnsafeEnvoyBuffer{}, shared.UnsafeEnvoyBuffer{}, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(region),
		envoyBufferToUnsafeEnvoyBuffer(zone),
		envoyBufferToUnsafeEnvoyBuffer(subZone),
		true
}

func (h *dymLoadBalancerHandle) SetHostData(priority uint32, index uint64, data uintptr) bool {
	return bool(C.envoy_dynamic_module_callback_lb_set_host_data(
		h.hostLbPtr, C.uint32_t(priority), C.size_t(index), C.uintptr_t(data)))
}

func (h *dymLoadBalancerHandle) GetHostData(priority uint32, index uint64) (uintptr, bool) {
	var data C.uintptr_t
	ok := C.envoy_dynamic_module_callback_lb_get_host_data(
		h.hostLbPtr, C.uint32_t(priority), C.size_t(index), &data)
	if !bool(ok) {
		return 0, false
	}
	return uintptr(data), true
}

func (h *dymLoadBalancerHandle) GetHostMetadataString(priority uint32, index uint64, filterName, key string) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_lb_get_host_metadata_string(
		h.hostLbPtr, C.uint32_t(priority), C.size_t(index),
		stringToModuleBuffer(filterName), stringToModuleBuffer(key), &buf)
	runtime.KeepAlive(filterName)
	runtime.KeepAlive(key)
	if !bool(ok) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ok)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymLoadBalancerHandle) GetHostMetadataNumber(priority uint32, index uint64, filterName, key string) (float64, bool) {
	var v C.double
	ok := C.envoy_dynamic_module_callback_lb_get_host_metadata_number(
		h.hostLbPtr, C.uint32_t(priority), C.size_t(index),
		stringToModuleBuffer(filterName), stringToModuleBuffer(key), &v)
	runtime.KeepAlive(filterName)
	runtime.KeepAlive(key)
	if !bool(ok) {
		return 0, false
	}
	return float64(v), true
}

func (h *dymLoadBalancerHandle) GetHostMetadataBool(priority uint32, index uint64, filterName, key string) (bool, bool) {
	var v C.bool
	ok := C.envoy_dynamic_module_callback_lb_get_host_metadata_bool(
		h.hostLbPtr, C.uint32_t(priority), C.size_t(index),
		stringToModuleBuffer(filterName), stringToModuleBuffer(key), &v)
	runtime.KeepAlive(filterName)
	runtime.KeepAlive(key)
	if !bool(ok) {
		return false, false
	}
	return bool(v), true
}

func (h *dymLoadBalancerHandle) GetHostStat(priority uint32, index uint64, stat shared.HostStat) uint64 {
	return uint64(C.envoy_dynamic_module_callback_lb_get_host_stat(
		h.hostLbPtr, C.uint32_t(priority), C.size_t(index),
		C.envoy_dynamic_module_type_host_stat(stat)))
}

func (h *dymLoadBalancerHandle) GetLocalityCount(priority uint32) uint64 {
	return uint64(C.envoy_dynamic_module_callback_lb_get_locality_count(h.hostLbPtr, C.uint32_t(priority)))
}

func (h *dymLoadBalancerHandle) GetLocalityHostCount(priority uint32, localityIndex uint64) uint64 {
	return uint64(C.envoy_dynamic_module_callback_lb_get_locality_host_count(
		h.hostLbPtr, C.uint32_t(priority), C.size_t(localityIndex)))
}

func (h *dymLoadBalancerHandle) GetLocalityHostAddress(priority uint32, localityIndex, hostIndex uint64) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_lb_get_locality_host_address(
		h.hostLbPtr, C.uint32_t(priority), C.size_t(localityIndex), C.size_t(hostIndex), &buf)
	if !bool(ok) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ok)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymLoadBalancerHandle) GetLocalityWeight(priority uint32, localityIndex uint64) uint32 {
	return uint32(C.envoy_dynamic_module_callback_lb_get_locality_weight(
		h.hostLbPtr, C.uint32_t(priority), C.size_t(localityIndex)))
}

func (h *dymLoadBalancerHandle) GetMemberUpdateHostAddress(index uint64, isAdded bool) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_lb_get_member_update_host_address(
		h.hostLbPtr, C.size_t(index), C.bool(isAdded), &buf)
	if !bool(ok) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ok)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

// dymLoadBalancerContext implements shared.LoadBalancerContext, bound to a single ChooseHost
// call.
type dymLoadBalancerContext struct {
	hostCtxPtr C.envoy_dynamic_module_type_lb_context_envoy_ptr
	lbHandle   *dymLoadBalancerHandle // for ShouldSelectAnotherHost
}

func (c *dymLoadBalancerContext) ComputeHashKey() (uint64, bool) {
	var v C.uint64_t
	ok := C.envoy_dynamic_module_callback_lb_context_compute_hash_key(c.hostCtxPtr, &v)
	if !bool(ok) {
		return 0, false
	}
	return uint64(v), true
}

func (c *dymLoadBalancerContext) GetDownstreamHeadersSize() uint64 {
	return uint64(C.envoy_dynamic_module_callback_lb_context_get_downstream_headers_size(c.hostCtxPtr))
}

func (c *dymLoadBalancerContext) GetDownstreamHeaders() [][2]shared.UnsafeEnvoyBuffer {
	size := C.envoy_dynamic_module_callback_lb_context_get_downstream_headers_size(c.hostCtxPtr)
	if size == 0 {
		return nil
	}
	hdrs := make([]C.envoy_dynamic_module_type_envoy_http_header, int(size))
	if !bool(C.envoy_dynamic_module_callback_lb_context_get_downstream_headers(
		c.hostCtxPtr, unsafe.SliceData(hdrs))) {
		return nil
	}
	out := envoyHttpHeaderSliceToUnsafeHeaderSlice(hdrs)
	runtime.KeepAlive(hdrs)
	return out
}

func (c *dymLoadBalancerContext) GetDownstreamHeader(key string, index uint64) (shared.UnsafeEnvoyBuffer, uint64, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var total C.size_t
	ok := C.envoy_dynamic_module_callback_lb_context_get_downstream_header(
		c.hostCtxPtr, stringToModuleBuffer(key), &buf, C.size_t(index), &total)
	runtime.KeepAlive(key)
	if !bool(ok) {
		return shared.UnsafeEnvoyBuffer{}, uint64(total), false
	}
	if buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, uint64(total), true
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), uint64(total), true
}

func (c *dymLoadBalancerContext) GetHostSelectionRetryCount() uint32 {
	return uint32(C.envoy_dynamic_module_callback_lb_context_get_host_selection_retry_count(c.hostCtxPtr))
}

func (c *dymLoadBalancerContext) ShouldSelectAnotherHost(_ shared.LoadBalancerHandle, priority uint32, index uint64) bool {
	if c.lbHandle == nil {
		return false
	}
	return bool(C.envoy_dynamic_module_callback_lb_context_should_select_another_host(
		c.lbHandle.hostLbPtr, c.hostCtxPtr, C.uint32_t(priority), C.size_t(index)))
}

func (c *dymLoadBalancerContext) GetOverrideHost() (shared.UnsafeEnvoyBuffer, bool, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var strict C.bool
	ok := C.envoy_dynamic_module_callback_lb_context_get_override_host(c.hostCtxPtr, &buf, &strict)
	if !bool(ok) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(strict), bool(ok)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), bool(strict), true
}

// =============================================================================
// Event hooks
// =============================================================================

//export envoy_dynamic_module_on_lb_config_new
func envoy_dynamic_module_on_lb_config_new(
	hostConfigPtr C.envoy_dynamic_module_type_lb_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_lb_config_module_ptr {
	nameStr := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesCopy(config)

	configHandle := &dymLbConfigHandle{hostConfigPtr: hostConfigPtr}
	configFactory := sdk.GetLoadBalancerConfigFactory(nameStr)
	if configFactory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load LB configuration: no factory for %s", []any{nameStr})
		return nil
	}
	factory, err := configFactory.Create(configHandle, configBytes)
	if err != nil || factory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load LB configuration: %v", []any{err})
		return nil
	}
	wrapper := &lbConfigWrapper{factory: factory, configHandle: configHandle}
	configPtr := lbConfigManager.record(wrapper)
	return C.envoy_dynamic_module_type_lb_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_lb_config_destroy
func envoy_dynamic_module_on_lb_config_destroy(
	configPtr C.envoy_dynamic_module_type_lb_config_module_ptr,
) {
	w := lbConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil {
		return
	}
	w.factory.OnDestroy()
	lbConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_lb_new
func envoy_dynamic_module_on_lb_new(
	configPtr C.envoy_dynamic_module_type_lb_config_module_ptr,
	hostLbPtr C.envoy_dynamic_module_type_lb_envoy_ptr,
) C.envoy_dynamic_module_type_lb_module_ptr {
	cfg := lbConfigManager.unwrap(unsafe.Pointer(configPtr))
	if cfg == nil {
		return nil
	}
	handle := &dymLoadBalancerHandle{hostLbPtr: hostLbPtr}
	handle.plugin = cfg.factory.Create(handle)
	if handle.plugin == nil {
		return nil
	}
	lbPtr := lbManager.record(handle)
	return C.envoy_dynamic_module_type_lb_module_ptr(lbPtr)
}

//export envoy_dynamic_module_on_lb_choose_host
func envoy_dynamic_module_on_lb_choose_host(
	hostLbPtr C.envoy_dynamic_module_type_lb_envoy_ptr,
	lbPtr C.envoy_dynamic_module_type_lb_module_ptr,
	hostCtxPtr C.envoy_dynamic_module_type_lb_context_envoy_ptr,
	resultPriority *C.uint32_t,
	resultIndex *C.uint32_t,
) C.bool {
	w := lbManager.unwrap(unsafe.Pointer(lbPtr))
	if w == nil || w.plugin == nil || w.destroyed {
		return false
	}
	// Refresh the lb pointer in case Envoy gives us a fresh one per call.
	w.hostLbPtr = hostLbPtr
	var ctx shared.LoadBalancerContext
	if hostCtxPtr != nil {
		ctx = &dymLoadBalancerContext{hostCtxPtr: hostCtxPtr, lbHandle: w}
	}
	sel, ok := w.plugin.ChooseHost(w, ctx)
	if !ok {
		return false
	}
	*resultPriority = C.uint32_t(sel.Priority)
	*resultIndex = C.uint32_t(sel.Index)
	return true
}

//export envoy_dynamic_module_on_lb_on_host_membership_update
func envoy_dynamic_module_on_lb_on_host_membership_update(
	hostLbPtr C.envoy_dynamic_module_type_lb_envoy_ptr,
	lbPtr C.envoy_dynamic_module_type_lb_module_ptr,
	numHostsAdded C.size_t,
	numHostsRemoved C.size_t,
) {
	w := lbManager.unwrap(unsafe.Pointer(lbPtr))
	if w == nil || w.plugin == nil || w.destroyed {
		return
	}
	w.hostLbPtr = hostLbPtr
	w.plugin.OnHostMembershipUpdate(w, uint64(numHostsAdded), uint64(numHostsRemoved))
}

//export envoy_dynamic_module_on_lb_destroy
func envoy_dynamic_module_on_lb_destroy(
	lbPtr C.envoy_dynamic_module_type_lb_module_ptr,
) {
	w := lbManager.unwrap(unsafe.Pointer(lbPtr))
	if w == nil || w.destroyed {
		return
	}
	w.destroyed = true
	if w.plugin != nil {
		w.plugin.OnDestroy()
	}
	lbManager.remove(unsafe.Pointer(lbPtr))
}
