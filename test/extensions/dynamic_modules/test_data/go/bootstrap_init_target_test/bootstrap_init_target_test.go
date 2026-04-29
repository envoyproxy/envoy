// Test module verifying that Envoy registers an init target for every bootstrap
// extension and unblocks server start once SignalInitComplete is called. Mirrors
// test_data/rust/bootstrap_init_target_test.rs.
package main

import (
	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterBootstrapExtensionConfigFactories(map[string]shared.BootstrapExtensionConfigFactory{
		"test": &initTargetConfigFactory{},
	})
}

func main() {}

type initTargetConfigFactory struct {
	shared.EmptyBootstrapExtensionConfigFactory
}

func (f *initTargetConfigFactory) Create(handle shared.BootstrapExtensionConfigHandle, _ []byte) (shared.BootstrapExtension, error) {
	// Signal completion immediately — this test does not require asynchronous init.
	handle.SignalInitComplete()
	sdk.Log(shared.LogLevelInfo, "Init target signaled complete during config creation")
	return &initTargetExtension{}, nil
}

type initTargetExtension struct {
	shared.EmptyBootstrapExtension
}

func (*initTargetExtension) OnServerInitialized(_ shared.BootstrapExtensionHandle) {
	sdk.Log(shared.LogLevelInfo, "Bootstrap init target test: server initialized after init target completed")
}

func (*initTargetExtension) OnWorkerThreadInitialized(_ shared.BootstrapExtensionHandle) {
	sdk.Log(shared.LogLevelInfo, "Bootstrap init target test completed successfully!")
}
