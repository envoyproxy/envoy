//go:generate mockgen -source=bootstrap.go -destination=mocks/mock_bootstrap.go -package=mocks
package shared

// Bootstrap extension SDK surface for dynamic modules.
//
// Mirrors the Rust SDK's `bootstrap` module. Bootstrap extensions are top-level extensions
// configured at server startup; they live for the duration of the Envoy process and have access
// to server-level facilities: timers, file watchers, admin handlers, cluster/listener lifecycle
// notifications, init signaling, and HTTP callouts on the main thread.
//
// Lifecycle:
//   1. BootstrapExtensionConfigFactory.Create is invoked once per configuration on the main
//      thread, returning a BootstrapExtension. Envoy automatically registers an init target
//      that blocks traffic until the module signals readiness via handle.SignalInitComplete.
//   2. OnNew is called on the main thread once Envoy is ready to invoke per-extension hooks.
//   3. OnServerInitialized fires after the ServerFactoryContext is fully ready. From this
//      point on, the module may opt in to cluster/listener lifecycle events.
//   4. OnWorkerThreadInitialized fires once per worker thread.
//   5. OnDrainStarted fires when Envoy begins draining. The module may still use timers and
//      HTTP callouts during drain.
//   6. OnShutdown fires on the main thread during ShutdownExit. The module MUST call
//      completion exactly once when its async cleanup is done; Envoy waits for it.
//   7. OnDestroy fires when the extension is being destroyed.

// FileWatcherEvent is a bitmask of file-watcher events. Corresponds to
// envoy_dynamic_module_type_file_watcher_event / Envoy's Filesystem::Watcher::Events.
type FileWatcherEvent uint32

const (
	// FileWatcherEventMovedTo — the watched path was moved to (e.g., atomic-replace via
	// rename(2)).
	FileWatcherEventMovedTo FileWatcherEvent = 0x1
	// FileWatcherEventModified — the watched file's contents were modified.
	FileWatcherEventModified FileWatcherEvent = 0x2
)

// StatsIterationAction is the action returned from a stats iteration callback to control
// whether iteration continues.
type StatsIterationAction uint32

const (
	// StatsIterationActionContinue — keep iterating.
	StatsIterationActionContinue StatsIterationAction = 0
	// StatsIterationActionStop — stop iterating.
	StatsIterationActionStop StatsIterationAction = 1
)

// BootstrapTimer is an opaque handle to a main-thread timer created via
// BootstrapExtensionConfigHandle.NewTimer. The module owns the timer and must call Delete when
// it is no longer needed. Enable/Disable/Enabled MUST all be called on the main thread.
type BootstrapTimer interface {
	// Enable arms the timer to fire after delayMs milliseconds. If already armed, the timer is
	// reset.
	Enable(delayMs uint64)
	// Disable disarms the timer without destroying it. The timer can be re-enabled later.
	Disable()
	// Enabled reports whether the timer is currently armed.
	Enabled() bool
	// Delete destroys the timer. Calling other methods after Delete is undefined behavior.
	Delete()
}

// BootstrapExtension is the module-side bootstrap extension. All hooks fire on the main
// thread except OnWorkerThreadInitialized (which fires per-worker on the corresponding
// worker thread).
type BootstrapExtension interface {
	// OnNew is called when the extension instance is created.
	OnNew(handle BootstrapExtensionHandle)

	// OnServerInitialized fires after the ServerFactoryContext is fully initialized. From
	// this point on, the ClusterManager and ListenerManager are available — the module may
	// call handle.EnableClusterLifecycle / EnableListenerLifecycle.
	OnServerInitialized(handle BootstrapExtensionHandle)

	// OnWorkerThreadInitialized fires once per worker thread, on that worker's thread.
	OnWorkerThreadInitialized(handle BootstrapExtensionHandle)

	// OnDrainStarted fires on the main thread when Envoy begins draining. The module can
	// still use timers and HTTP callouts during drain.
	OnDrainStarted(handle BootstrapExtensionHandle)

	// OnShutdown fires on the main thread during ShutdownExit. The module MUST call
	// completion() exactly once when its async cleanup is finished; Envoy waits for it
	// before terminating.
	OnShutdown(handle BootstrapExtensionHandle, completion func())

	// OnDestroy is called when the extension is being destroyed.
	OnDestroy()
}

// EmptyBootstrapExtension is a no-op BootstrapExtension. OnShutdown immediately calls
// completion.
type EmptyBootstrapExtension struct{}

