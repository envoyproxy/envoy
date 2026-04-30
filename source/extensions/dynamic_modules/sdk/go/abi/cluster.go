package abi

/*
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup
#cgo linux LDFLAGS: -Wl,--unresolved-symbols=ignore-all
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../../abi/abi.h"

// cgoClusterInvokeEventCb invokes Envoy's cluster-shutdown completion callback. Each cgo
// file has its own preamble, so we duplicate the wrapper from bootstrap.go's preamble here
// to keep the two files independently compilable.
static inline void cgoClusterInvokeEventCb(envoy_dynamic_module_type_event_cb cb, void* context) {
    if (cb != NULL) {
        cb(context);
    }
}
*/
import "C"
import (
	"runtime"
	"sync"
	"sync/atomic"
	"unsafe"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

type clusterConfigWrapper struct {
	hostConfigPtr C.envoy_dynamic_module_type_cluster_config_envoy_ptr
	factory       shared.ClusterFactory
	configHandle  *dymClusterConfigHandle
}

type clusterWrapper struct {
	hostClusterPtr C.envoy_dynamic_module_type_cluster_envoy_ptr
	cluster        shared.Cluster
	configRef      *clusterConfigWrapper

	scheduler *dymScheduler

	calloutMu        sync.Mutex
	calloutCallbacks map[uint64]shared.HttpCalloutCallback

	shutdownMu         sync.Mutex
	shutdownCompletion *clusterShutdownCompletion

	// destroyed is set during the destroy hook so late callbacks (scheduled tasks,
	// callouts that fire as the cluster is being torn down) become no-ops instead of
	// re-entering user code.
	destroyed bool
}

type clusterShutdownCompletion struct {
	cb      C.envoy_dynamic_module_type_event_cb
	context unsafe.Pointer
	done    atomic.Bool
}

type clusterLbWrapper struct {
	hostLbPtr  C.envoy_dynamic_module_type_cluster_lb_envoy_ptr
	lb         shared.ClusterLoadBalancer
	clusterRef *clusterWrapper

	asyncMu      sync.Mutex
	asyncHandles map[*dymClusterAsyncCompletion]struct{}

	// destroyed is set during the destroy hook to prevent late callbacks (cancel,
	// host-membership updates) from re-entering user code after OnDestroy.
	destroyed bool
}

var clusterConfigManager = newManager[clusterConfigWrapper]()
var clusterManager = newManager[clusterWrapper]()
var clusterLbManager = newManager[clusterLbWrapper]()
var clusterAsyncCompletionManager = newManager[dymClusterAsyncCompletion]()

// dymClusterConfigHandle implements shared.ClusterConfigHandle (labeled metrics).
type dymClusterConfigHandle struct {
	hostConfigPtr C.envoy_dynamic_module_type_cluster_config_envoy_ptr
}

func (h *dymClusterConfigHandle) DefineCounter(name string, labelNames []string) (shared.MetricID, shared.MetricsResult) {
	labels := stringSlicesToModuleBuffers(labelNames)
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_cluster_config_define_counter(
		h.hostConfigPtr, stringToModuleBuffer(name),
		bufferSlicePtr(labels), C.size_t(len(labels)), &id,
	)
	runtime.KeepAlive(name)
	runtime.KeepAlive(labelNames)
	runtime.KeepAlive(labels)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymClusterConfigHandle) IncrementCounter(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_cluster_config_increment_counter(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymClusterConfigHandle) DefineGauge(name string, labelNames []string) (shared.MetricID, shared.MetricsResult) {
	labels := stringSlicesToModuleBuffers(labelNames)
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_cluster_config_define_gauge(
		h.hostConfigPtr, stringToModuleBuffer(name),
		bufferSlicePtr(labels), C.size_t(len(labels)), &id,
	)
	runtime.KeepAlive(name)
	runtime.KeepAlive(labelNames)
	runtime.KeepAlive(labels)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymClusterConfigHandle) SetGauge(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_cluster_config_set_gauge(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymClusterConfigHandle) IncrementGauge(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_cluster_config_increment_gauge(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymClusterConfigHandle) DecrementGauge(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_cluster_config_decrement_gauge(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymClusterConfigHandle) DefineHistogram(name string, labelNames []string) (shared.MetricID, shared.MetricsResult) {
	labels := stringSlicesToModuleBuffers(labelNames)
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_cluster_config_define_histogram(
		h.hostConfigPtr, stringToModuleBuffer(name),
		bufferSlicePtr(labels), C.size_t(len(labels)), &id,
	)
	runtime.KeepAlive(name)
	runtime.KeepAlive(labelNames)
	runtime.KeepAlive(labels)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymClusterConfigHandle) RecordHistogramValue(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_cluster_config_record_histogram_value(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

// dymClusterHandle implements shared.ClusterHandle.
type dymClusterHandle struct {
	wrapper *clusterWrapper
}

func (h *dymClusterHandle) PreInitComplete() {
	C.envoy_dynamic_module_callback_cluster_pre_init_complete(h.wrapper.hostClusterPtr)
}

func (h *dymClusterHandle) AddHosts(priority uint32, specs []shared.ClusterHostSpec) ([]shared.ClusterHost, bool) {
	if len(specs) == 0 {
		return nil, true
	}
	addresses := make([]C.envoy_dynamic_module_type_module_buffer, len(specs))
	weights := make([]C.uint32_t, len(specs))
	regions := make([]C.envoy_dynamic_module_type_module_buffer, len(specs))
	zones := make([]C.envoy_dynamic_module_type_module_buffer, len(specs))
	subZones := make([]C.envoy_dynamic_module_type_module_buffer, len(specs))

	// metadataPairsPerHost MUST be the same for all specs. Use the max length and pad shorter
	// ones; if any spec has a different number of triples than another, fail (caller error).
	pairsPer := uint64(0)
	for i := range specs {
		n := uint64(len(specs[i].MetadataPairs)) / 3
		if i == 0 {
			pairsPer = n
		} else if n != pairsPer {
			return nil, false
		}
	}
	var metadataPairs []C.envoy_dynamic_module_type_module_buffer
	if pairsPer > 0 {
		metadataPairs = make([]C.envoy_dynamic_module_type_module_buffer, 0, len(specs)*int(pairsPer)*3)
	}

	for i, s := range specs {
		addresses[i] = stringToModuleBuffer(s.Address)
		weights[i] = C.uint32_t(s.Weight)
		regions[i] = stringToModuleBuffer(s.Region)
		zones[i] = stringToModuleBuffer(s.Zone)
		subZones[i] = stringToModuleBuffer(s.SubZone)
		for _, p := range s.MetadataPairs {
			metadataPairs = append(metadataPairs, stringToModuleBuffer(p))
		}
	}

	hostPtrs := make([]C.envoy_dynamic_module_type_cluster_host_envoy_ptr, len(specs))
	var metadataPtr *C.envoy_dynamic_module_type_module_buffer
	if len(metadataPairs) > 0 {
		metadataPtr = unsafe.SliceData(metadataPairs)
	}
	ok := C.envoy_dynamic_module_callback_cluster_add_hosts(
		h.wrapper.hostClusterPtr,
		C.uint32_t(priority),
		unsafe.SliceData(addresses),
		unsafe.SliceData(weights),
		unsafe.SliceData(regions),
		unsafe.SliceData(zones),
		unsafe.SliceData(subZones),
		metadataPtr,
		C.size_t(pairsPer),
		C.size_t(len(specs)),
		unsafe.SliceData(hostPtrs),
	)
	runtime.KeepAlive(specs)
	runtime.KeepAlive(addresses)
	runtime.KeepAlive(weights)
	runtime.KeepAlive(regions)
	runtime.KeepAlive(zones)
	runtime.KeepAlive(subZones)
	runtime.KeepAlive(metadataPairs)
	if !bool(ok) {
		return nil, false
	}
	out := make([]shared.ClusterHost, len(specs))
	for i := range hostPtrs {
		out[i] = shared.UnsafeClusterHost(unsafe.Pointer(hostPtrs[i]))
	}
	return out, true
}

func (h *dymClusterHandle) RemoveHosts(hosts []shared.ClusterHost) uint64 {
	if len(hosts) == 0 {
		return 0
	}
	cHosts := make([]C.envoy_dynamic_module_type_cluster_host_envoy_ptr, len(hosts))
	for i, host := range hosts {
		cHosts[i] = C.envoy_dynamic_module_type_cluster_host_envoy_ptr(shared.UnsafeClusterHostPtr(host))
	}
	n := C.envoy_dynamic_module_callback_cluster_remove_hosts(
		h.wrapper.hostClusterPtr, unsafe.SliceData(cHosts), C.size_t(len(cHosts)))
	runtime.KeepAlive(cHosts)
	return uint64(n)
}

func (h *dymClusterHandle) UpdateHostHealth(host shared.ClusterHost, status shared.HostHealth) bool {
	return bool(C.envoy_dynamic_module_callback_cluster_update_host_health(
		h.wrapper.hostClusterPtr,
		C.envoy_dynamic_module_type_cluster_host_envoy_ptr(shared.UnsafeClusterHostPtr(host)),
		C.envoy_dynamic_module_type_host_health(status)))
}

func (h *dymClusterHandle) FindHostByAddress(address string) shared.ClusterHost {
	hp := C.envoy_dynamic_module_callback_cluster_find_host_by_address(
		h.wrapper.hostClusterPtr, stringToModuleBuffer(address))
	runtime.KeepAlive(address)
	return shared.UnsafeClusterHost(unsafe.Pointer(hp))
}

func (h *dymClusterHandle) HttpCallout(
	clusterName string, headers [][2]string, body []byte, timeoutMs uint64,
	cb shared.HttpCalloutCallback,
) (shared.HttpCalloutInitResult, uint64) {
	headerViews := headersToModuleHttpHeaderSlice(headers)
	var calloutID C.uint64_t
	result := C.envoy_dynamic_module_callback_cluster_http_callout(
		h.wrapper.hostClusterPtr,
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
	h.wrapper.calloutMu.Lock()
	if h.wrapper.calloutCallbacks == nil {
		h.wrapper.calloutCallbacks = make(map[uint64]shared.HttpCalloutCallback)
	}
	h.wrapper.calloutCallbacks[uint64(calloutID)] = cb
	h.wrapper.calloutMu.Unlock()
	return goResult, uint64(calloutID)
}

func (h *dymClusterHandle) NewScheduler() shared.Scheduler {
	if h.wrapper.scheduler == nil {
		schedulerPtr := C.envoy_dynamic_module_callback_cluster_scheduler_new(h.wrapper.hostClusterPtr)
		h.wrapper.scheduler = newDymScheduler(
			unsafe.Pointer(schedulerPtr),
			func(p unsafe.Pointer, taskID C.uint64_t) {
				C.envoy_dynamic_module_callback_cluster_scheduler_commit(
					(C.envoy_dynamic_module_type_cluster_scheduler_module_ptr)(p), taskID)
			},
			func(p unsafe.Pointer) {
				C.envoy_dynamic_module_callback_cluster_scheduler_delete(
					(C.envoy_dynamic_module_type_cluster_scheduler_module_ptr)(p))
			},
		)
		// Finalizer is a fallback; the destroy hook should call close() synchronously.
		runtime.SetFinalizer(h.wrapper.scheduler, func(s *dymScheduler) { s.close() })
	}
	return h.wrapper.scheduler
}

// dymClusterLoadBalancerHandle implements shared.ClusterLoadBalancerHandle.
type dymClusterLoadBalancerHandle struct {
	wrapper *clusterLbWrapper
}

func (h *dymClusterLoadBalancerHandle) GetClusterName() shared.UnsafeEnvoyBuffer {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	C.envoy_dynamic_module_callback_cluster_lb_get_cluster_name(h.wrapper.hostLbPtr, &buf)
	if buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf)
}

func (h *dymClusterLoadBalancerHandle) GetHostsCount(priority uint32) uint64 {
	return uint64(C.envoy_dynamic_module_callback_cluster_lb_get_hosts_count(h.wrapper.hostLbPtr, C.uint32_t(priority)))
}

func (h *dymClusterLoadBalancerHandle) GetHealthyHostCount(priority uint32) uint64 {
	return uint64(C.envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(h.wrapper.hostLbPtr, C.uint32_t(priority)))
}

func (h *dymClusterLoadBalancerHandle) GetDegradedHostsCount(priority uint32) uint64 {
	return uint64(C.envoy_dynamic_module_callback_cluster_lb_get_degraded_hosts_count(h.wrapper.hostLbPtr, C.uint32_t(priority)))
}

func (h *dymClusterLoadBalancerHandle) GetPrioritySetSize() uint64 {
	return uint64(C.envoy_dynamic_module_callback_cluster_lb_get_priority_set_size(h.wrapper.hostLbPtr))
}

func (h *dymClusterLoadBalancerHandle) GetHealthyHost(priority uint32, index uint64) shared.ClusterHost {
	hp := C.envoy_dynamic_module_callback_cluster_lb_get_healthy_host(h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(index))
	return shared.UnsafeClusterHost(unsafe.Pointer(hp))
}

func (h *dymClusterLoadBalancerHandle) GetHealthyHostAddress(priority uint32, index uint64) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_cluster_lb_get_healthy_host_address(h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(index), &buf)
	if !bool(ok) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ok)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymClusterLoadBalancerHandle) GetHealthyHostWeight(priority uint32, index uint64) uint32 {
	return uint32(C.envoy_dynamic_module_callback_cluster_lb_get_healthy_host_weight(h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(index)))
}

func (h *dymClusterLoadBalancerHandle) GetHost(priority uint32, index uint64) shared.ClusterHost {
	hp := C.envoy_dynamic_module_callback_cluster_lb_get_host(h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(index))
	return shared.UnsafeClusterHost(unsafe.Pointer(hp))
}

func (h *dymClusterLoadBalancerHandle) GetHostAddress(priority uint32, index uint64) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_cluster_lb_get_host_address(h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(index), &buf)
	if !bool(ok) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ok)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymClusterLoadBalancerHandle) GetHostWeight(priority uint32, index uint64) uint32 {
	return uint32(C.envoy_dynamic_module_callback_cluster_lb_get_host_weight(h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(index)))
}

func (h *dymClusterLoadBalancerHandle) GetHostHealth(priority uint32, index uint64) shared.HostHealth {
	return shared.HostHealth(C.envoy_dynamic_module_callback_cluster_lb_get_host_health(h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(index)))
}

func (h *dymClusterLoadBalancerHandle) GetHostHealthByAddress(address string) (shared.HostHealth, bool) {
	var v C.envoy_dynamic_module_type_host_health
	ok := C.envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address(h.wrapper.hostLbPtr, stringToModuleBuffer(address), &v)
	runtime.KeepAlive(address)
	if !bool(ok) {
		return 0, false
	}
	return shared.HostHealth(v), true
}

func (h *dymClusterLoadBalancerHandle) GetHostStat(priority uint32, index uint64, stat shared.HostStat) uint64 {
	return uint64(C.envoy_dynamic_module_callback_cluster_lb_get_host_stat(
		h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(index),
		C.envoy_dynamic_module_type_host_stat(stat)))
}

func (h *dymClusterLoadBalancerHandle) GetHostLocality(priority uint32, index uint64) (shared.UnsafeEnvoyBuffer, shared.UnsafeEnvoyBuffer, shared.UnsafeEnvoyBuffer, bool) {
	var region, zone, subZone C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_cluster_lb_get_host_locality(
		h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(index), &region, &zone, &subZone)
	if !bool(ok) {
		return shared.UnsafeEnvoyBuffer{}, shared.UnsafeEnvoyBuffer{}, shared.UnsafeEnvoyBuffer{}, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(region),
		envoyBufferToUnsafeEnvoyBuffer(zone),
		envoyBufferToUnsafeEnvoyBuffer(subZone),
		true
}

func (h *dymClusterLoadBalancerHandle) FindHostByAddress(address string) shared.ClusterHost {
	hp := C.envoy_dynamic_module_callback_cluster_lb_find_host_by_address(h.wrapper.hostLbPtr, stringToModuleBuffer(address))
	runtime.KeepAlive(address)
	return shared.UnsafeClusterHost(unsafe.Pointer(hp))
}

func (h *dymClusterLoadBalancerHandle) SetHostData(priority uint32, index uint64, data uintptr) bool {
	return bool(C.envoy_dynamic_module_callback_cluster_lb_set_host_data(
		h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(index), C.uintptr_t(data)))
}

func (h *dymClusterLoadBalancerHandle) GetHostData(priority uint32, index uint64) (uintptr, bool) {
	var data C.uintptr_t
	ok := C.envoy_dynamic_module_callback_cluster_lb_get_host_data(
		h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(index), &data)
	if !bool(ok) {
		return 0, false
	}
	return uintptr(data), true
}

func (h *dymClusterLoadBalancerHandle) GetHostMetadataString(priority uint32, index uint64, filterName, key string) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string(
		h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(index),
		stringToModuleBuffer(filterName), stringToModuleBuffer(key), &buf)
	runtime.KeepAlive(filterName)
	runtime.KeepAlive(key)
	if !bool(ok) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ok)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymClusterLoadBalancerHandle) GetHostMetadataNumber(priority uint32, index uint64, filterName, key string) (float64, bool) {
	var v C.double
	ok := C.envoy_dynamic_module_callback_cluster_lb_get_host_metadata_number(
		h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(index),
		stringToModuleBuffer(filterName), stringToModuleBuffer(key), &v)
	runtime.KeepAlive(filterName)
	runtime.KeepAlive(key)
	if !bool(ok) {
		return 0, false
	}
	return float64(v), true
}

func (h *dymClusterLoadBalancerHandle) GetHostMetadataBool(priority uint32, index uint64, filterName, key string) (bool, bool) {
	var v C.bool
	ok := C.envoy_dynamic_module_callback_cluster_lb_get_host_metadata_bool(
		h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(index),
		stringToModuleBuffer(filterName), stringToModuleBuffer(key), &v)
	runtime.KeepAlive(filterName)
	runtime.KeepAlive(key)
	if !bool(ok) {
		return false, false
	}
	return bool(v), true
}

func (h *dymClusterLoadBalancerHandle) GetLocalityCount(priority uint32) uint64 {
	return uint64(C.envoy_dynamic_module_callback_cluster_lb_get_locality_count(h.wrapper.hostLbPtr, C.uint32_t(priority)))
}

func (h *dymClusterLoadBalancerHandle) GetLocalityHostCount(priority uint32, localityIndex uint64) uint64 {
	return uint64(C.envoy_dynamic_module_callback_cluster_lb_get_locality_host_count(
		h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(localityIndex)))
}

func (h *dymClusterLoadBalancerHandle) GetLocalityHostAddress(priority uint32, localityIndex, hostIndex uint64) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_cluster_lb_get_locality_host_address(
		h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(localityIndex), C.size_t(hostIndex), &buf)
	if !bool(ok) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ok)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (h *dymClusterLoadBalancerHandle) GetLocalityWeight(priority uint32, localityIndex uint64) uint32 {
	return uint32(C.envoy_dynamic_module_callback_cluster_lb_get_locality_weight(
		h.wrapper.hostLbPtr, C.uint32_t(priority), C.size_t(localityIndex)))
}

func (h *dymClusterLoadBalancerHandle) GetMemberUpdateHostAddress(index uint64, isAdded bool) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_cluster_lb_get_member_update_host_address(
		h.wrapper.hostLbPtr, C.size_t(index), C.bool(isAdded), &buf)
	if !bool(ok) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ok)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

