// Test module for the process-wide shared-data registry. Mirrors
// test_data/rust/bootstrap_shared_data_test.rs.
//
// Stores a sentinel pointer under a key, retrieves it back, overwrites it with a
// different value, retrieves again, and verifies non-existent key lookups return false.
// The Rust version uses static C uint64 storage; we use package-level Go uint64 vars,
// whose addresses are stable for the process lifetime since they're never dropped.
package main

import (
	"unsafe"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterBootstrapExtensionConfigFactories(map[string]shared.BootstrapExtensionConfigFactory{
		"test": &sharedDataConfigFactory{},
	})
}

func main() {}

type sharedDataConfigFactory struct {
	shared.EmptyBootstrapExtensionConfigFactory
}

func (f *sharedDataConfigFactory) Create(handle shared.BootstrapExtensionConfigHandle, _ []byte) (shared.BootstrapExtension, error) {
	handle.SignalInitComplete()
	return &sharedDataExtension{}, nil
}

type sharedDataExtension struct {
	shared.EmptyBootstrapExtension
}

var (
	initialValue uint64 = 42
	updatedValue uint64 = 84
)

func (*sharedDataExtension) OnServerInitialized(_ shared.BootstrapExtensionHandle) {
	const key = "test.shared.value"

	// First registration. The registry is process-wide; under parameterized test runs the
	// key may already exist from a prior run, so ignore the bool — overwrites are allowed.
	_ = sdk.RegisterSharedData(key, unsafe.Pointer(&initialValue))

	if p, ok := sdk.GetSharedData(key); !ok {
		panic("shared data should be found")
	} else if got := *(*uint64)(p); got != 42 {
		panic("first read returned unexpected value")
	}

	// Overwrite with a different pointer.
	if !sdk.RegisterSharedData(key, unsafe.Pointer(&updatedValue)) {
		panic("overwrite should succeed")
	}

	if p, ok := sdk.GetSharedData(key); !ok {
		panic("shared data should still be found after overwrite")
	} else if got := *(*uint64)(p); got != 84 {
		panic("second read returned unexpected value")
	}

	if _, ok := sdk.GetSharedData("no_such_data"); ok {
		panic("non-existent key should return false")
	}

	sdk.Log(shared.LogLevelInfo, "Bootstrap shared data registry test completed successfully!")
}