func (*EmptyBootstrapExtension) OnNew(_ BootstrapExtensionHandle)                     {}
func (*EmptyBootstrapExtension) OnServerInitialized(_ BootstrapExtensionHandle)       {}
func (*EmptyBootstrapExtension) OnWorkerThreadInitialized(_ BootstrapExtensionHandle) {}
func (*EmptyBootstrapExtension) OnDrainStarted(_ BootstrapExtensionHandle)            {}
func (*EmptyBootstrapExtension) OnShutdown(_ BootstrapExtensionHandle, completion func()) {
	completion()
}
func (*EmptyBootstrapExtension) OnDestroy() {}

// BootstrapExtensionConfigFactory is the top-level factory the module registers via
// sdk.RegisterBootstrapExtensionConfigFactories.
type BootstrapExtensionConfigFactory interface {
	// Create parses unparsedConfig and returns a BootstrapExtension. The handle is the
	// config-context handle that exposes timers, file watchers, admin handlers, scheduling,
	// metrics, HTTP callouts, init signaling, and cluster/listener-lifecycle event opt-in.
	//
	// IMPORTANT: every bootstrap extension is implicitly an init target — Envoy blocks
	// startup until the module calls handle.SignalInitComplete. If no async init is needed,
	// call it before returning from Create.
	Create(handle BootstrapExtensionConfigHandle, unparsedConfig []byte) (BootstrapExtension, error)
}

// EmptyBootstrapExtensionConfigFactory is a no-op BootstrapExtensionConfigFactory. It signals
// init complete immediately.
type EmptyBootstrapExtensionConfigFactory struct{}

func (*EmptyBootstrapExtensionConfigFactory) Create(handle BootstrapExtensionConfigHandle, _ []byte) (BootstrapExtension, error) {
	handle.SignalInitComplete()
	return &EmptyBootstrapExtension{}, nil
}

// BootstrapClusterLifecycleListener can be implemented by the BootstrapExtensionConfigFactory
// or BootstrapExtension if the module opted in to cluster lifecycle events via
// BootstrapExtensionConfigHandle.EnableClusterLifecycle. Hooks fire on the main thread.
type BootstrapClusterLifecycleListener interface {
	// OnClusterAddOrUpdate is called when a cluster is added or updated in the ClusterManager.
	OnClusterAddOrUpdate(clusterName string)
	// OnClusterRemoval is called when a cluster is removed from the ClusterManager.
	OnClusterRemoval(clusterName string)
}

// BootstrapListenerLifecycleListener can be implemented if the module opted in to listener
// lifecycle events via BootstrapExtensionConfigHandle.EnableListenerLifecycle. Hooks fire on
// the main thread.
type BootstrapListenerLifecycleListener interface {
	// OnListenerAddOrUpdate is called when a listener is added or updated in the
	// ListenerManager.
	OnListenerAddOrUpdate(listenerName string)
	// OnListenerRemoval is called when a listener is removed from the ListenerManager.
	OnListenerRemoval(listenerName string)
}

// BootstrapAdminHandler is implemented by the module to handle requests for an admin endpoint
// registered via BootstrapExtensionConfigHandle.RegisterAdminHandler. The module may be the
// BootstrapExtensionConfigFactory itself or a separate object stashed via cookies.
type BootstrapAdminHandler interface {
	// HandleAdminRequest is called when the admin endpoint is requested. The module sets the
	// response body via handle.SetAdminResponse (only valid during this call) and returns the
	// HTTP status code.
	HandleAdminRequest(handle BootstrapExtensionConfigHandle, method, path string, body []byte) uint32
}

