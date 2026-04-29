//go:generate mockgen -source=cluster.go -destination=mocks/mock_cluster.go -package=mocks
package shared

import "unsafe"

// Cluster extension SDK surface for dynamic modules.
//
// Mirrors the Rust SDK's `cluster` module. A cluster extension implements a complete cluster
// type: it manages the host set (add/remove/find hosts), runs an initial discovery phase,
// optionally provides custom load balancing per worker thread, and integrates with the rest of
// Envoy's lifecycle (server_initialized, drain_started, shutdown).
//
// This is the most complex extension surface — it composes lifecycle, host management, and
// (optional) per-worker load balancing in one extension type.
//
// Lifecycle:
//   1. ClusterConfigFactory.Create on the main thread → ClusterFactory.
//   2. ClusterFactory.Create on the main thread → Cluster (one per cluster instance).
//   3. Cluster.OnInit fires on the main thread; the module performs initial host discovery
//      and signals readiness via handle.PreInitComplete. Envoy blocks routing to this cluster
//      until that signal.
//   4. Cluster.NewLoadBalancer is called once per worker thread, returning a per-worker
//      ClusterLoadBalancer.
//   5. ClusterLoadBalancer.ChooseHost is called for each upstream selection on that worker.
//      It can resolve synchronously (returning a host) or asynchronously (returning a
//      ClusterAsyncHostSelection; the module signals completion via the SDK-provided
//      ClusterAsyncCompletion handed to ChooseHost).
//   6. ClusterLoadBalancer.OnHostMembershipUpdate notifies the worker's LB of host-set changes.
//   7. Cluster.OnServerInitialized / OnDrainStarted / OnShutdown fire on the main thread at
//      their respective lifecycle stages. OnShutdown MUST call completion exactly once.

// ClusterHost is an opaque, Envoy-owned pointer to a host in the cluster's host set. The
// module receives ClusterHost values from add-hosts and find-host operations and passes them
// back to host-management and LB-selection callbacks. Modules MUST NOT dereference these
// pointers; they remain valid only while the host is part of the cluster's host set.
//
// The zero value (nil) represents "no host" and is returned by lookup functions when the
// host is not found.
type ClusterHost struct {
	// p is the opaque Envoy-owned pointer. Module code MUST NOT dereference this; it is
	// strictly a round-trip handle.
	p unsafe.Pointer
}

// IsNil reports whether the host handle is the zero/no-host value.
func (h ClusterHost) IsNil() bool { return h.p == nil }

// UnsafeClusterHost is internal SDK plumbing — do NOT call from module code. It exposes the
// raw pointer so the ABI bridge can pass it back to Envoy.
//
//go:nosplit
func UnsafeClusterHost(p unsafe.Pointer) ClusterHost { return ClusterHost{p: p} }

// UnsafeClusterHostPtr is internal SDK plumbing — do NOT call from module code.
//
//go:nosplit
func UnsafeClusterHostPtr(h ClusterHost) unsafe.Pointer { return h.p }

// ClusterHostSpec describes a host to add to the cluster via ClusterHandle.AddHosts. Region/
// Zone/SubZone may be empty when no locality is set. MetadataPairs is a flat slice of
// (filterName, key, value) triples; each triple consists of three consecutive strings, all
// values stored as strings on the Envoy side. nil/empty MetadataPairs means no metadata.
type ClusterHostSpec struct {
	// Address is the host address in "ip:port" form (e.g. "10.0.0.1:8080").
	Address string
	// Weight is the LB weight (1–128).
	Weight uint32
	// Region/Zone/SubZone form the host's locality. Empty strings indicate no value.
	Region, Zone, SubZone string
	// MetadataPairs is a flat slice of (filterName, key, value) triples.
	MetadataPairs []string
}

// ClusterAsyncCompletion is the SDK-provided completion handle passed to
// ClusterLoadBalancer.ChooseHost. When ChooseHost returns asynchronously, the module stores
// this handle and calls Complete exactly once when the selection finishes (unless
// OnCancelHostSelection has already fired). Complete is safe to call from any goroutine.
type ClusterAsyncCompletion interface {
	// Complete delivers the final host (or the zero ClusterHost for failure), with a
	// free-form details string recorded as the resolution outcome. Calling Complete more
	// than once is a no-op; calling Complete after OnCancelHostSelection is a no-op.
	Complete(host ClusterHost, details string)
}

