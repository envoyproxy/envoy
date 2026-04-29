// Test module for the Cluster SDK surface from Go. Mirrors test_data/rust/cluster_integration_test.rs.
//
// Validates the bug fixes that came out of the code review:
//   - Shutdown completion callback actually reaches Envoy (was silently dropped before — would
//     hang Envoy teardown).
//   - Async host selection's Complete() does not leak the wrapper into the global manager.
//   - User-supplied ClusterAsyncHostSelection.Cancel is invoked on cancellation.
//   - destroyed flag short-circuits late callbacks (scheduler events, host-membership updates,
//     callouts that fire while OnDestroy is running).
//
// Each scenario is exposed as a named cluster type the C++ integration test driver can
// instantiate by name.
package main

import (
	"strings"
	"sync"
	"sync/atomic"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterClusterConfigFactories(map[string]shared.ClusterConfigFactory{
		"sync_host_selection":   &syncHostSelectionConfigFactory{},
		"async_host_selection":  &asyncHostSelectionConfigFactory{},
		"scheduler_host_update": &schedulerHostUpdateConfigFactory{},
		"lifecycle_callbacks":   &lifecycleCallbacksConfigFactory{},
	})
}

func main() {}

// hostList wraps a slice of ClusterHost so a mutex can guard updates from multiple callbacks.
type hostList struct {
	mu    sync.Mutex
	hosts []shared.ClusterHost
}

func (l *hostList) set(hosts []shared.ClusterHost) {
	l.mu.Lock()
	l.hosts = hosts
	l.mu.Unlock()
}

func (l *hostList) get() []shared.ClusterHost {
	l.mu.Lock()
	defer l.mu.Unlock()
	if len(l.hosts) == 0 {
		return nil
	}
	out := make([]shared.ClusterHost, len(l.hosts))
	copy(out, l.hosts)
	return out
}

// addressFromConfig extracts the upstream address from the test config bytes (the C++ driver
// passes the upstream address verbatim as the config string).
func addressFromConfig(config []byte) string {
	return strings.TrimSpace(string(config))
}

// =============================================================================
// Synchronous host selection.
// =============================================================================

type syncHostSelectionConfigFactory struct {
	shared.EmptyClusterConfigFactory
}

func (f *syncHostSelectionConfigFactory) Create(handle shared.ClusterConfigHandle, config []byte) (shared.ClusterFactory, error) {
	counterID, _ := handle.DefineCounter("requests_routed", nil)
	return &syncHostSelectionFactory{
		address:   addressFromConfig(config),
		counterID: counterID,
		handle:    handle,
	}, nil
}

type syncHostSelectionFactory struct {
	shared.EmptyClusterFactory
	address   string
	counterID shared.MetricID
	handle    shared.ClusterConfigHandle
}

func (f *syncHostSelectionFactory) Create(_ shared.ClusterConfigHandle) shared.Cluster {
	return &syncHostSelectionCluster{
		address:   f.address,
		hosts:     &hostList{},
		counterID: f.counterID,
		handle:    f.handle,
	}
}

type syncHostSelectionCluster struct {
	shared.EmptyCluster
	address   string
	hosts     *hostList
	counterID shared.MetricID
	handle    shared.ClusterConfigHandle
}

func (c *syncHostSelectionCluster) OnInit(handle shared.ClusterHandle) {
	specs := []shared.ClusterHostSpec{{Address: c.address, Weight: 1}}
	if hosts, ok := handle.AddHosts(0, specs); ok {
		c.hosts.set(hosts)
	}
	handle.PreInitComplete()
}

func (c *syncHostSelectionCluster) NewLoadBalancer(_ shared.ClusterLoadBalancerHandle) shared.ClusterLoadBalancer {
	return &syncHostSelectionLb{
		hosts:     c.hosts,
		counterID: c.counterID,
		handle:    c.handle,
	}
}

type syncHostSelectionLb struct {
	shared.EmptyClusterLoadBalancer
	hosts     *hostList
	counterID shared.MetricID
	index     atomic.Uint64
	handle    shared.ClusterConfigHandle
}