// dymClusterLbContext implements shared.ClusterLoadBalancerContext.
type dymClusterLbContext struct {
	hostCtxPtr C.envoy_dynamic_module_type_cluster_lb_context_envoy_ptr
	lbWrapper  *clusterLbWrapper
}

func (c *dymClusterLbContext) ComputeHashKey() (uint64, bool) {
	var v C.uint64_t
	ok := C.envoy_dynamic_module_callback_cluster_lb_context_compute_hash_key(c.hostCtxPtr, &v)
	if !bool(ok) {
		return 0, false
	}
	return uint64(v), true
}

func (c *dymClusterLbContext) GetDownstreamHeadersSize() uint64 {
	return uint64(C.envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers_size(c.hostCtxPtr))
}

func (c *dymClusterLbContext) GetDownstreamHeaders() [][2]shared.UnsafeEnvoyBuffer {
	size := C.envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers_size(c.hostCtxPtr)
	if size == 0 {
		return nil
	}
	hdrs := make([]C.envoy_dynamic_module_type_envoy_http_header, int(size))
	if !bool(C.envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers(c.hostCtxPtr, unsafe.SliceData(hdrs))) {
		return nil
	}
	out := envoyHttpHeaderSliceToUnsafeHeaderSlice(hdrs)
	runtime.KeepAlive(hdrs)
	return out
}