// BootstrapExtensionConfigHandle is the config-context handle. It is valid for the lifetime
// of the BootstrapExtension. Most methods MUST be called on the main thread; cross-thread
// calls should be routed via NewScheduler.
type BootstrapExtensionConfigHandle interface {
	// SignalInitComplete signals that the bootstrap extension has finished its
	// initialization. Envoy blocks listener traffic until this is called. Modules with no
	// async init MUST call this synchronously during Create.
	//
	// MUST be called on the main thread.
	SignalInitComplete()

	// ---- scheduler ----

	// NewScheduler creates a main-thread scheduler bound to this configuration. The returned
	// Scheduler is safe to call from any goroutine; functions scheduled on it will run on
	// Envoy's main thread.
	NewScheduler() Scheduler

	// ---- HTTP callouts ----

	// HttpCallout sends an asynchronous HTTP request from the main thread. Result is
	// delivered via the supplied HttpCalloutCallback. MUST be called on the main thread (use
	// NewScheduler to dispatch from other threads).
	HttpCallout(clusterName string, headers [][2]string, body []byte, timeoutMs uint64,
		cb HttpCalloutCallback) (HttpCalloutInitResult, uint64)

	// ---- timers / file watchers ----

	// NewTimer creates a new disabled timer on the main-thread dispatcher. Returns nil on
	// failure. MUST be called on the main thread.
	NewTimer(onFire func(timer BootstrapTimer)) BootstrapTimer

	// AddFileWatch adds a watch for the given file path. When the file changes,
	// onChange(path, events) is invoked on the main thread. The watcher's lifetime is tied
	// to the configuration (auto-removed on destroy). MUST be called on the main thread.
	AddFileWatch(path string, events FileWatcherEvent, onChange func(path string, events FileWatcherEvent)) bool

	// ---- admin handler ----

	// RegisterAdminHandler registers a custom admin HTTP endpoint. When pathPrefix is
	// requested, handler.HandleAdminRequest is called on the main thread. removable allows
	// later RemoveAdminHandler. mutatesServerState marks endpoints that change server state
	// (e.g., POST endpoints).
	//
	// Returns false if admin is unavailable or the prefix is already taken.
	RegisterAdminHandler(pathPrefix, helpText string, removable, mutatesServerState bool, handler BootstrapAdminHandler) bool

	// RemoveAdminHandler removes a previously-registered (and removable) admin endpoint.
	RemoveAdminHandler(pathPrefix string) bool

	// SetAdminResponse sets the response body for the in-flight admin request. ONLY valid
	// inside BootstrapAdminHandler.HandleAdminRequest. Envoy copies the buffer immediately.
	SetAdminResponse(responseBody []byte)

	// ---- cluster / listener lifecycle opt-in ----

	// EnableClusterLifecycle opts the module in to receiving cluster-add/update/removal
	// events. The configuration's BootstrapExtension or BootstrapExtensionConfigFactory MUST
	// implement BootstrapClusterLifecycleListener. Idempotent: returns false if already
	// enabled.
	//
	// MUST be called on the main thread, typically during or after OnServerInitialized.
	EnableClusterLifecycle() bool

	// EnableListenerLifecycle opts the module in to receiving listener-add/update/removal
	// events. The configuration's BootstrapExtension or BootstrapExtensionConfigFactory MUST
	// implement BootstrapListenerLifecycleListener. Idempotent.
	EnableListenerLifecycle() bool

	// ---- labeled metrics ----

	DefineCounter(name string, labelNames []string) (MetricID, MetricsResult)
	IncrementCounter(id MetricID, labelValues []string, value uint64) MetricsResult
	DefineGauge(name string, labelNames []string) (MetricID, MetricsResult)
	SetGauge(id MetricID, labelValues []string, value uint64) MetricsResult
	IncrementGauge(id MetricID, labelValues []string, value uint64) MetricsResult
	DecrementGauge(id MetricID, labelValues []string, value uint64) MetricsResult
	DefineHistogram(name string, labelNames []string) (MetricID, MetricsResult)
	RecordHistogramValue(id MetricID, labelValues []string, value uint64) MetricsResult
}

// BootstrapExtensionHandle is the per-extension handle passed to BootstrapExtension hooks. It
// exposes server-wide stats access in addition to all the BootstrapExtensionConfigHandle
// methods (via composition: methods are not duplicated here; the runtime gives modules the
// config handle alongside).
//
// In practice, modules typically retain BootstrapExtensionConfigHandle from Create and use it
// throughout. BootstrapExtensionHandle adds read-only access to existing stats.
type BootstrapExtensionHandle interface {
	// GetCounterValue returns the current value of a counter by name. Returns 0 and false
	// if the counter does not exist.
	GetCounterValue(name string) (uint64, bool)

	// GetGaugeValue returns the current value of a gauge by name. Returns 0 and false if the
	// gauge does not exist.
	GetGaugeValue(name string) (uint64, bool)

	// GetHistogramSummary returns the cumulative sample count and sum for a histogram by
	// name. Returns (0, 0, false) if the histogram does not exist.
	GetHistogramSummary(name string) (sampleCount uint64, sampleSum float64, ok bool)

	// IterateCounters iterates over all counters in the stats store, calling visit for each.
	// Returning StatsIterationActionStop from visit halts iteration.
	IterateCounters(visit func(name UnsafeEnvoyBuffer, value uint64) StatsIterationAction)

	// IterateGauges iterates over all gauges in the stats store, calling visit for each.
	// Returning StatsIterationActionStop from visit halts iteration.
	IterateGauges(visit func(name UnsafeEnvoyBuffer, value uint64) StatsIterationAction)
}