func (lb *syncHostSelectionLb) ChooseHost(
	_ shared.ClusterLoadBalancerHandle, _ shared.ClusterLoadBalancerContext, _ shared.ClusterAsyncCompletion,
) (shared.ClusterHost, shared.ClusterAsyncHostSelection, bool) {
	hosts := lb.hosts.get()
	if len(hosts) == 0 {
		return shared.ClusterHost{}, nil, false
	}
	idx := lb.index.Add(1) - 1
	host := hosts[idx%uint64(len(hosts))]
	if lb.counterID != 0 {
		lb.handle.IncrementCounter(lb.counterID, nil, 1)
	}
	return host, nil, true
}

// =============================================================================
// Asynchronous host selection via background goroutine.
// =============================================================================

type asyncHostSelectionConfigFactory struct {
	shared.EmptyClusterConfigFactory
}

func (f *asyncHostSelectionConfigFactory) Create(_ shared.ClusterConfigHandle, config []byte) (shared.ClusterFactory, error) {
	return &asyncHostSelectionFactory{address: addressFromConfig(config)}, nil
}

type asyncHostSelectionFactory struct {
	shared.EmptyClusterFactory
	address string
}

func (f *asyncHostSelectionFactory) Create(_ shared.ClusterConfigHandle) shared.Cluster {
	return &asyncHostSelectionCluster{address: f.address, hosts: &hostList{}}
}

type asyncHostSelectionCluster struct {
	shared.EmptyCluster
	address string
	hosts   *hostList
}

func (c *asyncHostSelectionCluster) OnInit(handle shared.ClusterHandle) {
	if hosts, ok := handle.AddHosts(0, []shared.ClusterHostSpec{{Address: c.address, Weight: 1}}); ok {
		c.hosts.set(hosts)
	}
	handle.PreInitComplete()
}

func (c *asyncHostSelectionCluster) NewLoadBalancer(_ shared.ClusterLoadBalancerHandle) shared.ClusterLoadBalancer {
	return &asyncHostSelectionLb{hosts: c.hosts}
}

type asyncHostSelectionLb struct {
	shared.EmptyClusterLoadBalancer
	hosts *hostList
}

func (lb *asyncHostSelectionLb) ChooseHost(
	_ shared.ClusterLoadBalancerHandle, _ shared.ClusterLoadBalancerContext, completion shared.ClusterAsyncCompletion,
) (shared.ClusterHost, shared.ClusterAsyncHostSelection, bool) {
	hosts := lb.hosts.get()
	if len(hosts) == 0 {
		return shared.ClusterHost{}, nil, false
	}
	host := hosts[0]
	cancelled := &atomic.Bool{}
	// Resolve in a background goroutine. The SDK posts the completion to the correct worker
	// thread via Envoy's async-host-selection callback; modules don't have to worry about
	// thread affinity here.
	go func() {
		if cancelled.Load() {
			return
		}
		completion.Complete(host, "async_resolved")
	}()
	return shared.ClusterHost{}, &asyncHandle{cancelled: cancelled}, true
}

type asyncHandle struct {
	cancelled *atomic.Bool
}

func (h *asyncHandle) Cancel() { h.cancelled.Store(true) }

// =============================================================================
// Scheduler-based host updates.
// =============================================================================

const addHostEventID uint64 = 100

type schedulerHostUpdateConfigFactory struct {
	shared.EmptyClusterConfigFactory
}

func (f *schedulerHostUpdateConfigFactory) Create(_ shared.ClusterConfigHandle, config []byte) (shared.ClusterFactory, error) {
	return &schedulerHostUpdateFactory{address: addressFromConfig(config)}, nil
}

type schedulerHostUpdateFactory struct {
	shared.EmptyClusterFactory
	address string
}

func (f *schedulerHostUpdateFactory) Create(_ shared.ClusterConfigHandle) shared.Cluster {
	return &schedulerHostUpdateCluster{address: f.address, hosts: &hostList{}}
}

type schedulerHostUpdateCluster struct {
	shared.EmptyCluster
	address string
	hosts   *hostList
	handle  shared.ClusterHandle
}

func (c *schedulerHostUpdateCluster) OnInit(handle shared.ClusterHandle) {
	c.handle = handle
	handle.PreInitComplete()
	// Schedule a deferred host-add to exercise the scheduler dispatch path. The Go SDK
	// translates this into an Envoy main-thread event.
	scheduler := handle.NewScheduler()
	scheduler.Schedule(func() {
		hosts, ok := handle.AddHosts(0, []shared.ClusterHostSpec{{Address: c.address, Weight: 1}})
		if ok {
			c.hosts.set(hosts)
		}
	})
}

