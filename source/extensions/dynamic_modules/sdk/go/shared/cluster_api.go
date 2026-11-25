//go:generate mockgen -source=cluster_api.go -destination=mocks/mock_cluster_api.go -package=mocks
package shared

// ClusterConfigFactory creates a configuration on the main thread. An error or nil
// rejects it. The config bytes are borrowed for Create; copy bytes retained later.
type ClusterConfigFactory interface {
	Create(handle ClusterConfigHandle, config []byte) (ClusterConfig, error)
}

// ClusterConfig owns configuration shared by cluster instances.
type ClusterConfig interface {
	// Create runs on the main thread. Returning nil rejects the instance.
	Create(handle ClusterHandle) Cluster
	// OnDestroy runs after the configuration's instances have been destroyed.
	OnDestroy()
}

// EmptyClusterConfig supplies defaults for optional configuration hooks.
// Embed it in a configuration that implements Create.
type EmptyClusterConfig struct{}

func (EmptyClusterConfig) OnDestroy() {}

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
	// A cluster created after this stage, including by a CDS add or update,
	// never receives this callback.
	OnServerInitialized()
	OnDrainStarted()
	// OnShutdown must call complete when cleanup finishes. The callback can be
	// retained and called from another goroutine; repeated calls are ignored.
	// If this hook panics, the SDK completes shutdown automatically. Otherwise
	// Envoy waits for complete before exiting. Envoy skips this hook during
	// early shutdown before workers start. Put essential cleanup in OnDestroy.
	OnShutdown(complete func())
	// OnDestroy releases module resources. Only Log and Scheduler.Schedule may
	// be used here. Envoy discards tasks scheduled during destruction.
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
