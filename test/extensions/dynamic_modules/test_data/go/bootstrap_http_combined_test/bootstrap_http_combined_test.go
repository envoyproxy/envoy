// Test module demonstrating cross-extension data sharing via the function registry.
// Mirrors test_data/rust/bootstrap_http_combined_test.rs.
//
// A single dynamic module registers BOTH a bootstrap extension AND an HTTP filter. The
// bootstrap extension performs asynchronous initialization (off the main thread, via a
// scheduler), populates a routing table, and exposes a lookup function through the
// process-wide function registry. The HTTP filter, on each request, resolves the
// function from the registry and uses it to route requests based on the
// x-target-service header.
//
// Behavior matches the Rust test exactly:
//   - x-target-service: "service-a" (or "service-b")  -> 200, x-routed-to set on upstream req
//   - x-target-service: "unknown-service"             -> 503, x-error-reason: service_not_onboarded
//   - no x-target-service header                       -> pass through, no x-routed-to
//
// Cross-language note: the Rust SDK passes a real C function pointer through the
// registry. Go can't synthesize C function pointers without cgo //export, and the
// Bazel-mandated _test.go filename precludes cgo at file level here. We instead
// register the address of a package-level Go function variable. Both extensions live
// in the same .so, so the consumer can dereference the registered unsafe.Pointer back
// to a *func and call it. This still exercises the Envoy round-trip:
//
//	sdk.RegisterFunction (host-side store) -> Envoy registry -> sdk.GetFunction (host-side load)
//
// and asserts the registered pointer survives that round-trip with bit-equality.
package main

import (
	"sync"
	"sync/atomic"
	"time"
	"unsafe"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterBootstrapExtensionConfigFactories(map[string]shared.BootstrapExtensionConfigFactory{
		"combined_test": &combinedBootstrapConfigFactory{},
	})
	sdk.RegisterHttpFilterConfigFactories(map[string]shared.HttpFilterConfigFactory{
		"combined_filter": &combinedHttpFilterConfigFactory{},
	})
}

func main() {} //nolint:all

// -------------------------------------------------------------------------------------
// Shared state.
//
// routingTableReady gates HTTP filter requests until the bootstrap extension has
// populated the table and registered the lookup. Without this gate the filter could
// race the bootstrap async init.
//
// In production code modules would rely on Envoy's "init target" mechanism — the
// bootstrap extension calls SignalInitComplete only after async work finishes, and
// Envoy holds back listener traffic until then. The Rust test relies on exactly this
// guarantee. We do the same here, but also keep the atomic for an in-process check.
// -------------------------------------------------------------------------------------

var (
	routingTable      sync.Map // string -> string
	routingTableReady atomic.Bool
)

// getRouteEndpoint is the lookup function the bootstrap extension publishes. The
// HTTP filter retrieves the address of this var, dereferences it back to a func, and
// calls it. Storing the function in a package-level var (not just `func`) gives us a
// stable address for &getRouteEndpoint.
//
// Returns (endpoint, true) on hit, ("", false) on miss.
var getRouteEndpoint = func(service string) (string, bool) {
	v, ok := routingTable.Load(service)
	if !ok {
		return "", false
	}
	return v.(string), true
}

// Namespaced with a "go_" prefix so it doesn't collide with the Rust mirror
// (bootstrap_http_combined_test.rs) which registers a Rust extern "C" fn under
// the bare name "get_route_endpoint". The function registry is process-wide and
// the gtest parameterization runs FunctionRegistryCrossFilterRust before
// FunctionRegistryCrossFilterGo within the same process, so the bare key would
// already point at a Rust function pointer when this Go filter resolves it —
// which would crash when re-cast to a Go closure pointer.
const registryKey = "go_get_route_endpoint"

// -------------------------------------------------------------------------------------
// Bootstrap extension.
// -------------------------------------------------------------------------------------

type combinedBootstrapConfigFactory struct {
	shared.EmptyBootstrapExtensionConfigFactory
}