// ClusterAsyncHostSelection is a module-owned handle returned from ClusterLoadBalancer.ChooseHost
// when the selection is performed asynchronously. The module typically stores the
// ClusterAsyncCompletion it was handed and signals completion via that handle; this interface
// exists so the SDK can notify the module of cancellation.
type ClusterAsyncHostSelection interface {
	// Cancel is called by the SDK when Envoy cancels async host selection (e.g., the stream
	// was destroyed before the module produced a result). After this returns, the module MUST
	// NOT call Complete on the associated ClusterAsyncCompletion. The module should release
	// any resources tied to the selection.
	Cancel()
}

// Cluster is the module-side cluster object — one instance per cluster configuration. All
// hooks except NewLoadBalancer fire on the main thread.
type Cluster interface {
	// OnInit is called when cluster initialization begins. The module should perform initial
	// host discovery and call handle.PreInitComplete when the initial set is ready (Envoy
	// blocks routing until then).
	OnInit(handle ClusterHandle)

	// NewLoadBalancer is called once per worker thread. The returned ClusterLoadBalancer is
	// owned by Envoy for the lifetime of the worker; its callbacks fire on that worker.
	// Returning nil indicates failure.
	NewLoadBalancer(handle ClusterLoadBalancerHandle) ClusterLoadBalancer

	// OnServerInitialized fires after server initialization completes — appropriate for
	// starting background discovery tasks that depend on server-wide facilities.
	OnServerInitialized(handle ClusterHandle)

	// OnDrainStarted fires when Envoy begins draining. Cluster operations are still allowed.
	OnDrainStarted(handle ClusterHandle)

	// OnShutdown fires during ShutdownExit. The module MUST call completion() exactly once
	// when its async cleanup is done; Envoy waits for it before terminating.
	OnShutdown(handle ClusterHandle, completion func())

	// OnDestroy is called when the cluster instance is destroyed.
	OnDestroy()
}

// EmptyCluster is a no-op Cluster. OnShutdown calls completion immediately.
type EmptyCluster struct{}

func (*EmptyCluster) OnInit(handle ClusterHandle)         { handle.PreInitComplete() }
func (*EmptyCluster) NewLoadBalancer(_ ClusterLoadBalancerHandle) ClusterLoadBalancer { return nil }
func (*EmptyCluster) OnServerInitialized(_ ClusterHandle)                              {}
func (*EmptyCluster) OnDrainStarted(_ ClusterHandle)                                   {}
func (*EmptyCluster) OnShutdown(_ ClusterHandle, completion func())                    { completion() }
func (*EmptyCluster) OnDestroy()                                                       {}

// ClusterFactory creates the per-cluster Cluster instance.
type ClusterFactory interface {
	Create(handle ClusterConfigHandle) Cluster
	OnDestroy()
}

// EmptyClusterFactory is a no-op ClusterFactory.
type EmptyClusterFactory struct{}

func (*EmptyClusterFactory) Create(_ ClusterConfigHandle) Cluster { return &EmptyCluster{} }
func (*EmptyClusterFactory) OnDestroy()                            {}

// ClusterConfigFactory is the top-level factory the module registers via
// sdk.RegisterClusterConfigFactories.
type ClusterConfigFactory interface {
	Create(handle ClusterConfigHandle, unparsedConfig []byte) (ClusterFactory, error)
}

// EmptyClusterConfigFactory is a no-op ClusterConfigFactory.
type EmptyClusterConfigFactory struct{}

func (*EmptyClusterConfigFactory) Create(_ ClusterConfigHandle, _ []byte) (ClusterFactory, error) {
	return &EmptyClusterFactory{}, nil
}