func (c *dymClusterLbContext) GetDownstreamHeader(key string, index uint64) (shared.UnsafeEnvoyBuffer, uint64, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var total C.size_t
	ok := C.envoy_dynamic_module_callback_cluster_lb_context_get_downstream_header(
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

func (c *dymClusterLbContext) GetHostSelectionRetryCount() uint32 {
	return uint32(C.envoy_dynamic_module_callback_cluster_lb_context_get_host_selection_retry_count(c.hostCtxPtr))
}

func (c *dymClusterLbContext) ShouldSelectAnotherHost(_ shared.ClusterLoadBalancerHandle, priority uint32, index uint64) bool {
	if c.lbWrapper == nil {
		return false
	}
	return bool(C.envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host(
		c.lbWrapper.hostLbPtr, c.hostCtxPtr, C.uint32_t(priority), C.size_t(index)))
}

func (c *dymClusterLbContext) GetOverrideHost() (shared.UnsafeEnvoyBuffer, bool, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	var strict C.bool
	ok := C.envoy_dynamic_module_callback_cluster_lb_context_get_override_host(c.hostCtxPtr, &buf, &strict)
	if !bool(ok) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(strict), bool(ok)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), bool(strict), true
}

func (c *dymClusterLbContext) GetDownstreamConnectionSNI() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_cluster_lb_context_get_downstream_connection_sni(c.hostCtxPtr, &buf)
	if !bool(ok) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ok)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

