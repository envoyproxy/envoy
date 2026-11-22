package shared

// ClusterConfigHandle provides services for configuration creation on the main thread.
// It must not be used after ClusterConfig.OnDestroy.
type ClusterConfigHandle interface {
	CommonHandle
	Log(level LogLevel, format string, args ...any)
}

// ClusterHandle provides cluster lifecycle and scheduling services. Except for Log
// and Scheduler.Schedule, its methods must be called on Envoy's main thread.
// The handle must not be used after Cluster.OnDestroy.
type ClusterHandle interface {
	Log(level LogLevel, format string, args ...any)

	// PreInitComplete signals that initial discovery is complete. Call it once
	// from OnInit, or schedule it on the main thread after asynchronous initialization.
	PreInitComplete()

	// GetScheduler returns a scheduler for main-thread tasks. Obtain it on the
	// main thread before sharing it with goroutines or worker callbacks. Acquire
	// it in OnInit for compatibility with hosts that establish native cluster
	// ownership after NewCluster returns. Pending
	// tasks are discarded at destruction; scheduling after destruction is a no-op.
	GetScheduler() Scheduler

	// RunOnAllWorkers posts eventID to OnWorkerEvent on each registered worker,
	// excluding the main thread. This does not wait for workers or acknowledge
	// completion. The module must account for completions itself. Call it from
	// OnInit onward; it is a no-op after Envoy stops its workers. Startup must
	// complete PreInitComplete and return from OnServerInitialized before waiting
	// for worker acknowledgements, because workers start afterward.
	RunOnAllWorkers(eventID uint64)
}

// ClusterConfigFactory creates a configuration on the main thread. An error or nil
// rejects it. The config bytes are borrowed for Create; copy bytes retained later.
type ClusterConfigFactory interface {
	Create(handle ClusterConfigHandle, config []byte) (ClusterConfig, error)
}

// ClusterConfig owns configuration shared by cluster instances.
type ClusterConfig interface {
	// NewCluster runs on the main thread. Returning nil rejects the instance.
	NewCluster(handle ClusterHandle) Cluster
	// OnDestroy runs after the configuration's instances have been destroyed.
	OnDestroy()
}

// Cluster receives lifecycle events on the main thread, except OnWorkerEvent.
// Embed EmptyCluster for optional hooks and implement OnInit explicitly.
// Set dynamic_module_config.do_not_close to true so the Go runtime remains
// loaded for the lifetime of the process.
// Configure a native lb_policy such as ROUND_ROBIN: CLUSTER_PROVIDED cannot
// select hosts through this API. Custom host selection and host discovery
// callbacks are not exposed yet.
type Cluster interface {
	OnInit()
	// OnWorkerEvent runs concurrently on worker threads, possibly alongside main
	// thread hooks. Synchronize shared state and use a previously obtained
	// Scheduler to report completion to the main thread. Do not call main-thread
	// host methods directly from this hook.
	OnWorkerEvent(eventID uint64)
	// OnServerInitialized runs after cluster initialization, before workers start.
	OnServerInitialized()
	OnDrainStarted()
	// OnShutdown must call complete when cleanup finishes. The callback can be
	// retained and called from another goroutine; repeated calls are ignored.
	// If this hook panics, the SDK completes shutdown automatically. Envoy may
	// skip this hook when shutting down before workers start; put essential
	// resource cleanup in OnDestroy.
	OnShutdown(complete func())
	// OnDestroy releases module resources. Only Log and Scheduler.Schedule may
	// be used here; Envoy may be removing this cluster or shutting down.
	OnDestroy()
}

// EmptyCluster supplies defaults for optional hooks, including immediate shutdown
// completion. OnInit is deliberately required so initialization is explicit.
type EmptyCluster struct{}

func (EmptyCluster) OnWorkerEvent(uint64)       {}
func (EmptyCluster) OnServerInitialized()       {}
func (EmptyCluster) OnDrainStarted()            {}
func (EmptyCluster) OnShutdown(complete func()) { complete() }
func (EmptyCluster) OnDestroy()                 {}