// ClusterConfigHandle is the config-context handle. Supports labeled metrics.
type ClusterConfigHandle interface {
	DefineCounter(name string, labelNames []string) (MetricID, MetricsResult)
	IncrementCounter(id MetricID, labelValues []string, value uint64) MetricsResult
	DefineGauge(name string, labelNames []string) (MetricID, MetricsResult)
	SetGauge(id MetricID, labelValues []string, value uint64) MetricsResult
	IncrementGauge(id MetricID, labelValues []string, value uint64) MetricsResult
	DecrementGauge(id MetricID, labelValues []string, value uint64) MetricsResult
	DefineHistogram(name string, labelNames []string) (MetricID, MetricsResult)
	RecordHistogramValue(id MetricID, labelValues []string, value uint64) MetricsResult
}

// ClusterHandle is the per-cluster handle for managing hosts, scheduling main-thread events,
// and issuing HTTP callouts. Methods MUST be called on the main thread (use NewScheduler to
// dispatch from other threads).
type ClusterHandle interface {
	// PreInitComplete signals that initial host discovery is done; Envoy starts routing to
	// the cluster after this call. The module MUST call this during or after OnInit.
	PreInitComplete()

	// AddHosts adds hosts to the cluster at the given priority. Returns the resulting
	// ClusterHost handles in the same order as specs (or nil on failure — when any host fails
	// to add, no hosts are added).
	AddHosts(priority uint32, specs []ClusterHostSpec) ([]ClusterHost, bool)

	// RemoveHosts removes the given hosts from the cluster. Returns the number that were
	// successfully removed.
	RemoveHosts(hosts []ClusterHost) uint64

	// UpdateHostHealth updates a host's health status. Useful when the module manages health
	// externally (custom health probes, EDS-like signals).
	UpdateHostHealth(host ClusterHost, status HostHealth) bool

	// FindHostByAddress looks up a host by its "ip:port" address. Returns 0 if not found.
	FindHostByAddress(address string) ClusterHost

	// HttpCallout sends a main-thread HTTP request. Result is delivered via the supplied
	// HttpCalloutCallback. MUST be called on the main thread.
	HttpCallout(clusterName string, headers [][2]string, body []byte, timeoutMs uint64,
		cb HttpCalloutCallback) (HttpCalloutInitResult, uint64)

	// NewScheduler returns a main-thread scheduler bound to this cluster. Safe from any
	// goroutine.
	NewScheduler() Scheduler
}

// ClusterLoadBalancer is the per-worker LB associated with a Cluster. ChooseHost is called for
// every upstream selection on this worker.
type ClusterLoadBalancer interface {
	// ChooseHost picks a host for the request. completion is the SDK-provided completion
	// handle the module uses to signal an asynchronous result. Returns:
	//   - (host, nil, true) for synchronous success (completion can be ignored)
	//   - (zero, async, true) when async resolution is in flight; the module MUST eventually
	//     call completion.Complete (unless async.Cancel fires first)
	//   - (zero, nil, false) for synchronous failure (no host selected; request fails)
	ChooseHost(handle ClusterLoadBalancerHandle, ctx ClusterLoadBalancerContext,
		completion ClusterAsyncCompletion) (ClusterHost, ClusterAsyncHostSelection, bool)

	// OnHostMembershipUpdate notifies the per-worker LB of host-set changes. During the
	// callback the module can enumerate added/removed hosts via
	// handle.GetMemberUpdateHostAddress.
	OnHostMembershipUpdate(handle ClusterLoadBalancerHandle, numHostsAdded, numHostsRemoved uint64)

	// OnDestroy is called when the per-worker LB is destroyed.
	OnDestroy()
}

// EmptyClusterLoadBalancer is a no-op ClusterLoadBalancer that always returns sync failure.
type EmptyClusterLoadBalancer struct{}

func (*EmptyClusterLoadBalancer) ChooseHost(_ ClusterLoadBalancerHandle, _ ClusterLoadBalancerContext, _ ClusterAsyncCompletion) (ClusterHost, ClusterAsyncHostSelection, bool) {
	return ClusterHost{}, nil, false
}
func (*EmptyClusterLoadBalancer) OnHostMembershipUpdate(_ ClusterLoadBalancerHandle, _, _ uint64) {}
func (*EmptyClusterLoadBalancer) OnDestroy()                                                      {}