// dymClusterAsyncCompletion is the SDK-provided shared.ClusterAsyncCompletion handed to
// ClusterLoadBalancer.ChooseHost. The module calls Complete (possibly from another goroutine)
// when async host selection finishes; the SDK then dispatches to Envoy's async-completion
// callback.
//
// The wrapper carries enough state to (a) deliver the result to Envoy, (b) cancel itself
// from both the per-LB tracking map and the global async manager, and (c) coordinate with
// the cancel path so completion-after-cancel and double-completion are safe no-ops.
type dymClusterAsyncCompletion struct {
	lbWrapper  *clusterLbWrapper
	hostCtxPtr C.envoy_dynamic_module_type_cluster_lb_context_envoy_ptr
	// userSelection is the module's optional ClusterAsyncHostSelection; Cancel is dispatched
	// to it when the SDK observes a cancellation from Envoy. nil if the module returned
	// nil for the async handle.
	userSelection shared.ClusterAsyncHostSelection
	// managerPtr is this completion's key in clusterAsyncCompletionManager. Stored so
	// Complete (called from arbitrary goroutines) can remove itself.
	managerPtr unsafe.Pointer
	// done is set on first Complete OR Cancel; further calls in either direction are
	// no-ops. This makes Complete↔Cancel safe regardless of order.
	done atomic.Bool
}

