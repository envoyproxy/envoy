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

type tracerConfigWrapper struct {
	tracer       shared.Tracer
	configHandle *dymTracerConfigHandle
}

type tracerSpanWrapper struct {
	span shared.TracerSpan
	// keep digest-style returns alive across ABI calls that take pointers into Go memory
	traceIDCache []byte
	spanIDCache  []byte
	baggageCache []byte
}

var tracerConfigManager = newManager[tracerConfigWrapper]()
var tracerSpanManager = newManager[tracerSpanWrapper]()

// dymTracerConfigHandle implements shared.TracerConfigHandle.
type dymTracerConfigHandle struct {
	hostConfigPtr C.envoy_dynamic_module_type_tracer_config_envoy_ptr
}

func (h *dymTracerConfigHandle) DefineCounter(name string, labelNames []string) (shared.MetricID, shared.MetricsResult) {
	labels := stringSlicesToModuleBuffers(labelNames)
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_tracer_define_counter(
		h.hostConfigPtr, stringToModuleBuffer(name),
		bufferSlicePtr(labels), C.size_t(len(labels)), &id,
	)
	runtime.KeepAlive(name)
	runtime.KeepAlive(labelNames)
	runtime.KeepAlive(labels)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymTracerConfigHandle) IncrementCounter(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_tracer_increment_counter(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymTracerConfigHandle) DefineGauge(name string, labelNames []string) (shared.MetricID, shared.MetricsResult) {
	labels := stringSlicesToModuleBuffers(labelNames)
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_tracer_define_gauge(
		h.hostConfigPtr, stringToModuleBuffer(name),
		bufferSlicePtr(labels), C.size_t(len(labels)), &id,
	)
	runtime.KeepAlive(name)
	runtime.KeepAlive(labelNames)
	runtime.KeepAlive(labels)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymTracerConfigHandle) SetGauge(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_tracer_set_gauge(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymTracerConfigHandle) DefineHistogram(name string, labelNames []string) (shared.MetricID, shared.MetricsResult) {
	labels := stringSlicesToModuleBuffers(labelNames)
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_tracer_define_histogram(
		h.hostConfigPtr, stringToModuleBuffer(name),
		bufferSlicePtr(labels), C.size_t(len(labels)), &id,
	)
	runtime.KeepAlive(name)
	runtime.KeepAlive(labelNames)
	runtime.KeepAlive(labels)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymTracerConfigHandle) RecordHistogramValue(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_tracer_record_histogram_value(
		h.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

// dymTracerSpanContext implements shared.TracerSpanContext. It is bound to a single StartSpan
// or InjectContext call.
type dymTracerSpanContext struct {
	hostSpanPtr C.envoy_dynamic_module_type_tracer_span_envoy_ptr
}

func (c *dymTracerSpanContext) GetTraceContextValue(key string) (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_tracer_get_trace_context_value(
		c.hostSpanPtr, stringToModuleBuffer(key), &buf)
	runtime.KeepAlive(key)
	if !bool(ret) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ret)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (c *dymTracerSpanContext) SetTraceContextValue(key, value string) {
	C.envoy_dynamic_module_callback_tracer_set_trace_context_value(
		c.hostSpanPtr, stringToModuleBuffer(key), stringToModuleBuffer(value))
	runtime.KeepAlive(key)
	runtime.KeepAlive(value)
}

func (c *dymTracerSpanContext) RemoveTraceContextValue(key string) {
	C.envoy_dynamic_module_callback_tracer_remove_trace_context_value(
		c.hostSpanPtr, stringToModuleBuffer(key))
	runtime.KeepAlive(key)
}

func (c *dymTracerSpanContext) GetTraceContextProtocol() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_tracer_get_trace_context_protocol(c.hostSpanPtr, &buf)
	if !bool(ret) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ret)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (c *dymTracerSpanContext) GetTraceContextHost() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_tracer_get_trace_context_host(c.hostSpanPtr, &buf)
	if !bool(ret) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ret)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (c *dymTracerSpanContext) GetTraceContextPath() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_tracer_get_trace_context_path(c.hostSpanPtr, &buf)
	if !bool(ret) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ret)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

func (c *dymTracerSpanContext) GetTraceContextMethod() (shared.UnsafeEnvoyBuffer, bool) {
	var buf C.envoy_dynamic_module_type_envoy_buffer
	ret := C.envoy_dynamic_module_callback_tracer_get_trace_context_method(c.hostSpanPtr, &buf)
	if !bool(ret) || buf.ptr == nil || buf.length == 0 {
		return shared.UnsafeEnvoyBuffer{}, bool(ret)
	}
	return envoyBufferToUnsafeEnvoyBuffer(buf), true
}

// =============================================================================
// Event hooks - tracer config
// =============================================================================

//export envoy_dynamic_module_on_tracer_config_new
func envoy_dynamic_module_on_tracer_config_new(
	hostConfigPtr C.envoy_dynamic_module_type_tracer_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_tracer_config_module_ptr {
	nameStr := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesCopy(config)

	configHandle := &dymTracerConfigHandle{hostConfigPtr: hostConfigPtr}
	configFactory := sdk.GetTracerConfigFactory(nameStr)
	if configFactory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load tracer configuration: no factory for %s", []any{nameStr})
		return nil
	}
	t, err := configFactory.Create(configHandle, configBytes)
	if err != nil || t == nil {
		hostLog(shared.LogLevelWarn, "Failed to load tracer configuration: %v", []any{err})
		return nil
	}
	wrapper := &tracerConfigWrapper{tracer: t, configHandle: configHandle}
	configPtr := tracerConfigManager.record(wrapper)
	return C.envoy_dynamic_module_type_tracer_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_tracer_config_destroy
func envoy_dynamic_module_on_tracer_config_destroy(
	configPtr C.envoy_dynamic_module_type_tracer_config_module_ptr,
) {
	w := tracerConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil {
		return
	}
	if w.tracer != nil {
		w.tracer.OnDestroy()
	}
	tracerConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_tracer_start_span
func envoy_dynamic_module_on_tracer_start_span(
	configPtr C.envoy_dynamic_module_type_tracer_config_module_ptr,
	hostSpanPtr C.envoy_dynamic_module_type_tracer_span_envoy_ptr,
	operationName C.envoy_dynamic_module_type_envoy_buffer,
	traced C.bool,
	reason C.envoy_dynamic_module_type_trace_reason,
) C.envoy_dynamic_module_type_tracer_span_module_ptr {
	cfg := tracerConfigManager.unwrap(unsafe.Pointer(configPtr))
	if cfg == nil || cfg.tracer == nil {
		return nil
	}
	ctx := &dymTracerSpanContext{hostSpanPtr: hostSpanPtr}
	span := cfg.tracer.StartSpan(ctx, envoyBufferToStringUnsafe(operationName), bool(traced), shared.TraceReason(reason))
	if span == nil {
		return nil
	}
	wrapper := &tracerSpanWrapper{span: span}
	spanPtr := tracerSpanManager.record(wrapper)
	return C.envoy_dynamic_module_type_tracer_span_module_ptr(spanPtr)
}

// =============================================================================
// Event hooks - span
// =============================================================================

//export envoy_dynamic_module_on_tracer_span_set_operation
func envoy_dynamic_module_on_tracer_span_set_operation(
	spanPtr C.envoy_dynamic_module_type_tracer_span_module_ptr,
	operation C.envoy_dynamic_module_type_envoy_buffer,
) {
	w := tracerSpanManager.unwrap(unsafe.Pointer(spanPtr))
	if w == nil || w.span == nil {
		return
	}
	w.span.SetOperation(envoyBufferToStringUnsafe(operation))
}

//export envoy_dynamic_module_on_tracer_span_set_tag
func envoy_dynamic_module_on_tracer_span_set_tag(
	spanPtr C.envoy_dynamic_module_type_tracer_span_module_ptr,
	key C.envoy_dynamic_module_type_envoy_buffer,
	value C.envoy_dynamic_module_type_envoy_buffer,
) {
	w := tracerSpanManager.unwrap(unsafe.Pointer(spanPtr))
	if w == nil || w.span == nil {
		return
	}
	w.span.SetTag(envoyBufferToStringUnsafe(key), envoyBufferToStringUnsafe(value))
}

//export envoy_dynamic_module_on_tracer_span_log
func envoy_dynamic_module_on_tracer_span_log(
	spanPtr C.envoy_dynamic_module_type_tracer_span_module_ptr,
	timestampNs C.int64_t,
	event C.envoy_dynamic_module_type_envoy_buffer,
) {
	w := tracerSpanManager.unwrap(unsafe.Pointer(spanPtr))
	if w == nil || w.span == nil {
		return
	}
	w.span.Log(int64(timestampNs), envoyBufferToStringUnsafe(event))
}

//export envoy_dynamic_module_on_tracer_span_finish
func envoy_dynamic_module_on_tracer_span_finish(
	spanPtr C.envoy_dynamic_module_type_tracer_span_module_ptr,
) {
	w := tracerSpanManager.unwrap(unsafe.Pointer(spanPtr))
	if w == nil || w.span == nil {
		return
	}
	w.span.Finish()
}

//export envoy_dynamic_module_on_tracer_span_inject_context
func envoy_dynamic_module_on_tracer_span_inject_context(
	spanPtr C.envoy_dynamic_module_type_tracer_span_module_ptr,
	hostSpanPtr C.envoy_dynamic_module_type_tracer_span_envoy_ptr,
) {
	w := tracerSpanManager.unwrap(unsafe.Pointer(spanPtr))
	if w == nil || w.span == nil {
		return
	}
	ctx := &dymTracerSpanContext{hostSpanPtr: hostSpanPtr}
	w.span.InjectContext(ctx)
}

//export envoy_dynamic_module_on_tracer_span_spawn_child
func envoy_dynamic_module_on_tracer_span_spawn_child(
	spanPtr C.envoy_dynamic_module_type_tracer_span_module_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	startTimeNs C.int64_t,
) C.envoy_dynamic_module_type_tracer_span_module_ptr {
	w := tracerSpanManager.unwrap(unsafe.Pointer(spanPtr))
	if w == nil || w.span == nil {
		return nil
	}
	child := w.span.SpawnChild(envoyBufferToStringUnsafe(name), int64(startTimeNs))
	if child == nil {
		return nil
	}
	wrapper := &tracerSpanWrapper{span: child}
	childPtr := tracerSpanManager.record(wrapper)
	return C.envoy_dynamic_module_type_tracer_span_module_ptr(childPtr)
}

//export envoy_dynamic_module_on_tracer_span_set_sampled
func envoy_dynamic_module_on_tracer_span_set_sampled(
	spanPtr C.envoy_dynamic_module_type_tracer_span_module_ptr,
	sampled C.bool,
) {
	w := tracerSpanManager.unwrap(unsafe.Pointer(spanPtr))
	if w == nil || w.span == nil {
		return
	}
	w.span.SetSampled(bool(sampled))
}

//export envoy_dynamic_module_on_tracer_span_use_local_decision
func envoy_dynamic_module_on_tracer_span_use_local_decision(
	spanPtr C.envoy_dynamic_module_type_tracer_span_module_ptr,
) C.bool {
	w := tracerSpanManager.unwrap(unsafe.Pointer(spanPtr))
	if w == nil || w.span == nil {
		return true
	}
	return C.bool(w.span.UseLocalDecision())
}

//export envoy_dynamic_module_on_tracer_span_get_baggage
func envoy_dynamic_module_on_tracer_span_get_baggage(
	spanPtr C.envoy_dynamic_module_type_tracer_span_module_ptr,
	key C.envoy_dynamic_module_type_envoy_buffer,
	valueOut *C.envoy_dynamic_module_type_module_buffer,
) C.bool {
	w := tracerSpanManager.unwrap(unsafe.Pointer(spanPtr))
	if w == nil || w.span == nil {
		return false
	}
	value, ok := w.span.GetBaggage(envoyBufferToStringUnsafe(key))
	if !ok {
		valueOut.ptr = nil
		valueOut.length = 0
		return false
	}
	w.baggageCache = value
	if len(value) == 0 {
		valueOut.ptr = nil
		valueOut.length = 0
	} else {
		valueOut.ptr = (*C.char)(unsafe.Pointer(unsafe.SliceData(value)))
		valueOut.length = C.size_t(len(value))
	}
	return true
}

//export envoy_dynamic_module_on_tracer_span_set_baggage
func envoy_dynamic_module_on_tracer_span_set_baggage(
	spanPtr C.envoy_dynamic_module_type_tracer_span_module_ptr,
	key C.envoy_dynamic_module_type_envoy_buffer,
	value C.envoy_dynamic_module_type_envoy_buffer,
) {
	w := tracerSpanManager.unwrap(unsafe.Pointer(spanPtr))
	if w == nil || w.span == nil {
		return
	}
	w.span.SetBaggage(envoyBufferToStringUnsafe(key), envoyBufferToStringUnsafe(value))
}

//export envoy_dynamic_module_on_tracer_span_get_trace_id
func envoy_dynamic_module_on_tracer_span_get_trace_id(
	spanPtr C.envoy_dynamic_module_type_tracer_span_module_ptr,
	valueOut *C.envoy_dynamic_module_type_module_buffer,
) C.bool {
	w := tracerSpanManager.unwrap(unsafe.Pointer(spanPtr))
	if w == nil || w.span == nil {
		return false
	}
	value, ok := w.span.GetTraceID()
	if !ok {
		valueOut.ptr = nil
		valueOut.length = 0
		return false
	}
	w.traceIDCache = value
	if len(value) == 0 {
		valueOut.ptr = nil
		valueOut.length = 0
	} else {
		valueOut.ptr = (*C.char)(unsafe.Pointer(unsafe.SliceData(value)))
		valueOut.length = C.size_t(len(value))
	}
	return true
}

//export envoy_dynamic_module_on_tracer_span_get_span_id
func envoy_dynamic_module_on_tracer_span_get_span_id(
	spanPtr C.envoy_dynamic_module_type_tracer_span_module_ptr,
	valueOut *C.envoy_dynamic_module_type_module_buffer,
) C.bool {
	w := tracerSpanManager.unwrap(unsafe.Pointer(spanPtr))
	if w == nil || w.span == nil {
		return false
	}
	value, ok := w.span.GetSpanID()
	if !ok {
		valueOut.ptr = nil
		valueOut.length = 0
		return false
	}
	w.spanIDCache = value
	if len(value) == 0 {
		valueOut.ptr = nil
		valueOut.length = 0
	} else {
		valueOut.ptr = (*C.char)(unsafe.Pointer(unsafe.SliceData(value)))
		valueOut.length = C.size_t(len(value))
	}
	return true
}

//export envoy_dynamic_module_on_tracer_span_destroy
func envoy_dynamic_module_on_tracer_span_destroy(
	spanPtr C.envoy_dynamic_module_type_tracer_span_module_ptr,
) {
	w := tracerSpanManager.unwrap(unsafe.Pointer(spanPtr))
	if w == nil {
		return
	}
	if w.span != nil {
		w.span.OnDestroy()
	}
	tracerSpanManager.remove(unsafe.Pointer(spanPtr))
}
