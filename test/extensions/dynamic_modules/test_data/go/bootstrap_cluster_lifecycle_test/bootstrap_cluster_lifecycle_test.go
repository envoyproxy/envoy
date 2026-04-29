// Test module for the Bootstrap cluster-lifecycle event API. Mirrors
// test_data/rust/bootstrap_cluster_lifecycle_test.rs.
//
// On server initialization, schedules a main-thread closure that calls
// EnableClusterLifecycle() and signals init complete. The extension implements
// BootstrapClusterLifecycleListener so subsequent cluster add/update/removal events flow
// in through the type-asserted dispatch.
package main

import (
	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterBootstrapExtensionConfigFactories(map[string]shared.BootstrapExtensionConfigFactory{
		"test": &clusterLifecycleConfigFactory{},
	})
}

func main() {}

type clusterLifecycleConfigFactory struct {
	shared.EmptyBootstrapExtensionConfigFactory
}

func (f *clusterLifecycleConfigFactory) Create(handle shared.BootstrapExtensionConfigHandle, _ []byte) (shared.BootstrapExtension, error) {
	// Defer SignalInitComplete to OnServerInitialized → scheduled work, mirroring the
	// Rust pattern where the scheduler.commit happens in on_server_initialized.
	return &clusterLifecycleExtension{handle: handle}, nil
}

type clusterLifecycleExtension struct {
	shared.EmptyBootstrapExtension
	handle shared.BootstrapExtensionConfigHandle
}

func (e *clusterLifecycleExtension) OnServerInitialized(_ shared.BootstrapExtensionHandle) {
	sdk.Log(shared.LogLevelInfo, "Bootstrap cluster lifecycle test: server initialized")
	scheduler := e.handle.NewScheduler()
	scheduler.Schedule(func() {
		enabled := e.handle.EnableClusterLifecycle()
		sdk.Log(shared.LogLevelInfo, "Cluster lifecycle enabled: %v", enabled)
		e.handle.SignalInitComplete()
	})
}

// BootstrapClusterLifecycleListener implementation — picked up by the SDK via type
// assertion on the BootstrapExtension instance.
func (*clusterLifecycleExtension) OnClusterAddOrUpdate(clusterName string) {
	sdk.Log(shared.LogLevelInfo, "Cluster added or updated: %s", clusterName)
}

func (*clusterLifecycleExtension) OnClusterRemoval(clusterName string) {
	sdk.Log(shared.LogLevelInfo, "Cluster removed: %s", clusterName)
}