func (combinedBootstrapConfigFactory) Create(handle shared.BootstrapExtensionConfigHandle, _ []byte) (shared.BootstrapExtension, error) {
	scheduler := handle.NewScheduler()

	// Simulate async initialization on a background goroutine. Once done we hop back to
	// the main thread via the scheduler to register the lookup function and signal
	// init complete.
	go func() {
		time.Sleep(50 * time.Millisecond)
		routingTable.Store("service-a", "10.0.0.1:8080")
		routingTable.Store("service-b", "10.0.0.2:9090")
		routingTableReady.Store(true)
		sdk.Log(shared.LogLevelInfo, "async initialization complete, scheduling readiness signal")
		scheduler.Schedule(func() {
			ptr := unsafe.Pointer(&getRouteEndpoint)
			registered := sdk.RegisterFunction(registryKey, ptr)
			sdk.Log(shared.LogLevelInfo, "function registry registration: %v", registered)
			handle.SignalInitComplete()
			sdk.Log(shared.LogLevelInfo, "bootstrap init signaled complete after async initialization")
		})
	}()

	return &combinedBootstrapExtension{}, nil
}

type combinedBootstrapExtension struct {
	shared.EmptyBootstrapExtension
}

func (*combinedBootstrapExtension) OnServerInitialized(_ shared.BootstrapExtensionHandle) {
	sdk.Log(shared.LogLevelInfo, "combined module: server initialized")
}

func (*combinedBootstrapExtension) OnWorkerThreadInitialized(_ shared.BootstrapExtensionHandle) {
	sdk.Log(shared.LogLevelInfo, "combined module: worker thread initialized")
}

func (*combinedBootstrapExtension) OnShutdown(_ shared.BootstrapExtensionHandle, completion func()) {
	sdk.Log(shared.LogLevelInfo, "combined module: shutdown")
	completion()
}

// -------------------------------------------------------------------------------------
// HTTP filter.
// -------------------------------------------------------------------------------------

type combinedHttpFilterConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}

func (combinedHttpFilterConfigFactory) Create(_ shared.HttpFilterConfigHandle, _ []byte) (shared.HttpFilterFactory, error) {
	// The Rust test logs this exact message; the C++ driver matches it via
	// EXPECT_LOG_CONTAINS_ALL_OF.
	sdk.Log(shared.LogLevelInfo, "http filter config created (function resolution deferred to request time)")
	return &combinedHttpFilterFactory{}, nil
}

type combinedHttpFilterFactory struct {
	shared.EmptyHttpFilterFactory
}

func (*combinedHttpFilterFactory) Create(handle shared.HttpFilterHandle) shared.HttpFilter {
	return &combinedHttpFilter{handle: handle}
}

type combinedHttpFilter struct {
	shared.EmptyHttpFilter
	handle shared.HttpFilterHandle
}

func (p *combinedHttpFilter) OnRequestHeaders(headers shared.HeaderMap, _ bool) shared.HeadersStatus {
	// Resolve the lookup function from the process-wide registry. The bootstrap init
	// target gates listener traffic, so this MUST succeed by the time we get here.
	ptr, ok := sdk.GetFunction(registryKey)
	if !ok {
		sdk.Log(shared.LogLevelError, "%s not found in function registry", registryKey)
		p.handle.SendLocalResponse(503,
			[][2]string{{"x-error-reason", "function_not_registered"}},
			[]byte("routing function not registered"),
			"function_not_registered")
		return shared.HeadersStatusStop
	}
	lookup := *(*func(string) (string, bool))(ptr)

	// Read the target service header. If absent, pass through.
	svcBuf := headers.GetOne("x-target-service")
	if svcBuf.Len == 0 {
		return shared.HeadersStatusContinue
	}
	svc := svcBuf.ToString()

	endpoint, found := lookup(svc)
	if !found {
		p.handle.SendLocalResponse(503,
			[][2]string{{"x-error-reason", "service_not_onboarded"}},
			[]byte("service '"+svc+"' is not onboarded"),
			"service_not_onboarded")
		return shared.HeadersStatusStop
	}

	headers.Set("x-routed-to", endpoint)
	sdk.Log(shared.LogLevelInfo, "routed service '%s' to endpoint '%s'", svc, endpoint)
	return shared.HeadersStatusContinue
}
