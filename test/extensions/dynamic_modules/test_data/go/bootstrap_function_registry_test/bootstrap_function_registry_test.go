// Test module for the process-wide function registry. Mirrors
// test_data/rust/bootstrap_function_registry_test.rs in spirit.
//
// The Rust test registers actual extern "C" functions and invokes them through the
// resolved pointer. Go can't directly convert Go funcs to C function pointers (cgo
// limitation) — and using cgo in this _test.go-suffixed source would make it
// non-buildable under "go build ./...". We therefore exercise the registry mechanism by
// registering two distinct sentinel addresses, retrieving them by key, and asserting
// pointer equality. This validates the SDK code paths (RegisterFunction / GetFunction)
// that the Rust test exercises end-to-end; the function-invocation aspect is exercised
// elsewhere through C test_data fakes.
package main

import (
	"unsafe"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterBootstrapExtensionConfigFactories(map[string]shared.BootstrapExtensionConfigFactory{
		"test": &functionRegistryConfigFactory{},
	})
}

func main() {}

type functionRegistryConfigFactory struct {
	shared.EmptyBootstrapExtensionConfigFactory
}

func (f *functionRegistryConfigFactory) Create(handle shared.BootstrapExtensionConfigHandle, _ []byte) (shared.BootstrapExtension, error) {
	handle.SignalInitComplete()
	return &functionRegistryExtension{}, nil
}

type functionRegistryExtension struct {
	shared.EmptyBootstrapExtension
}

// Sentinel storage we register pointers to. Package-level vars have stable addresses for
// the lifetime of the process — Go's GC won't move them, and they're never dropped.
var (
	answerSentinel uint64 = 0xA1
	doubleSentinel uint64 = 0xB2
)

func (*functionRegistryExtension) OnServerInitialized(_ shared.BootstrapExtensionHandle) {
	answerPtr := unsafe.Pointer(&answerSentinel)
	doublePtr := unsafe.Pointer(&doubleSentinel)

	// Register sentinels. The registry is process-wide; under parameterized test runs the
	// same key may already exist from a prior run, so we ignore the boolean return.
	_ = sdk.RegisterFunction("get_answer", answerPtr)
	_ = sdk.RegisterFunction("double_it", doublePtr)

	if got, ok := sdk.GetFunction("get_answer"); !ok {
		panic("registered function get_answer should be found")
	} else if got != answerPtr {
		panic("get_answer round-trip returned wrong pointer")
	}

	if got, ok := sdk.GetFunction("double_it"); !ok {
		panic("registered function double_it should be found")
	} else if got != doublePtr {
		panic("double_it round-trip returned wrong pointer")
	}

	if _, ok := sdk.GetFunction("no_such_fn"); ok {
		panic("non-existent function should not be found")
	}

	sdk.Log(shared.LogLevelInfo, "Bootstrap function registry test completed successfully!")
}
