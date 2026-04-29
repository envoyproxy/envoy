// Test module for the Bootstrap admin-handler API. Mirrors
// test_data/rust/bootstrap_admin_handler_test.rs.
//
// Registers a custom admin endpoint during config_new, then logs the request method/path
// when the endpoint is hit. The C++ integration test asserts on the registered-success
// log line and on the response body.
package main

import (
	"fmt"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterBootstrapExtensionConfigFactories(map[string]shared.BootstrapExtensionConfigFactory{
		"test": &adminHandlerConfigFactory{},
	})
}

func main() {}

type adminHandlerConfigFactory struct {
	shared.EmptyBootstrapExtensionConfigFactory
}

func (f *adminHandlerConfigFactory) Create(handle shared.BootstrapExtensionConfigHandle, _ []byte) (shared.BootstrapExtension, error) {
	// Register the admin handler during config_new — admin is available at this point.
	registered := handle.RegisterAdminHandler(
		"/dynamic_module_admin_test",
		"Dynamic module admin handler test endpoint.",
		true,  // removable
		false, // mutatesServerState
		&adminHandler{},
	)
	sdk.Log(shared.LogLevelInfo, "Admin handler registered: %v", registered)
	if !registered {
		return nil, fmt.Errorf("admin handler registration failed")
	}

	handle.SignalInitComplete()
	return &shared.EmptyBootstrapExtension{}, nil
}

type adminHandler struct{}

func (*adminHandler) HandleAdminRequest(handle shared.BootstrapExtensionConfigHandle,
	method, path string, _ []byte) uint32 {
	sdk.Log(shared.LogLevelInfo, "Admin request received: %s %s", method, path)
	body := fmt.Sprintf("Hello from dynamic module admin handler! method=%s path=%s", method, path)
	handle.SetAdminResponse([]byte(body))
	return 200
}
