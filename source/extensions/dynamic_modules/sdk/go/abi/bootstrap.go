package abi

/*
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup
#cgo linux LDFLAGS: -Wl,--unresolved-symbols=ignore-all
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../../abi/abi.h"

// Forward declarations for Go-exported callbacks so we can reference them from C trampolines.
extern envoy_dynamic_module_type_stats_iteration_action cgoBootstrapCounterIteratorGo(
    envoy_dynamic_module_type_envoy_buffer name, uint64_t value, void* user_data);
extern envoy_dynamic_module_type_stats_iteration_action cgoBootstrapGaugeIteratorGo(
    envoy_dynamic_module_type_envoy_buffer name, uint64_t value, void* user_data);

// cgoInvokeEventCb invokes Envoy's completion callback. The completion callback is a
// plain C function pointer Envoy hands us via the *_shutdown export hooks; calling it
// from Go via cgo would require turning the function-pointer field into a Go-callable
// value, so we route through this tiny C wrapper instead.
static inline void cgoInvokeEventCb(envoy_dynamic_module_type_event_cb cb, void* context) {
    if (cb != NULL) {
        cb(context);
    }
}

// C-side function pointers we can pass to Envoy. They forward to the Go-exported
// callbacks. `__attribute__((used))` ensures the linker keeps the symbol even though
// the only references are address-taken via cgo type wrappers compiled into a
// different translation unit; without it, --gc-sections strips the function and
// dlopen of the resulting c-shared .so fails on Linux.
__attribute__((used)) static envoy_dynamic_module_type_stats_iteration_action cgoBootstrapCounterIteratorC(
    envoy_dynamic_module_type_envoy_buffer name, uint64_t value, void* user_data) {
    return cgoBootstrapCounterIteratorGo(name, value, user_data);
}
__attribute__((used)) static envoy_dynamic_module_type_stats_iteration_action cgoBootstrapGaugeIteratorC(
    envoy_dynamic_module_type_envoy_buffer name, uint64_t value, void* user_data) {
    return cgoBootstrapGaugeIteratorGo(name, value, user_data);
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

type bootstrapConfigWrapper struct {
	hostConfigPtr C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr
	extension     shared.BootstrapExtension
	configHandle  *dymBootstrapConfigHandle

	// destroyed is set during the config_destroy hook so late callbacks (timer fires,
	// file watch events, lifecycle listener notifications, callout completions, admin
	// requests, scheduled tasks) become no-ops instead of re-entering user code.
	destroyed bool

	// timers indexed by their host pointer address.
	timersMu sync.Mutex
	timers   map[unsafe.Pointer]*dymBootstrapTimer

	// file watchers keyed by registered path.
	watchersMu sync.Mutex
	watchers   map[string]func(path string, events shared.FileWatcherEvent)

	// admin handlers keyed by path prefix.
	adminMu      sync.Mutex
	adminHandlers map[string]shared.BootstrapAdminHandler

	// callout callbacks indexed by callout id.
	calloutMu        sync.Mutex
	calloutCallbacks map[uint64]shared.HttpCalloutCallback

	// scheduler created from the config (lazy).
	scheduler *dymScheduler

	// Pending shutdown completion. Stored when OnShutdown is in flight; the runtime invokes
	// the C completion_callback exactly once when the module's wrapper completion func is
	// called.
	shutdownMu       sync.Mutex
	shutdownCompletion *bootstrapShutdownCompletion
}

type bootstrapExtensionWrapper struct {
	hostExtensionPtr C.envoy_dynamic_module_type_bootstrap_extension_envoy_ptr
	extension        shared.BootstrapExtension
	configRef        *bootstrapConfigWrapper

	// destroyed is set during the destroy hook so late callbacks (scheduled tasks,
	// shutdown event, file watcher events) become no-ops instead of re-entering user code.
	destroyed bool
}

type bootstrapShutdownCompletion struct {
	cb      C.envoy_dynamic_module_type_event_cb
	context unsafe.Pointer
	done    atomic.Bool
}

var bootstrapConfigManager = newManager[bootstrapConfigWrapper]()
var bootstrapExtensionManager = newManager[bootstrapExtensionWrapper]()

// dymBootstrapConfigHandle implements shared.BootstrapExtensionConfigHandle.
type dymBootstrapConfigHandle struct {
	wrapper *bootstrapConfigWrapper
}

func (h *dymBootstrapConfigHandle) SignalInitComplete() {
	C.envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete(
		h.wrapper.hostConfigPtr)
}

func (h *dymBootstrapConfigHandle) NewScheduler() shared.Scheduler {
	if h.wrapper.scheduler == nil {
		schedulerPtr := C.envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(
			h.wrapper.hostConfigPtr)
		h.wrapper.scheduler = newDymScheduler(
			unsafe.Pointer(schedulerPtr),
			func(p unsafe.Pointer, taskID C.uint64_t) {
				C.envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(
					(C.envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr)(p),
					taskID,
				)
			},
		)
		runtime.SetFinalizer(h.wrapper.scheduler, func(s *dymScheduler) {
			C.envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(
				(C.envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr)(s.schedulerPtr),
			)
		})
	}
	return h.wrapper.scheduler
}

func (h *dymBootstrapConfigHandle) HttpCallout(
	clusterName string, headers [][2]string, body []byte, timeoutMs uint64,
	cb shared.HttpCalloutCallback,
) (shared.HttpCalloutInitResult, uint64) {
	headerViews := headersToModuleHttpHeaderSlice(headers)
	var calloutID C.uint64_t
	result := C.envoy_dynamic_module_callback_bootstrap_extension_http_callout(
		h.wrapper.hostConfigPtr,
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

// dymBootstrapTimer implements shared.BootstrapTimer.
type dymBootstrapTimer struct {
	hostTimerPtr C.envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr
	onFire       func(timer shared.BootstrapTimer)
	deleted      atomic.Bool
}

func (t *dymBootstrapTimer) Enable(delayMs uint64) {
	C.envoy_dynamic_module_callback_bootstrap_extension_timer_enable(t.hostTimerPtr, C.uint64_t(delayMs))
}

func (t *dymBootstrapTimer) Disable() {
	C.envoy_dynamic_module_callback_bootstrap_extension_timer_disable(t.hostTimerPtr)
}

func (t *dymBootstrapTimer) Enabled() bool {
	return bool(C.envoy_dynamic_module_callback_bootstrap_extension_timer_enabled(t.hostTimerPtr))
}

func (t *dymBootstrapTimer) Delete() {
	if t.deleted.Swap(true) {
		return
	}
	C.envoy_dynamic_module_callback_bootstrap_extension_timer_delete(t.hostTimerPtr)
}

func (h *dymBootstrapConfigHandle) NewTimer(onFire func(timer shared.BootstrapTimer)) shared.BootstrapTimer {
	timerPtr := C.envoy_dynamic_module_callback_bootstrap_extension_timer_new(h.wrapper.hostConfigPtr)
	if timerPtr == nil {
		return nil
	}
	t := &dymBootstrapTimer{hostTimerPtr: timerPtr, onFire: onFire}
	h.wrapper.timersMu.Lock()
	if h.wrapper.timers == nil {
		h.wrapper.timers = make(map[unsafe.Pointer]*dymBootstrapTimer)
	}
	h.wrapper.timers[unsafe.Pointer(timerPtr)] = t
	h.wrapper.timersMu.Unlock()
	return t
}

func (h *dymBootstrapConfigHandle) AddFileWatch(
	path string, events shared.FileWatcherEvent,
	onChange func(path string, events shared.FileWatcherEvent),
) bool {
	ok := C.envoy_dynamic_module_callback_bootstrap_extension_file_watcher_add_watch(
		h.wrapper.hostConfigPtr, stringToModuleBuffer(path), C.uint32_t(events))
	runtime.KeepAlive(path)
	if !bool(ok) {
		return false
	}
	h.wrapper.watchersMu.Lock()
	if h.wrapper.watchers == nil {
		h.wrapper.watchers = make(map[string]func(path string, events shared.FileWatcherEvent))
	}
	h.wrapper.watchers[path] = onChange
	h.wrapper.watchersMu.Unlock()
	return true
}

func (h *dymBootstrapConfigHandle) RegisterAdminHandler(
	pathPrefix, helpText string, removable, mutatesServerState bool,
	handler shared.BootstrapAdminHandler,
) bool {
	ok := C.envoy_dynamic_module_callback_bootstrap_extension_register_admin_handler(
		h.wrapper.hostConfigPtr,
		stringToModuleBuffer(pathPrefix),
		stringToModuleBuffer(helpText),
		C.bool(removable),
		C.bool(mutatesServerState),
	)
	runtime.KeepAlive(pathPrefix)
	runtime.KeepAlive(helpText)
	if !bool(ok) {
		return false
	}
	h.wrapper.adminMu.Lock()
	if h.wrapper.adminHandlers == nil {
		h.wrapper.adminHandlers = make(map[string]shared.BootstrapAdminHandler)
	}
	h.wrapper.adminHandlers[pathPrefix] = handler
	h.wrapper.adminMu.Unlock()
	return true
}

func (h *dymBootstrapConfigHandle) RemoveAdminHandler(pathPrefix string) bool {
	ok := C.envoy_dynamic_module_callback_bootstrap_extension_remove_admin_handler(
		h.wrapper.hostConfigPtr, stringToModuleBuffer(pathPrefix))
	runtime.KeepAlive(pathPrefix)
	if bool(ok) {
		h.wrapper.adminMu.Lock()
		delete(h.wrapper.adminHandlers, pathPrefix)
		h.wrapper.adminMu.Unlock()
	}
	return bool(ok)
}

func (h *dymBootstrapConfigHandle) SetAdminResponse(responseBody []byte) {
	C.envoy_dynamic_module_callback_bootstrap_extension_admin_set_response(
		h.wrapper.hostConfigPtr, bytesToModuleBuffer(responseBody))
	runtime.KeepAlive(responseBody)
}

func (h *dymBootstrapConfigHandle) EnableClusterLifecycle() bool {
	return bool(C.envoy_dynamic_module_callback_bootstrap_extension_enable_cluster_lifecycle(
		h.wrapper.hostConfigPtr))
}

func (h *dymBootstrapConfigHandle) EnableListenerLifecycle() bool {
	return bool(C.envoy_dynamic_module_callback_bootstrap_extension_enable_listener_lifecycle(
		h.wrapper.hostConfigPtr))
}

// ---- labeled metrics ----

func (h *dymBootstrapConfigHandle) DefineCounter(name string, labelNames []string) (shared.MetricID, shared.MetricsResult) {
	labels := stringSlicesToModuleBuffers(labelNames)
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
		h.wrapper.hostConfigPtr, stringToModuleBuffer(name),
		bufferSlicePtr(labels), C.size_t(len(labels)), &id,
	)
	runtime.KeepAlive(name)
	runtime.KeepAlive(labelNames)
	runtime.KeepAlive(labels)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymBootstrapConfigHandle) IncrementCounter(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
		h.wrapper.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymBootstrapConfigHandle) DefineGauge(name string, labelNames []string) (shared.MetricID, shared.MetricsResult) {
	labels := stringSlicesToModuleBuffers(labelNames)
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
		h.wrapper.hostConfigPtr, stringToModuleBuffer(name),
		bufferSlicePtr(labels), C.size_t(len(labels)), &id,
	)
	runtime.KeepAlive(name)
	runtime.KeepAlive(labelNames)
	runtime.KeepAlive(labels)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymBootstrapConfigHandle) SetGauge(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge(
		h.wrapper.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymBootstrapConfigHandle) IncrementGauge(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(
		h.wrapper.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymBootstrapConfigHandle) DecrementGauge(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(
		h.wrapper.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

func (h *dymBootstrapConfigHandle) DefineHistogram(name string, labelNames []string) (shared.MetricID, shared.MetricsResult) {
	labels := stringSlicesToModuleBuffers(labelNames)
	var id C.size_t
	ret := C.envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram(
		h.wrapper.hostConfigPtr, stringToModuleBuffer(name),
		bufferSlicePtr(labels), C.size_t(len(labels)), &id,
	)
	runtime.KeepAlive(name)
	runtime.KeepAlive(labelNames)
	runtime.KeepAlive(labels)
	return shared.MetricID(id), shared.MetricsResult(ret)
}

func (h *dymBootstrapConfigHandle) RecordHistogramValue(id shared.MetricID, labelValues []string, value uint64) shared.MetricsResult {
	labels := stringSlicesToModuleBuffers(labelValues)
	ret := C.envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
		h.wrapper.hostConfigPtr, C.size_t(uint64(id)),
		bufferSlicePtr(labels), C.size_t(len(labels)),
		C.uint64_t(value),
	)
	runtime.KeepAlive(labelValues)
	runtime.KeepAlive(labels)
	return shared.MetricsResult(ret)
}

// dymBootstrapExtensionHandle implements shared.BootstrapExtensionHandle. It exposes
// stats-store access scoped to the extension instance.
type dymBootstrapExtensionHandle struct {
	hostExtensionPtr C.envoy_dynamic_module_type_bootstrap_extension_envoy_ptr
}

func (h *dymBootstrapExtensionHandle) GetCounterValue(name string) (uint64, bool) {
	var v C.uint64_t
	ok := C.envoy_dynamic_module_callback_bootstrap_extension_get_counter_value(
		h.hostExtensionPtr, stringToModuleBuffer(name), &v)
	runtime.KeepAlive(name)
	if !bool(ok) {
		return 0, false
	}
	return uint64(v), true
}

func (h *dymBootstrapExtensionHandle) GetGaugeValue(name string) (uint64, bool) {
	var v C.uint64_t
	ok := C.envoy_dynamic_module_callback_bootstrap_extension_get_gauge_value(
		h.hostExtensionPtr, stringToModuleBuffer(name), &v)
	runtime.KeepAlive(name)
	if !bool(ok) {
		return 0, false
	}
	return uint64(v), true
}

func (h *dymBootstrapExtensionHandle) GetHistogramSummary(name string) (uint64, float64, bool) {
	var sampleCount C.uint64_t
	var sampleSum C.double
	ok := C.envoy_dynamic_module_callback_bootstrap_extension_get_histogram_summary(
		h.hostExtensionPtr, stringToModuleBuffer(name), &sampleCount, &sampleSum)
	runtime.KeepAlive(name)
	if !bool(ok) {
		return 0, 0, false
	}
	return uint64(sampleCount), float64(sampleSum), true
}

// IterateCounters and IterateGauges intentionally route through the same callback machinery
// — each call captures the visit closure into a per-call thread-local-style map keyed by an
// opaque user_data pointer. Because Envoy invokes the iterator synchronously on the same
// goroutine and unwinds before returning to us, we can simply pin the Go closure for the
// duration of the call.

type bootstrapStatsIterContext struct {
	visit func(name shared.UnsafeEnvoyBuffer, value uint64) shared.StatsIterationAction
}

var bootstrapStatsIterManager = newManager[bootstrapStatsIterContext]()

func (h *dymBootstrapExtensionHandle) IterateCounters(visit func(name shared.UnsafeEnvoyBuffer, value uint64) shared.StatsIterationAction) {
	if visit == nil {
		return
	}
	ctx := &bootstrapStatsIterContext{visit: visit}
	ctxPtr := bootstrapStatsIterManager.record(ctx)
	defer bootstrapStatsIterManager.remove(ctxPtr)
	C.envoy_dynamic_module_callback_bootstrap_extension_iterate_counters(
		h.hostExtensionPtr,
		(C.envoy_dynamic_module_type_counter_iterator_fn)(C.cgoBootstrapCounterIteratorC),
		ctxPtr,
	)
}

func (h *dymBootstrapExtensionHandle) IterateGauges(visit func(name shared.UnsafeEnvoyBuffer, value uint64) shared.StatsIterationAction) {
	if visit == nil {
		return
	}
	ctx := &bootstrapStatsIterContext{visit: visit}
	ctxPtr := bootstrapStatsIterManager.record(ctx)
	defer bootstrapStatsIterManager.remove(ctxPtr)
	C.envoy_dynamic_module_callback_bootstrap_extension_iterate_gauges(
		h.hostExtensionPtr,
		(C.envoy_dynamic_module_type_gauge_iterator_fn)(C.cgoBootstrapGaugeIteratorC),
		ctxPtr,
	)
}

// =============================================================================
// CGo trampolines for stats iteration
// =============================================================================

//export cgoBootstrapCounterIteratorGo
func cgoBootstrapCounterIteratorGo(
	name C.envoy_dynamic_module_type_envoy_buffer,
	value C.uint64_t,
	userData unsafe.Pointer,
) C.envoy_dynamic_module_type_stats_iteration_action {
	ctx := bootstrapStatsIterManager.unwrap(userData)
	if ctx == nil || ctx.visit == nil {
		return C.envoy_dynamic_module_type_stats_iteration_action_Stop
	}
	action := ctx.visit(envoyBufferToUnsafeEnvoyBuffer(name), uint64(value))
	return C.envoy_dynamic_module_type_stats_iteration_action(action)
}

//export cgoBootstrapGaugeIteratorGo
func cgoBootstrapGaugeIteratorGo(
	name C.envoy_dynamic_module_type_envoy_buffer,
	value C.uint64_t,
	userData unsafe.Pointer,
) C.envoy_dynamic_module_type_stats_iteration_action {
	ctx := bootstrapStatsIterManager.unwrap(userData)
	if ctx == nil || ctx.visit == nil {
		return C.envoy_dynamic_module_type_stats_iteration_action_Stop
	}
	action := ctx.visit(envoyBufferToUnsafeEnvoyBuffer(name), uint64(value))
	return C.envoy_dynamic_module_type_stats_iteration_action(action)
}

// =============================================================================
// Event hooks
// =============================================================================

//export envoy_dynamic_module_on_bootstrap_extension_config_new
func envoy_dynamic_module_on_bootstrap_extension_config_new(
	hostConfigPtr C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr {
	nameStr := envoyBufferToStringUnsafe(name)
	configBytes := envoyBufferToBytesCopy(config)

	configFactory := sdk.GetBootstrapExtensionConfigFactory(nameStr)
	if configFactory == nil {
		hostLog(shared.LogLevelWarn, "Failed to load bootstrap extension configuration for %q: no factory registered", []any{nameStr})
		return nil
	}
	wrapper := &bootstrapConfigWrapper{hostConfigPtr: hostConfigPtr}
	wrapper.configHandle = &dymBootstrapConfigHandle{wrapper: wrapper}
	ext, err := configFactory.Create(wrapper.configHandle, configBytes)
	if err != nil {
		hostLog(shared.LogLevelWarn, "Failed to load bootstrap extension configuration for %q: %v", []any{nameStr, err})
		return nil
	}
	if ext == nil {
		hostLog(shared.LogLevelWarn, "Failed to load bootstrap extension configuration for %q: factory returned nil", []any{nameStr})
		return nil
	}
	wrapper.extension = ext
	configPtr := bootstrapConfigManager.record(wrapper)
	return C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr(configPtr)
}

//export envoy_dynamic_module_on_bootstrap_extension_config_destroy
func envoy_dynamic_module_on_bootstrap_extension_config_destroy(
	configPtr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
) {
	w := bootstrapConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil || w.destroyed {
		return
	}
	w.destroyed = true
	if w.extension != nil {
		w.extension.OnDestroy()
	}
	w.scheduler = nil
	bootstrapConfigManager.remove(unsafe.Pointer(configPtr))
}

//export envoy_dynamic_module_on_bootstrap_extension_new
func envoy_dynamic_module_on_bootstrap_extension_new(
	configPtr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	hostExtensionPtr C.envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
) C.envoy_dynamic_module_type_bootstrap_extension_module_ptr {
	cfg := bootstrapConfigManager.unwrap(unsafe.Pointer(configPtr))
	if cfg == nil {
		return nil
	}
	wrapper := &bootstrapExtensionWrapper{
		hostExtensionPtr: hostExtensionPtr,
		extension:        cfg.extension,
		configRef:        cfg,
	}
	extPtr := bootstrapExtensionManager.record(wrapper)
	handle := &dymBootstrapExtensionHandle{hostExtensionPtr: hostExtensionPtr}
	cfg.extension.OnNew(handle)
	return C.envoy_dynamic_module_type_bootstrap_extension_module_ptr(extPtr)
}

//export envoy_dynamic_module_on_bootstrap_extension_server_initialized
func envoy_dynamic_module_on_bootstrap_extension_server_initialized(
	hostExtensionPtr C.envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
	extPtr C.envoy_dynamic_module_type_bootstrap_extension_module_ptr,
) {
	w := bootstrapExtensionManager.unwrap(unsafe.Pointer(extPtr))
	if w == nil || w.extension == nil || w.destroyed {
		return
	}
	w.hostExtensionPtr = hostExtensionPtr
	handle := &dymBootstrapExtensionHandle{hostExtensionPtr: hostExtensionPtr}
	w.extension.OnServerInitialized(handle)
}

//export envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized
func envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized(
	hostExtensionPtr C.envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
	extPtr C.envoy_dynamic_module_type_bootstrap_extension_module_ptr,
) {
	w := bootstrapExtensionManager.unwrap(unsafe.Pointer(extPtr))
	if w == nil || w.extension == nil || w.destroyed {
		return
	}
	handle := &dymBootstrapExtensionHandle{hostExtensionPtr: hostExtensionPtr}
	w.extension.OnWorkerThreadInitialized(handle)
}

//export envoy_dynamic_module_on_bootstrap_extension_drain_started
func envoy_dynamic_module_on_bootstrap_extension_drain_started(
	hostExtensionPtr C.envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
	extPtr C.envoy_dynamic_module_type_bootstrap_extension_module_ptr,
) {
	w := bootstrapExtensionManager.unwrap(unsafe.Pointer(extPtr))
	if w == nil || w.extension == nil || w.destroyed {
		return
	}
	handle := &dymBootstrapExtensionHandle{hostExtensionPtr: hostExtensionPtr}
	w.extension.OnDrainStarted(handle)
}

//export envoy_dynamic_module_on_bootstrap_extension_shutdown
func envoy_dynamic_module_on_bootstrap_extension_shutdown(
	hostExtensionPtr C.envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
	extPtr C.envoy_dynamic_module_type_bootstrap_extension_module_ptr,
	completionCallback C.envoy_dynamic_module_type_event_cb,
	completionContext unsafe.Pointer,
) {
	w := bootstrapExtensionManager.unwrap(unsafe.Pointer(extPtr))
	if w == nil || w.extension == nil {
		// We still have to call completion to unblock Envoy.
		C.cgoInvokeEventCb(completionCallback, completionContext)
		return
	}
	completion := &bootstrapShutdownCompletion{cb: completionCallback, context: completionContext}
	w.configRef.shutdownMu.Lock()
	w.configRef.shutdownCompletion = completion
	w.configRef.shutdownMu.Unlock()
	handle := &dymBootstrapExtensionHandle{hostExtensionPtr: hostExtensionPtr}
	w.extension.OnShutdown(handle, func() {
		if completion.done.Swap(true) {
			return
		}
		C.cgoInvokeEventCb(completion.cb, completion.context)
	})
}

//export envoy_dynamic_module_on_bootstrap_extension_destroy
func envoy_dynamic_module_on_bootstrap_extension_destroy(
	extPtr C.envoy_dynamic_module_type_bootstrap_extension_module_ptr,
) {
	w := bootstrapExtensionManager.unwrap(unsafe.Pointer(extPtr))
	if w == nil || w.destroyed {
		return
	}
	w.destroyed = true
	bootstrapExtensionManager.remove(unsafe.Pointer(extPtr))
}

//export envoy_dynamic_module_on_bootstrap_extension_config_scheduled
func envoy_dynamic_module_on_bootstrap_extension_config_scheduled(
	_ C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	configPtr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	eventID C.uint64_t,
) {
	w := bootstrapConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil || w.scheduler == nil || w.destroyed {
		return
	}
	w.scheduler.onScheduled(uint64(eventID))
}

//export envoy_dynamic_module_on_bootstrap_extension_timer_fired
func envoy_dynamic_module_on_bootstrap_extension_timer_fired(
	_ C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	configPtr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	timerPtr C.envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr,
) {
	w := bootstrapConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil || w.destroyed {
		return
	}
	w.timersMu.Lock()
	t := w.timers[unsafe.Pointer(timerPtr)]
	w.timersMu.Unlock()
	if t != nil && t.onFire != nil {
		t.onFire(t)
	}
}

//export envoy_dynamic_module_on_bootstrap_extension_file_changed
func envoy_dynamic_module_on_bootstrap_extension_file_changed(
	_ C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	configPtr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	path C.envoy_dynamic_module_type_envoy_buffer,
	events C.uint32_t,
) {
	w := bootstrapConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil || w.destroyed {
		return
	}
	pathStr := envoyBufferToStringUnsafe(path)
	w.watchersMu.Lock()
	cb := w.watchers[pathStr]
	w.watchersMu.Unlock()
	if cb != nil {
		cb(pathStr, shared.FileWatcherEvent(events))
	}
}

//export envoy_dynamic_module_on_bootstrap_extension_cluster_add_or_update
func envoy_dynamic_module_on_bootstrap_extension_cluster_add_or_update(
	_ C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	configPtr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	clusterName C.envoy_dynamic_module_type_envoy_buffer,
) {
	w := bootstrapConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil || w.extension == nil || w.destroyed {
		return
	}
	if l, ok := w.extension.(shared.BootstrapClusterLifecycleListener); ok {
		l.OnClusterAddOrUpdate(envoyBufferToStringUnsafe(clusterName))
	}
}

//export envoy_dynamic_module_on_bootstrap_extension_cluster_removal
func envoy_dynamic_module_on_bootstrap_extension_cluster_removal(
	_ C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	configPtr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	clusterName C.envoy_dynamic_module_type_envoy_buffer,
) {
	w := bootstrapConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil || w.extension == nil || w.destroyed {
		return
	}
	if l, ok := w.extension.(shared.BootstrapClusterLifecycleListener); ok {
		l.OnClusterRemoval(envoyBufferToStringUnsafe(clusterName))
	}
}

//export envoy_dynamic_module_on_bootstrap_extension_listener_add_or_update
func envoy_dynamic_module_on_bootstrap_extension_listener_add_or_update(
	_ C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	configPtr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	listenerName C.envoy_dynamic_module_type_envoy_buffer,
) {
	w := bootstrapConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil || w.extension == nil || w.destroyed {
		return
	}
	if l, ok := w.extension.(shared.BootstrapListenerLifecycleListener); ok {
		l.OnListenerAddOrUpdate(envoyBufferToStringUnsafe(listenerName))
	}
}

//export envoy_dynamic_module_on_bootstrap_extension_listener_removal
func envoy_dynamic_module_on_bootstrap_extension_listener_removal(
	_ C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	configPtr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	listenerName C.envoy_dynamic_module_type_envoy_buffer,
) {
	w := bootstrapConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil || w.extension == nil || w.destroyed {
		return
	}
	if l, ok := w.extension.(shared.BootstrapListenerLifecycleListener); ok {
		l.OnListenerRemoval(envoyBufferToStringUnsafe(listenerName))
	}
}

//export envoy_dynamic_module_on_bootstrap_extension_http_callout_done
func envoy_dynamic_module_on_bootstrap_extension_http_callout_done(
	_ C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	configPtr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	calloutID C.uint64_t,
	result C.envoy_dynamic_module_type_http_callout_result,
	headers *C.envoy_dynamic_module_type_envoy_http_header,
	headersSize C.size_t,
	chunks *C.envoy_dynamic_module_type_envoy_buffer,
	chunksSize C.size_t,
) {
	w := bootstrapConfigManager.unwrap(unsafe.Pointer(configPtr))
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

//export envoy_dynamic_module_on_bootstrap_extension_admin_request
func envoy_dynamic_module_on_bootstrap_extension_admin_request(
	_ C.envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
	configPtr C.envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
	method C.envoy_dynamic_module_type_envoy_buffer,
	path C.envoy_dynamic_module_type_envoy_buffer,
	body C.envoy_dynamic_module_type_envoy_buffer,
) C.uint32_t {
	w := bootstrapConfigManager.unwrap(unsafe.Pointer(configPtr))
	if w == nil || w.destroyed {
		return 500
	}
	// path is used both for handler lookup and is passed through to the user; the lookup is
	// safe with the unsafe alias, but the user may retain pathStr, so copy before dispatch.
	pathView := envoyBufferToStringUnsafe(path)
	w.adminMu.Lock()
	// Match by exact prefix first; if no exact match, fall back to longest matching prefix.
	var handler shared.BootstrapAdminHandler
	if h, ok := w.adminHandlers[pathView]; ok {
		handler = h
	} else {
		var bestPrefix string
		for prefix, h := range w.adminHandlers {
			if len(prefix) > len(bestPrefix) && hasPrefixGo(pathView, prefix) {
				bestPrefix = prefix
				handler = h
			}
		}
	}
	w.adminMu.Unlock()
	if handler == nil {
		return 404
	}
	// Admin handlers may retain method/path/body strings (e.g., persisting them in module
	// state); copy out of the Envoy-owned buffers before user code runs.
	pathStr := string(pathView)
	methodStr := envoyBufferToStringCopy(method)
	bodyBytes := envoyBufferToBytesCopy(body)
	return C.uint32_t(handler.HandleAdminRequest(w.configHandle, methodStr, pathStr, bodyBytes))
}

func hasPrefixGo(s, prefix string) bool {
	if len(s) < len(prefix) {
		return false
	}
	return s[:len(prefix)] == prefix
}