func (c *schedulerHostUpdateCluster) NewLoadBalancer(_ shared.ClusterLoadBalancerHandle) shared.ClusterLoadBalancer {
	return &schedulerHostUpdateLb{hosts: c.hosts}
}

type schedulerHostUpdateLb struct {
	shared.EmptyClusterLoadBalancer
	hosts                 *hostList
	membershipUpdateCount atomic.Uint64
}

func (lb *schedulerHostUpdateLb) ChooseHost(
	_ shared.ClusterLoadBalancerHandle, _ shared.ClusterLoadBalancerContext, _ shared.ClusterAsyncCompletion,
) (shared.ClusterHost, shared.ClusterAsyncHostSelection, bool) {
	hosts := lb.hosts.get()
	if len(hosts) == 0 {
		return shared.ClusterHost{}, nil, false
	}
	return hosts[0], nil, true
}

func (lb *schedulerHostUpdateLb) OnHostMembershipUpdate(_ shared.ClusterLoadBalancerHandle, _, _ uint64) {
	lb.membershipUpdateCount.Add(1)
}

// =============================================================================
// Lifecycle callbacks (server_initialized / drain_started / shutdown).
// =============================================================================
//
// The shutdown branch is the most important one — it locks in the bug fix where the
// completion callback was being silently dropped at the C trampoline layer. If the bug
// regresses, server teardown will hang waiting for an event_cb that never arrives.

type lifecycleCallbacksConfigFactory struct {
	shared.EmptyClusterConfigFactory
}

func (f *lifecycleCallbacksConfigFactory) Create(_ shared.ClusterConfigHandle, config []byte) (shared.ClusterFactory, error) {
	return &lifecycleCallbacksFactory{address: addressFromConfig(config)}, nil
}

type lifecycleCallbacksFactory struct {
	shared.EmptyClusterFactory
	address string
}

func (f *lifecycleCallbacksFactory) Create(_ shared.ClusterConfigHandle) shared.Cluster {
	return &lifecycleCallbacksCluster{address: f.address, hosts: &hostList{}}
}

type lifecycleCallbacksCluster struct {
	shared.EmptyCluster
	address string
	hosts   *hostList
}

func (c *lifecycleCallbacksCluster) OnInit(handle shared.ClusterHandle) {
	sdk.Log(shared.LogLevelInfo, "cluster lifecycle: on_init called")
	if hosts, ok := handle.AddHosts(0, []shared.ClusterHostSpec{{Address: c.address, Weight: 1}}); ok {
		c.hosts.set(hosts)
	}
	handle.PreInitComplete()
}

func (c *lifecycleCallbacksCluster) NewLoadBalancer(_ shared.ClusterLoadBalancerHandle) shared.ClusterLoadBalancer {
	return &lifecycleCallbacksLb{hosts: c.hosts}
}

func (c *lifecycleCallbacksCluster) OnServerInitialized(_ shared.ClusterHandle) {
	sdk.Log(shared.LogLevelInfo, "cluster lifecycle: on_server_initialized called")
}

func (c *lifecycleCallbacksCluster) OnDrainStarted(_ shared.ClusterHandle) {
	sdk.Log(shared.LogLevelInfo, "cluster lifecycle: on_drain_started called")
}

func (c *lifecycleCallbacksCluster) OnShutdown(_ shared.ClusterHandle, completion func()) {
	sdk.Log(shared.LogLevelInfo, "cluster lifecycle: on_shutdown called")
	// MUST call completion exactly once; if the trampoline regresses, Envoy hangs.
	completion()
}

type lifecycleCallbacksLb struct {
	shared.EmptyClusterLoadBalancer
	hosts *hostList
}

func (lb *lifecycleCallbacksLb) ChooseHost(
	_ shared.ClusterLoadBalancerHandle, _ shared.ClusterLoadBalancerContext, _ shared.ClusterAsyncCompletion,
) (shared.ClusterHost, shared.ClusterAsyncHostSelection, bool) {
	hosts := lb.hosts.get()
	if len(hosts) == 0 {
		return shared.ClusterHost{}, nil, false
	}
	return hosts[0], nil, true
}
