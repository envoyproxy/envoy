// Listener filter integration test module.
//
// Registers a single filter "test_filter" that returns Continue from OnAccept (allowing
// the filter chain to proceed) and exercises a few read-only handle accessors on each
// invocation.
package main

import (
	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterListenerFilterConfigFactories(map[string]shared.ListenerFilterConfigFactory{
		"test_filter": &passthroughConfigFactory{},
	})
}

func main() {} //nolint:all

type passthroughConfigFactory struct {
	shared.EmptyListenerFilterConfigFactory
}

func (passthroughConfigFactory) Create(_ shared.ListenerFilterConfigHandle, _ []byte) (shared.ListenerFilterFactory, error) {
	return &passthroughFactory{}, nil
}

type passthroughFactory struct {
	shared.EmptyListenerFilterFactory
}

func (*passthroughFactory) Create(_ shared.ListenerFilterHandle) shared.ListenerFilter {
	return &passthroughFilter{}
}

type passthroughFilter struct {
	shared.EmptyListenerFilter
}

func (*passthroughFilter) OnAccept(handle shared.ListenerFilterHandle) shared.ListenerFilterStatus {
	// Exercise a handful of read-only handle methods so they're hit at runtime.
	_, _, _ = handle.GetRemoteAddress()
	_, _, _ = handle.GetLocalAddress()
	return shared.ListenerFilterStatusContinue
}
