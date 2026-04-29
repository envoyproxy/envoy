// Test module for bootstrap extension lifecycle from the Go SDK. Mirrors the Rust
// bootstrap_integration_test.rs: emits log messages at each lifecycle hook so the C++
// integration test driver can assert via EXPECT_LOG_CONTAINS that the dispatch path is
// wired up correctly. Specifically validates the shutdown completion fix from the code
// review (the trampoline previously discarded the completion callback, hanging Envoy
// teardown).
package main

import (
	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterBootstrapExtensionConfigFactories(map[string]shared.BootstrapExtensionConfigFactory{
		"test": &basicConfigFactory{},
	})
}

func main() {}

type basicConfigFactory struct {
	shared.EmptyBootstrapExtensionConfigFactory
}

func (f *basicConfigFactory) Create(handle shared.BootstrapExtensionConfigHandle, _ []byte) (shared.BootstrapExtension, error) {
	// Signal init complete synchronously so Envoy can start accepting traffic.
	handle.SignalInitComplete()
	return &basicExtension{}, nil
}

type basicExtension struct {
	shared.EmptyBootstrapExtension
}

func (*basicExtension) OnServerInitialized(_ shared.BootstrapExtensionHandle) {
	sdk.Log(shared.LogLevelInfo, "Bootstrap extension server initialized from Go!")
}

func (*basicExtension) OnWorkerThreadInitialized(_ shared.BootstrapExtensionHandle) {
	sdk.Log(shared.LogLevelInfo, "Bootstrap extension worker thread initialized from Go!")
}

func (*basicExtension) OnDrainStarted(_ shared.BootstrapExtensionHandle) {
	sdk.Log(shared.LogLevelInfo, "Bootstrap extension drain started from Go!")
}

func (*basicExtension) OnShutdown(_ shared.BootstrapExtensionHandle, completion func()) {
	sdk.Log(shared.LogLevelInfo, "Bootstrap extension shutdown from Go!")
	// MUST call completion exactly once so Envoy can finish teardown. The bug fix in
	// abi/bootstrap.go ensures this actually reaches Envoy's event_cb.
	completion()
}
