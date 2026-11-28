//go:generate mockgen -source=bootstrap_api.go -destination=mocks/mock_bootstrap_api.go -package=mocks
package shared

// BootstrapExtensionConfigFactory creates a configuration on the main thread.
// Returning an error or nil rejects the configuration. The config bytes are valid
// only during Create; copy any bytes that the configuration retains.
type BootstrapExtensionConfigFactory interface {
	Create(handle BootstrapExtensionConfigHandle, config []byte) (BootstrapExtensionConfig, error)
}

// BootstrapExtensionConfig owns the configuration shared by its extension instances.
// Instances are created on the main thread and destroyed before OnDestroy.
type BootstrapExtensionConfig interface {
	// Create creates an instance. Returning nil rejects the extension.
	Create(handle BootstrapExtensionHandle) BootstrapExtension
	// OnDestroy releases configuration resources. Schedulers do not keep the
	// configuration alive, and tasks scheduled after it starts are discarded.
	OnDestroy()
}

// EmptyBootstrapExtensionConfig supplies defaults for optional configuration hooks.
// Embed it in a configuration that implements Create.
type EmptyBootstrapExtensionConfig struct{}

func (EmptyBootstrapExtensionConfig) OnDestroy() {}

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
	// Otherwise Envoy waits for complete before exiting. Envoy skips this hook
	// during early shutdown before workers start and in init_only and validate
	// modes. Put essential cleanup in OnDestroy.
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
