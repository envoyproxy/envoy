//go:generate mockgen -source=bootstrap.go -destination=mocks/mock_bootstrap.go -package=mocks
package shared

// BootstrapExtensionConfigHandle provides the host services available to a bootstrap
// configuration. Except for Scheduler.Schedule and Log, its methods must be called on
// Envoy's main thread. It must not be used after BootstrapExtensionConfig.OnDestroy.
type BootstrapExtensionConfigHandle interface {
	CommonHandle
	Log(level LogLevel, format string, args ...any)
	// SignalInitComplete releases this configuration's initialization target. Call it
	// exactly once, during Create or when asynchronous initialization finishes, even
	// on failure. Envoy waits without a timeout. Use GetScheduler to return to the
	// main thread from a background goroutine. This call can run other lifecycle
	// hooks and wait for workers to start. Release any locks those hooks need first.
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