func (a *dymClusterAsyncCompletion) Complete(host shared.ClusterHost, details string) {
	if a.done.Swap(true) {
		return
	}
	if a.lbWrapper == nil {
		return
	}
	C.envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete(
		a.lbWrapper.hostLbPtr,
		a.hostCtxPtr,
		C.envoy_dynamic_module_type_cluster_host_envoy_ptr(shared.UnsafeClusterHostPtr(host)),
		stringToModuleBuffer(details),
	)
	runtime.KeepAlive(details)
	// Drop from per-LB tracking and from the global async manager so the wrapper can be GC'd.
	a.lbWrapper.asyncMu.Lock()
	delete(a.lbWrapper.asyncHandles, a)
	a.lbWrapper.asyncMu.Unlock()
	if a.managerPtr != nil {
		clusterAsyncCompletionManager.remove(a.managerPtr)
	}
}

// =============================================================================
// Event hooks
// =============================================================================

//export envoy_dynamic_module_on_cluster_config_new
func envoy_dynamic_module_on_cluster_config_new(
	hostConfigPtr C.envoy_dynamic_module_type_cluster_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_cluster_config_module_ptr {
	nameStr := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesCopy(config)

	configHandle := &dymClusterConfigHandle{hostConfigPtr: hostConfigPtr}
	configFactory := sdk.GetClusterConfigFactory(nameStr)
	if configFactory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load cluster configuration for %q: no factory registered", []any{nameStr})
		return nil
	}
	factory, err := configFactory.Create(configHandle, configBytes)
	if err != nil {
		hostLog(shared.LogLevelWarn, "Failed to load cluster configuration for %q: %v", []any{nameStr, err})
		return nil
	}
	if factory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load cluster configuration for %q: factory returned nil", []any{nameStr})
		return nil
	}
	wrapper := &clusterConfigWrapper{
		hostConfigPtr: hostConfigPtr,
		factory:       factory,
		configHandle:  configHandle,
	}
	configPtr := clusterConfigManager.record(wrapper)
	return C.envoy_dynamic_module_type_cluster_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_cluster_config_destroy