// ClusterLoadBalancerHandle is the per-worker LB handle. All methods MUST be called on the
// owning worker thread (i.e., from inside a ClusterLoadBalancer callback).
//
// This mirrors LoadBalancerHandle but is bound to a cluster's per-worker LB pointer rather
// than a standalone load-balancer pointer. The two cannot be used interchangeably.
type ClusterLoadBalancerHandle interface {
	// ---- cluster-level info ----

	GetClusterName() UnsafeEnvoyBuffer

	// ---- host queries ----

	GetHostsCount(priority uint32) uint64
	GetHealthyHostCount(priority uint32) uint64
	GetDegradedHostsCount(priority uint32) uint64
	GetPrioritySetSize() uint64

	// GetHealthyHost returns a healthy-host handle by index (or 0 if out of bounds).
	GetHealthyHost(priority uint32, index uint64) ClusterHost
	GetHealthyHostAddress(priority uint32, index uint64) (UnsafeEnvoyBuffer, bool)
	GetHealthyHostWeight(priority uint32, index uint64) uint32

	// GetHost returns an all-hosts handle by index (or 0 if out of bounds).
	GetHost(priority uint32, index uint64) ClusterHost
	GetHostAddress(priority uint32, index uint64) (UnsafeEnvoyBuffer, bool)
	GetHostWeight(priority uint32, index uint64) uint32
	GetHostHealth(priority uint32, index uint64) HostHealth
	GetHostHealthByAddress(address string) (HostHealth, bool)
	GetHostStat(priority uint32, index uint64, stat HostStat) uint64
	GetHostLocality(priority uint32, index uint64) (region, zone, subZone UnsafeEnvoyBuffer, ok bool)

	// FindHostByAddress is the per-worker counterpart to ClusterHandle.FindHostByAddress;
	// safe to call during host selection.
	FindHostByAddress(address string) ClusterHost

	// SetHostData / GetHostData attach/retrieve a module-defined opaque uintptr per host,
	// per worker (NOT shared across workers).
	SetHostData(priority uint32, index uint64, data uintptr) bool
	GetHostData(priority uint32, index uint64) (uintptr, bool)

	// Host metadata (per-endpoint).
	GetHostMetadataString(priority uint32, index uint64, filterName, key string) (UnsafeEnvoyBuffer, bool)
	GetHostMetadataNumber(priority uint32, index uint64, filterName, key string) (float64, bool)
	GetHostMetadataBool(priority uint32, index uint64, filterName, key string) (bool, bool)

	// ---- locality buckets (healthy hosts) ----

	GetLocalityCount(priority uint32) uint64
	GetLocalityHostCount(priority uint32, localityIndex uint64) uint64
	GetLocalityHostAddress(priority uint32, localityIndex, hostIndex uint64) (UnsafeEnvoyBuffer, bool)
	GetLocalityWeight(priority uint32, localityIndex uint64) uint32

	// ---- membership update enumeration ----

	// GetMemberUpdateHostAddress returns the address of an added (isAdded=true) or removed
	// (false) host at the given index. Only valid inside OnHostMembershipUpdate.
	GetMemberUpdateHostAddress(index uint64, isAdded bool) (UnsafeEnvoyBuffer, bool)
}

// ClusterLoadBalancerContext is the per-request context passed to ChooseHost. Memory accessed
// through this handle is only valid for the duration of the ChooseHost callback (synchronous
// case) or until the async-handle is completed/cancelled (async case).
type ClusterLoadBalancerContext interface {
	ComputeHashKey() (uint64, bool)
	GetDownstreamHeadersSize() uint64
	GetDownstreamHeaders() [][2]UnsafeEnvoyBuffer
	GetDownstreamHeader(key string, index uint64) (UnsafeEnvoyBuffer, uint64, bool)
	GetHostSelectionRetryCount() uint32
	ShouldSelectAnotherHost(handle ClusterLoadBalancerHandle, priority uint32, index uint64) bool
	GetOverrideHost() (address UnsafeEnvoyBuffer, strict bool, ok bool)

	// GetDownstreamConnectionSNI returns the SNI from the downstream connection associated
	// with this request, if any.
	GetDownstreamConnectionSNI() (UnsafeEnvoyBuffer, bool)
}
