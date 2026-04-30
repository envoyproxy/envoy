// Load balancer integration test module.
//
// Registers a "first_host_lb" load balancer that always picks priority 0, index 0 (the
// only host in the test setup). The test asserts that requests successfully route
// through Envoy with the dynamic-module LB attached, exercising the full
// ChooseHost / OnHostMembershipUpdate dispatch surface.
package main

import (
	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterLoadBalancerConfigFactories(map[string]shared.LoadBalancerConfigFactory{
		"first_host_lb": &firstHostConfigFactory{},
	})
}

func main() {} //nolint:all

type firstHostConfigFactory struct {
	shared.EmptyLoadBalancerConfigFactory
}

func (firstHostConfigFactory) Create(_ shared.LoadBalancerConfigHandle, _ []byte) (shared.LoadBalancerFactory, error) {
	return &firstHostFactory{}, nil
}

type firstHostFactory struct {
	shared.EmptyLoadBalancerFactory
}

func (*firstHostFactory) Create(_ shared.LoadBalancerHandle) shared.LoadBalancer {
	return &firstHostLB{}
}

type firstHostLB struct {
	shared.EmptyLoadBalancer
}

func (*firstHostLB) ChooseHost(handle shared.LoadBalancerHandle, _ shared.LoadBalancerContext) (shared.HostSelection, bool) {
	// Bail if no hosts are available.
	if handle.GetHostsCount(0) == 0 {
		return shared.HostSelection{}, false
	}
	return shared.HostSelection{Priority: 0, Index: 0}, true
}