func envoy_dynamic_module_on_cluster_config_destroy(
	configPtr C.envoy_dynamic_module_type_cluster_config_module_ptr,
) {
	w := clusterConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil {
		return
	}
	w.factory.OnDestroy()
	clusterConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_cluster_new
func envoy_dynamic_module_on_cluster_new(
	configPtr C.envoy_dynamic_module_type_cluster_config_module_ptr,
	hostClusterPtr C.envoy_dynamic_module_type_cluster_envoy_ptr,
) C.envoy_dynamic_module_type_cluster_module_ptr {
	cfg := clusterConfigManager.unwrap(unsafe.Pointer(configPtr))
	if cfg == nil {
		return nil
	}
	wrapper := &clusterWrapper{
		hostClusterPtr: hostClusterPtr,
		configRef:      cfg,
	}
	cluster := cfg.factory.Create(cfg.configHandle)
	if cluster == nil {
		return nil
	}
	wrapper.cluster = cluster
	clusterPtr := clusterManager.record(wrapper)
	return C.envoy_dynamic_module_type_cluster_module_ptr(clusterPtr)
}

// Init is the first lifecycle callback after _new; it must not see a destroyed wrapper
// (Envoy serializes _new -> _init -> _destroy on the main thread). The destroyed-flag
// guards on subsequent hooks below cover the case where a late callback races with the
// destroy hook running concurrently on the same thread.
//
//export envoy_dynamic_module_on_cluster_init
func envoy_dynamic_module_on_cluster_init(
	hostClusterPtr C.envoy_dynamic_module_type_cluster_envoy_ptr,
	clusterPtr C.envoy_dynamic_module_type_cluster_module_ptr,
) {
	w := clusterManager.unwrap(unsafe.Pointer(clusterPtr))
	if w == nil || w.cluster == nil {
		return
	}
	w.hostClusterPtr = hostClusterPtr
	w.cluster.OnInit(&dymClusterHandle{wrapper: w})
}

//export envoy_dynamic_module_on_cluster_destroy
func envoy_dynamic_module_on_cluster_destroy(
	clusterPtr C.envoy_dynamic_module_type_cluster_module_ptr,
) {
	w := clusterManager.unwrap(unsafe.Pointer(clusterPtr))
	if w == nil || w.destroyed {
		return
	}
	w.destroyed = true
	if w.cluster != nil {
		w.cluster.OnDestroy()
	}
	if w.scheduler != nil {
		// See bootstrap config destroy for why we close synchronously.
		w.scheduler.close()
		w.scheduler = nil
	}
	clusterManager.remove(unsafe.Pointer(clusterPtr))
}

//export envoy_dynamic_module_on_cluster_lb_new
func envoy_dynamic_module_on_cluster_lb_new(
	clusterPtr C.envoy_dynamic_module_type_cluster_module_ptr,
	hostLbPtr C.envoy_dynamic_module_type_cluster_lb_envoy_ptr,
) C.envoy_dynamic_module_type_cluster_lb_module_ptr {
	w := clusterManager.unwrap(unsafe.Pointer(clusterPtr))
	if w == nil || w.cluster == nil {
		return nil
	}
	lbWrapper := &clusterLbWrapper{
		hostLbPtr:    hostLbPtr,
		clusterRef:   w,
		asyncHandles: make(map[*dymClusterAsyncCompletion]struct{}),
	}
	handle := &dymClusterLoadBalancerHandle{wrapper: lbWrapper}
	lb := w.cluster.NewLoadBalancer(handle)
	if lb == nil {
		return nil
	}
	lbWrapper.lb = lb
	lbPtr := clusterLbManager.record(lbWrapper)
	return C.envoy_dynamic_module_type_cluster_lb_module_ptr(lbPtr)
}

//export envoy_dynamic_module_on_cluster_lb_destroy
func envoy_dynamic_module_on_cluster_lb_destroy(
	lbPtr C.envoy_dynamic_module_type_cluster_lb_module_ptr,
) {
	w := clusterLbManager.unwrap(unsafe.Pointer(lbPtr))
	if w == nil || w.destroyed {
		return
	}
	w.destroyed = true
	if w.lb != nil {
		w.lb.OnDestroy()
	}
	clusterLbManager.remove(unsafe.Pointer(lbPtr))
}

//export envoy_dynamic_module_on_cluster_lb_choose_host
func envoy_dynamic_module_on_cluster_lb_choose_host(
	lbPtr C.envoy_dynamic_module_type_cluster_lb_module_ptr,
	hostCtxPtr C.envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
	hostOut *C.envoy_dynamic_module_type_cluster_host_envoy_ptr,
	asyncOut *C.envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr,
) {
	w := clusterLbManager.unwrap(unsafe.Pointer(lbPtr))
	if w == nil || w.lb == nil || w.destroyed {
		*hostOut = nil
		*asyncOut = nil
		return
	}
	ctx := &dymClusterLbContext{hostCtxPtr: hostCtxPtr, lbWrapper: w}
	// Pre-allocate the SDK completion handle so the module always has a real callable
	// object to store; if the module returns sync, we discard it without registering.
	completion := &dymClusterAsyncCompletion{lbWrapper: w, hostCtxPtr: hostCtxPtr}
	host, userAsync, ok := w.lb.ChooseHost(&dymClusterLoadBalancerHandle{wrapper: w}, ctx, completion)
	if !ok {
		*hostOut = nil
		*asyncOut = nil
		return
	}
	if userAsync != nil {
		// Async path. Bind the completion to the user's selection (used for cancel
		// dispatch), then register so Envoy can address it via the returned async-handle
		// pointer.
		completion.userSelection = userAsync
		w.asyncMu.Lock()
		w.asyncHandles[completion] = struct{}{}
		w.asyncMu.Unlock()
		ptr := clusterAsyncCompletionManager.record(completion)
		completion.managerPtr = ptr
		*hostOut = nil
		*asyncOut = C.envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr(ptr)
		return
	}
	*hostOut = C.envoy_dynamic_module_type_cluster_host_envoy_ptr(shared.UnsafeClusterHostPtr(host))
	*asyncOut = nil
}

//export envoy_dynamic_module_on_cluster_lb_cancel_host_selection
func envoy_dynamic_module_on_cluster_lb_cancel_host_selection(
	lbPtr C.envoy_dynamic_module_type_cluster_lb_module_ptr,
	asyncPtr C.envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr,
) {
	w := clusterLbManager.unwrap(unsafe.Pointer(lbPtr))
	if w == nil || w.lb == nil || w.destroyed {
		return
	}
	a := clusterAsyncCompletionManager.unwrap(unsafe.Pointer(asyncPtr))
	if a == nil {
		return
	}
	// Mark done first to prevent a racing Complete from also dispatching to Envoy.
	if a.done.Swap(true) {
		// Already completed; Complete already removed itself, nothing to do.
		return
	}
	if a.userSelection != nil {
		a.userSelection.Cancel()
	}
	w.asyncMu.Lock()
	delete(w.asyncHandles, a)
	w.asyncMu.Unlock()
	clusterAsyncCompletionManager.remove(unsafe.Pointer(asyncPtr))
}

//export envoy_dynamic_module_on_cluster_scheduled
func envoy_dynamic_module_on_cluster_scheduled(
	hostClusterPtr C.envoy_dynamic_module_type_cluster_envoy_ptr,
	clusterPtr C.envoy_dynamic_module_type_cluster_module_ptr,
	eventID C.uint64_t,
) {
	w := clusterManager.unwrap(unsafe.Pointer(clusterPtr))
	if w == nil || w.scheduler == nil || w.destroyed {
		return
	}
	w.hostClusterPtr = hostClusterPtr
	w.scheduler.onScheduled(uint64(eventID))
}

//export envoy_dynamic_module_on_cluster_server_initialized
func envoy_dynamic_module_on_cluster_server_initialized(
	hostClusterPtr C.envoy_dynamic_module_type_cluster_envoy_ptr,
	clusterPtr C.envoy_dynamic_module_type_cluster_module_ptr,
) {
	w := clusterManager.unwrap(unsafe.Pointer(clusterPtr))
	if w == nil || w.cluster == nil || w.destroyed {
		return
	}
	w.hostClusterPtr = hostClusterPtr
	w.cluster.OnServerInitialized(&dymClusterHandle{wrapper: w})
}

//export envoy_dynamic_module_on_cluster_drain_started
func envoy_dynamic_module_on_cluster_drain_started(
	hostClusterPtr C.envoy_dynamic_module_type_cluster_envoy_ptr,
	clusterPtr C.envoy_dynamic_module_type_cluster_module_ptr,
) {
	w := clusterManager.unwrap(unsafe.Pointer(clusterPtr))
	if w == nil || w.cluster == nil || w.destroyed {
		return
	}
	w.hostClusterPtr = hostClusterPtr
	w.cluster.OnDrainStarted(&dymClusterHandle{wrapper: w})
}

//export envoy_dynamic_module_on_cluster_shutdown
func envoy_dynamic_module_on_cluster_shutdown(
	hostClusterPtr C.envoy_dynamic_module_type_cluster_envoy_ptr,
	clusterPtr C.envoy_dynamic_module_type_cluster_module_ptr,
	completionCallback C.envoy_dynamic_module_type_event_cb,
	completionContext unsafe.Pointer,
) {
	w := clusterManager.unwrap(unsafe.Pointer(clusterPtr))
	if w == nil || w.cluster == nil || w.destroyed {
		C.cgoClusterInvokeEventCb(completionCallback, completionContext)
		return
	}
	completion := &clusterShutdownCompletion{cb: completionCallback, context: completionContext}
	w.shutdownMu.Lock()
	w.shutdownCompletion = completion
	w.shutdownMu.Unlock()
	w.hostClusterPtr = hostClusterPtr
	w.cluster.OnShutdown(&dymClusterHandle{wrapper: w}, func() {
		if completion.done.Swap(true) {
			return
		}
		C.cgoClusterInvokeEventCb(completion.cb, completion.context)
	})
}

//export envoy_dynamic_module_on_cluster_http_callout_done
func envoy_dynamic_module_on_cluster_http_callout_done(
	_ C.envoy_dynamic_module_type_cluster_envoy_ptr,
	clusterPtr C.envoy_dynamic_module_type_cluster_module_ptr,
	calloutID C.uint64_t,
	result C.envoy_dynamic_module_type_http_callout_result,
	headers *C.envoy_dynamic_module_type_envoy_http_header,
	headersSize C.size_t,
	chunks *C.envoy_dynamic_module_type_envoy_buffer,
	chunksSize C.size_t,
) {
	w := clusterManager.unwrap(unsafe.Pointer(clusterPtr))
	if w == nil || w.destroyed {
		return
	}
	resultHeaders := envoyHttpHeaderSliceToUnsafeHeaderSlice(unsafe.Slice(headers, int(headersSize)))
	resultChunks := envoyBufferSliceToUnsafeEnvoyBufferSlice(unsafe.Slice(chunks, int(chunksSize)))
	w.calloutMu.Lock()
	cb := w.calloutCallbacks[uint64(calloutID)]
	delete(w.calloutCallbacks, uint64(calloutID))
	w.calloutMu.Unlock()
	if cb != nil {
		cb.OnHttpCalloutDone(uint64(calloutID), shared.HttpCalloutResult(result), resultHeaders, resultChunks)
	}
}

//export envoy_dynamic_module_on_cluster_lb_on_host_membership_update
func envoy_dynamic_module_on_cluster_lb_on_host_membership_update(
	hostLbPtr C.envoy_dynamic_module_type_cluster_lb_envoy_ptr,
	lbPtr C.envoy_dynamic_module_type_cluster_lb_module_ptr,
	numHostsAdded C.size_t,
	numHostsRemoved C.size_t,
) {
	w := clusterLbManager.unwrap(unsafe.Pointer(lbPtr))
	if w == nil || w.lb == nil || w.destroyed {
		return
	}
	w.hostLbPtr = hostLbPtr
	w.lb.OnHostMembershipUpdate(&dymClusterLoadBalancerHandle{wrapper: w}, uint64(numHostsAdded), uint64(numHostsRemoved))
}
