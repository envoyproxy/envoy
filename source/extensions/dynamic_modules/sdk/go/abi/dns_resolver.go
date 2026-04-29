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

type dnsResolverConfigWrapper struct {
	factory      shared.DnsResolverFactory
	configHandle *dymDnsResolverConfigHandle

	// destroyed is set during config_destroy so any late resolver creation becomes a
	// no-op instead of re-entering user code.
	destroyed bool
}

type dnsResolverWrapper struct {
	resolver  shared.DnsResolver
	configRef *dnsResolverConfigWrapper // for ResolveComplete via the config handle

	// destroyed is set during resolver_destroy so any late resolve / cancel /
	// reset_networking callbacks become no-ops.
	destroyed bool
}

// dnsQueryWrapper is a stable Go-allocated cell that backs the opaque query pointer handed to
// Envoy. Because the manager pins these via its internal map, the wrapper's address is a safe,
// stable handle to round-trip through Envoy's ABI.
type dnsQueryWrapper struct {
	query any
}

var dnsResolverConfigManager = newManager[dnsResolverConfigWrapper]()
var dnsResolverManager = newManager[dnsResolverWrapper]()
var dnsQueryManager = newManager[dnsQueryWrapper]()

// dymDnsResolverConfigHandle implements shared.DnsResolverConfigHandle. The same handle is used
// for ResolveComplete (the Envoy-side resolver pointer) and for metric callbacks (the
// Envoy-side config pointer).
type dymDnsResolverConfigHandle struct {
	hostConfigPtr   C.envoy_dynamic_module_type_dns_resolver_config_envoy_ptr
	hostResolverPtr C.envoy_dynamic_module_type_dns_resolver_envoy_ptr
}

func (h *dymDnsResolverConfigHandle) ResolveComplete(
	queryID uint64, status shared.DnsResolutionStatus, details string, addresses []shared.DnsAddress,
) {
	// Convert addresses to C array.
	cAddrs := make([]C.envoy_dynamic_module_type_dns_address, len(addresses))
	// Pin Go strings for the duration of the C call by keeping them in a slice.
	for i := range addresses {
		s := addresses[i].Address
		cAddrs[i] = C.envoy_dynamic_module_type_dns_address{
			address_ptr:    (*C.char)(unsafe.Pointer(unsafe.StringData(s))),
			address_length: C.size_t(len(s)),
			ttl_seconds:    C.uint32_t(addresses[i].TTLSeconds),
		}
	}
	var cAddrPtr *C.envoy_dynamic_module_type_dns_address
	if len(cAddrs) > 0 {
		cAddrPtr = unsafe.SliceData(cAddrs)
	}
	C.envoy_dynamic_module_callback_dns_resolve_complete(
		h.hostResolverPtr,
		C.uint64_t(queryID),
		C.envoy_dynamic_module_type_dns_resolution_status(status),
		stringToModuleBuffer(details),
		cAddrPtr,
		C.size_t(len(cAddrs)),
	)
	runtime.KeepAlive(addresses)
	runtime.KeepAlive(details)
	runtime.KeepAlive(cAddrs)
}

// stringSlicesToModuleBuffers prepares a Go []string as a C buffer array. Caller must
// runtime.KeepAlive both the input and the returned slice for the duration of the C call.
func stringSlicesToModuleBuffers(values []string) []C.envoy_dynamic_module_type_module_buffer {
	if len(values) == 0 {
		return nil
	}
	out := make([]C.envoy_dynamic_module_type_module_buffer, len(values))
	for i, v := range values {
		out[i] = stringToModuleBuffer(v)
	}
	return out
}

func bufferSlicePtr(b []C.envoy_dynamic_module_type_module_buffer) *C.envoy_dynamic_module_type_module_buffer {
	if len(b) == 0 {
		return nil
	}
	return unsafe.SliceData(b)
}

