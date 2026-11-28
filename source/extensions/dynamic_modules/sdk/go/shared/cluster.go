//go:generate mockgen -source=cluster.go -destination=mocks/mock_cluster.go -package=mocks
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
	// An off-main-thread call aborts a debug build. In a release build Envoy
	// drops the call and the cluster stays warming.
	PreInitComplete()

	// GetScheduler returns a scheduler for main-thread tasks. It MUST only be
	// called on the main thread; but then the returned Scheduler can be used
	// from any thread. Obtain it on the main thread before sharing it with
	// goroutines or worker callbacks. Acquire
	// it in OnInit for compatibility with hosts that establish native cluster
	// ownership after ClusterConfig.Create returns. Envoy discards pending tasks
	// at destruction and ignores later scheduling attempts.
	GetScheduler() Scheduler

	// RunOnAllWorkers posts eventID to OnWorkerEvent on each registered worker,
	// excluding the main thread. This does not wait for workers or acknowledge
	// completion. Use ClusterConfigHandle.GetServerConcurrency on the main thread
	// to determine how many acknowledgements to expect. Call RunOnAllWorkers
	// from OnInit onward. It is a no-op after Envoy stops its workers. Startup must
	// complete PreInitComplete and return from OnServerInitialized before waiting
	// for worker acknowledgements, because workers start afterward.
	RunOnAllWorkers(eventID uint64)
}
