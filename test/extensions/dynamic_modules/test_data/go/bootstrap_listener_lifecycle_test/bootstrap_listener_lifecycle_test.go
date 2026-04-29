// Test module for the Bootstrap listener-lifecycle event API. Mirrors
// test_data/rust/bootstrap_listener_lifecycle_test.rs. Same shape as the cluster
// lifecycle test but for listener events.
package main

import (
	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterBootstrapExtensionConfigFactories(map[string]shared.BootstrapExtensionConfigFactory{
		"test": &listenerLifecycleConfigFactory{},
	})
}

func main() {}

type listenerLifecycleConfigFactory struct {
	shared.EmptyBootstrapExtensionConfigFactory
}

func (f *listenerLifecycleConfigFactory) Create(handle shared.BootstrapExtensionConfigHandle, _ []byte) (shared.BootstrapExtension, error) {
	return &listenerLifecycleExtension{handle: handle}, nil
}

type listenerLifecycleExtension struct {
	shared.EmptyBootstrapExtension
	handle shared.BootstrapExtensionConfigHandle
}

func (e *listenerLifecycleExtension) OnServerInitialized(_ shared.BootstrapExtensionHandle) {
	sdk.Log(shared.LogLevelInfo, "Bootstrap listener lifecycle test: server initialized")
	scheduler := e.handle.NewScheduler()
	scheduler.Schedule(func() {
		enabled := e.handle.EnableListenerLifecycle()
		sdk.Log(shared.LogLevelInfo, "Listener lifecycle enabled: %v", enabled)
		e.handle.SignalInitComplete()
	})
}

// BootstrapListenerLifecycleListener implementation.
func (*listenerLifecycleExtension) OnListenerAddOrUpdate(listenerName string) {
	sdk.Log(shared.LogLevelInfo, "Listener added or updated: %s", listenerName)
}

func (*listenerLifecycleExtension) OnListenerRemoval(listenerName string) {
	sdk.Log(shared.LogLevelInfo, "Listener removed: %s", listenerName)
}