func (h *dymDnsResolverConfigHandle) DefineCounter(name string, labelNames []string) (shared.MetricID, shared.MetricsResult) {
	labels := stringSlicesToModuleBuffers(labelNames)
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_dns_resolver_config_define_counter(
		h.hostConfigPtr, stringToModuleBuffer(name),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		&id,
	)
	runtime.KeepAlive(name)
	runtime.KeepAlive(labelNames)
	runtime.KeepAlive(labels)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymDnsResolverConfigHandle) IncrementCounter(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_dns_resolver_config_increment_counter(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymDnsResolverConfigHandle) DefineGauge(name string, labelNames []string) (shared.MetricID, shared.MetricsResult) {
	labels := stringSlicesToModuleBuffers(labelNames)
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_dns_resolver_config_define_gauge(
		h.hostConfigPtr, stringToModuleBuffer(name),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		&id,
	)
	runtime.KeepAlive(name)
	runtime.KeepAlive(labelNames)
	runtime.KeepAlive(labels)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymDnsResolverConfigHandle) SetGauge(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_dns_resolver_config_set_gauge(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymDnsResolverConfigHandle) IncrementGauge(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_dns_resolver_config_increment_gauge(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymDnsResolverConfigHandle) DecrementGauge(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_dns_resolver_config_decrement_gauge(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymDnsResolverConfigHandle) DefineHistogram(name string, labelNames []string) (shared.MetricID, shared.MetricsResult) {
	labels := stringSlicesToModuleBuffers(labelNames)
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_dns_resolver_config_define_histogram(
		h.hostConfigPtr, stringToModuleBuffer(name),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		&id,
	)
	runtime.KeepAlive(name)
	runtime.KeepAlive(labelNames)
	runtime.KeepAlive(labels)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymDnsResolverConfigHandle) RecordHistogramValue(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_dns_resolver_config_record_histogram_value(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

// =============================================================================
// Event hooks
// =============================================================================

//export envoy_dynamic_module_on_dns_resolver_config_new
func envoy_dynamic_module_on_dns_resolver_config_new(
	hostConfigPtr C.envoy_dynamic_module_type_dns_resolver_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_dns_resolver_config_module_ptr {
	nameStr := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesCopy(config)

	configHandle := &dymDnsResolverConfigHandle{hostConfigPtr: hostConfigPtr}
	configFactory := sdk.GetDnsResolverConfigFactory(nameStr)
	if configFactory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load DNS resolver configuration for %q: no factory registered", []any{nameStr})
		return nil
	}
	factory, err := configFactory.Create(configHandle, configBytes)
	if err != nil {
		hostLog(shared.LogLevelWarn, "Failed to load DNS resolver configuration for %q: %v", []any{nameStr, err})
		return nil
	}
	if factory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load DNS resolver configuration for %q: factory returned nil", []any{nameStr})
		return nil
	}
	wrapper := &dnsResolverConfigWrapper{factory: factory, configHandle: configHandle}
	configPtr := dnsResolverConfigManager.record(wrapper)
	return C.envoy_dynamic_module_type_dns_resolver_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_dns_resolver_config_destroy
func envoy_dynamic_module_on_dns_resolver_config_destroy(
	configPtr C.envoy_dynamic_module_type_dns_resolver_config_module_ptr,
) {
	w := dnsResolverConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil || w.destroyed {
		return
	}
	w.destroyed = true
	w.factory.OnDestroy()
	dnsResolverConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_dns_resolver_new
func envoy_dynamic_module_on_dns_resolver_new(
	configPtr C.envoy_dynamic_module_type_dns_resolver_config_module_ptr,
	hostResolverPtr C.envoy_dynamic_module_type_dns_resolver_envoy_ptr,
) C.envoy_dynamic_module_type_dns_resolver_module_ptr {
	cfg := dnsResolverConfigManager.unwrap(unsafe.Pointer(configPtr))
	if cfg == nil || cfg.destroyed {
		return nil
	}
	// Bind the resolver pointer for ResolveComplete callbacks.
	cfg.configHandle.hostResolverPtr = hostResolverPtr

	r := cfg.factory.Create(cfg.configHandle)
	if r == nil {
		return nil
	}
	wrapper := &dnsResolverWrapper{
		resolver:  r,
		configRef: cfg,
	}
	resolverPtr := dnsResolverManager.record(wrapper)
	return C.envoy_dynamic_module_type_dns_resolver_module_ptr(resolverPtr)
}

//export envoy_dynamic_module_on_dns_resolver_destroy
func envoy_dynamic_module_on_dns_resolver_destroy(
	resolverPtr C.envoy_dynamic_module_type_dns_resolver_module_ptr,
) {
	w := dnsResolverManager.unwrap(unsafe.Pointer(resolverPtr))
	if w == nil || w.destroyed {
		return
	}
	w.destroyed = true
	if w.resolver != nil {
		w.resolver.OnDestroy()
	}
	dnsResolverManager.remove(unsafe.Pointer(resolverPtr))
}

//export envoy_dynamic_module_on_dns_resolve
func envoy_dynamic_module_on_dns_resolve(
	resolverPtr C.envoy_dynamic_module_type_dns_resolver_module_ptr,
	dnsName C.envoy_dynamic_module_type_envoy_buffer,
	family C.envoy_dynamic_module_type_dns_lookup_family,
	queryID C.uint64_t,
) C.envoy_dynamic_module_type_dns_query_module_ptr {
	w := dnsResolverManager.unwrap(unsafe.Pointer(resolverPtr))
	if w == nil || w.resolver == nil || w.destroyed {
		return nil
	}
	dnsNameStr := envoyBufferToStringUnsafe(dnsName)
	query := w.resolver.Resolve(w.configRef.configHandle, dnsNameStr, shared.DnsLookupFamily(family), uint64(queryID))
	if query == nil {
		return nil
	}
	// Wrap the module-side query in a stable Go-allocated cell whose address can safely be
	// round-tripped through Envoy's ABI as the opaque query handle.
	wrapper := &dnsQueryWrapper{query: query}
	queryPtr := dnsQueryManager.record(wrapper)
	return C.envoy_dynamic_module_type_dns_query_module_ptr(queryPtr)
}

//export envoy_dynamic_module_on_dns_resolve_cancel
func envoy_dynamic_module_on_dns_resolve_cancel(
	resolverPtr C.envoy_dynamic_module_type_dns_resolver_module_ptr,
	queryPtr C.envoy_dynamic_module_type_dns_query_module_ptr,
) {
	w := dnsResolverManager.unwrap(unsafe.Pointer(resolverPtr))
	if w == nil || w.resolver == nil || w.destroyed {
		return
	}
	q := dnsQueryManager.unwrap(unsafe.Pointer(queryPtr))
	if q == nil {
		return
	}
	w.resolver.Cancel(q.query)
	dnsQueryManager.remove(unsafe.Pointer(queryPtr))
}

//export envoy_dynamic_module_on_dns_resolver_reset_networking
func envoy_dynamic_module_on_dns_resolver_reset_networking(
	resolverPtr C.envoy_dynamic_module_type_dns_resolver_module_ptr,
) {
	w := dnsResolverManager.unwrap(unsafe.Pointer(resolverPtr))
	if w == nil || w.resolver == nil || w.destroyed {
		return
	}
	w.resolver.ResetNetworking()
}
