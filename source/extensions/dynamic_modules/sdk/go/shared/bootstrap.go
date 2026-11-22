package shared

// BootstrapExtensionConfigHandle provides the host services available to a bootstrap
// configuration. Except for Scheduler.Schedule and Log, its methods must be called on
// Envoy's main thread. It must not be used after BootstrapExtensionConfig.OnDestroy.
type BootstrapExtensionConfigHandle interface {
	CommonHandle
	Log(level LogLevel, format string, args ...any)
	// SignalInitComplete releases this configuration's initialization target. Call it
	// exactly once, either during Create or after asynchronous initialization finishes.
	// Use GetScheduler to return to the main thread from a background goroutine.
	SignalInitComplete()

	// GetScheduler returns a scheduler whose tasks run on the main thread. Obtain it
	// on the main thread; Schedule may then be called from any goroutine. Tasks that
	// have not run when the configuration is destroyed are discarded, and scheduling
	// after destruction is a no-op.
	GetScheduler() Scheduler
}

// BootstrapExtensionHandle provides host services for a bootstrap extension instance.
// It must not be used after BootstrapExtension.OnDestroy.
type BootstrapExtensionHandle interface {
	Log(level LogLevel, format string, args ...any)
}

// BootstrapExtensionConfigFactory creates a configuration on the main thread.
// Returning an error or nil rejects the configuration. The config bytes are valid
// only during Create; copy any bytes that the configuration retains.
type BootstrapExtensionConfigFactory interface {
	Create(handle BootstrapExtensionConfigHandle, config []byte) (BootstrapExtensionConfig, error)
}

// BootstrapExtensionConfig owns the configuration shared by its extension instances.
// Instances are created on the main thread and destroyed before OnDestroy.
type BootstrapExtensionConfig interface {
	// NewBootstrapExtension creates an instance. Returning nil rejects the extension.
	NewBootstrapExtension(handle BootstrapExtensionHandle) BootstrapExtension
	// OnDestroy releases configuration resources. It can run on the goroutine
	// whose scheduler commit releases the last native configuration reference.
	// Do not call main-thread-only host services or wait for that commit to return.
	OnDestroy()
}

// BootstrapExtension receives server lifecycle events on the main thread. Embed
// EmptyBootstrapExtension to provide defaults for events the extension does not use.
type BootstrapExtension interface {
	OnServerInitialized()
	// OnWorkerThreadInitialized runs once on the main thread, after workers have
	// been created and before their threads start. It is not a per-worker callback.
	OnWorkerThreadInitialized()
	OnDrainStarted()
	// OnShutdown must call complete when cleanup finishes. It may retain complete
	// for asynchronous cleanup and call it from another goroutine. Repeated calls
	// are ignored. If this hook panics, the SDK completes shutdown automatically.
	OnShutdown(complete func())
	OnDestroy()
}

// EmptyBootstrapExtension implements the default lifecycle behavior, including
// immediately completing shutdown.
type EmptyBootstrapExtension struct{}

func (EmptyBootstrapExtension) OnServerInitialized()       {}
func (EmptyBootstrapExtension) OnWorkerThreadInitialized() {}
func (EmptyBootstrapExtension) OnDrainStarted()            {}
func (EmptyBootstrapExtension) OnShutdown(complete func()) { complete() }
func (EmptyBootstrapExtension) OnDestroy()                 {}
