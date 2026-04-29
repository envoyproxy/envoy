//go:generate mockgen -source=load_balancer.go -destination=mocks/mock_load_balancer.go -package=mocks
package shared

// Load balancer SDK surface for dynamic modules.
//
// Mirrors the Rust SDK's `load_balancer` module. Modules implement custom load-balancing
// algorithms; Envoy retains cluster management, health checking, and connection pooling.
//
// Lifecycle: LoadBalancerConfigFactory.Create is called once on the main thread per
// configuration, returning a LoadBalancerFactory. For each worker thread, the factory's
// Create method is invoked to build a per-worker LoadBalancer. ChooseHost is then called for
// every upstream selection on that worker. OnHostMembershipUpdate informs the worker's LB of
// host-set changes (EDS, health, etc.) — during that callback only, the LB can use
// handle.GetMemberUpdateHostAddress to enumerate added/removed hosts by index.

// HostHealth corresponds to envoy_dynamic_module_type_host_health.
type HostHealth uint32

const (
	// HostHealthUnhealthy — host is currently unhealthy.
	HostHealthUnhealthy HostHealth = 0
	// HostHealthDegraded — host is degraded.
	HostHealthDegraded HostHealth = 1
	// HostHealthHealthy — host is healthy.
	HostHealthHealthy HostHealth = 2
)

// HostStat identifies a per-host stat read by LoadBalancerHandle.GetHostStat. Corresponds to
// envoy_dynamic_module_type_host_stat.
type HostStat uint32

const (
	// HostStatCxConnectFail — total connect failures.
	HostStatCxConnectFail HostStat = 0
	// HostStatCxTotal — total connections.
	HostStatCxTotal HostStat = 1
	// HostStatRqError — total request errors.
	HostStatRqError HostStat = 2
	// HostStatRqSuccess — total successful requests.
	HostStatRqSuccess HostStat = 3
	// HostStatRqTimeout — total request timeouts.
	HostStatRqTimeout HostStat = 4
	// HostStatRqTotal — total requests.
	HostStatRqTotal HostStat = 5
	// HostStatCxActive — active connections.
	HostStatCxActive HostStat = 6
	// HostStatRqActive — active requests.
	HostStatRqActive HostStat = 7
)

// HostSelection is the result of ChooseHost. priority + index identifies the chosen host
// within the healthy hosts at that priority.
type HostSelection struct {
	Priority uint32
	Index    uint32
}

// LoadBalancer is the per-worker module-side load balancer. Envoy creates one per worker
// thread; all per-worker calls happen on the same thread. The module does not need internal
// synchronization for these calls.
type LoadBalancer interface {
	// ChooseHost is called when a host needs to be selected for an upstream request. The
	// module returns (selection, true) to select that host or (_, false) to abort the request.
	//
	// ctx may be nil if the caller did not provide a LoadBalancerContext (rare).
	ChooseHost(handle LoadBalancerHandle, ctx LoadBalancerContext) (HostSelection, bool)

	// OnHostMembershipUpdate is called when the set of hosts in the cluster changes
	// (EDS update, health-check transition, etc.). During this callback the module can
	// enumerate the added/removed hosts via handle.GetMemberUpdateHostAddress(index, isAdded).
	// After the callback returns, the standard host queries reflect the new state.
	OnHostMembershipUpdate(handle LoadBalancerHandle, numHostsAdded, numHostsRemoved uint64)

	// OnDestroy is called when the load balancer instance is destroyed.
	OnDestroy()
}

// EmptyLoadBalancer is a no-op LoadBalancer that always fails host selection.
type EmptyLoadBalancer struct{}

func (*EmptyLoadBalancer) ChooseHost(_ LoadBalancerHandle, _ LoadBalancerContext) (HostSelection, bool) {
	return HostSelection{}, false
}
func (*EmptyLoadBalancer) OnHostMembershipUpdate(_ LoadBalancerHandle, _, _ uint64) {}
func (*EmptyLoadBalancer) OnDestroy()                                                {}

// LoadBalancerFactory creates per-worker LoadBalancer instances.
type LoadBalancerFactory interface {
	// Create creates the LoadBalancer for a worker thread.
	Create(handle LoadBalancerHandle) LoadBalancer

	// OnDestroy is called when the factory is destroyed.
	OnDestroy()
}

// EmptyLoadBalancerFactory is a no-op LoadBalancerFactory.
type EmptyLoadBalancerFactory struct{}

func (*EmptyLoadBalancerFactory) Create(_ LoadBalancerHandle) LoadBalancer {
	return &EmptyLoadBalancer{}
}
func (*EmptyLoadBalancerFactory) OnDestroy() {}

// LoadBalancerConfigFactory is the top-level factory the module registers via
// sdk.RegisterLoadBalancerConfigFactories.
type LoadBalancerConfigFactory interface {
	// Create parses unparsedConfig and returns a LoadBalancerFactory. The handle is valid for
	// the lifetime of the factory and provides metric definitions.
	Create(handle LoadBalancerConfigHandle, unparsedConfig []byte) (LoadBalancerFactory, error)
}

// EmptyLoadBalancerConfigFactory is a no-op LoadBalancerConfigFactory.
type EmptyLoadBalancerConfigFactory struct{}

func (*EmptyLoadBalancerConfigFactory) Create(_ LoadBalancerConfigHandle, _ []byte) (LoadBalancerFactory, error) {
	return &EmptyLoadBalancerFactory{}, nil
}

// LoadBalancerConfigHandle is the config-context handle. It supports labeled metrics in the
// same style as DnsResolverConfigHandle.
type LoadBalancerConfigHandle interface {
	DefineCounter(name string, labelNames []string) (MetricID, MetricsResult)
	IncrementCounter(id MetricID, labelValues []string, value uint64) MetricsResult
	DefineGauge(name string, labelNames []string) (MetricID, MetricsResult)
	SetGauge(id MetricID, labelValues []string, value uint64) MetricsResult
	IncrementGauge(id MetricID, labelValues []string, value uint64) MetricsResult
	DecrementGauge(id MetricID, labelValues []string, value uint64) MetricsResult
	DefineHistogram(name string, labelNames []string) (MetricID, MetricsResult)
	RecordHistogramValue(id MetricID, labelValues []string, value uint64) MetricsResult
}

// LoadBalancerHandle is the per-worker handle for inspecting the host set, reading per-host
// metadata/state, and (during OnHostMembershipUpdate) enumerating added/removed hosts.
//
// All methods MUST be called on the worker thread that owns the LoadBalancer instance.
type LoadBalancerHandle interface {
	// ---- cluster-level info ----

	// GetClusterName returns the name of the cluster this load balancer serves.
	//
	// NOTE: The buffer is owned by Envoy and only valid for the duration of the current
	// callback. Copy if you need to keep it.
	GetClusterName() UnsafeEnvoyBuffer

	// GetHostsCount returns the number of all hosts at the given priority.
	GetHostsCount(priority uint32) uint64

	// GetHealthyHostsCount returns the number of healthy hosts at the given priority.
	GetHealthyHostsCount(priority uint32) uint64

	// GetDegradedHostsCount returns the number of degraded hosts at the given priority.
	GetDegradedHostsCount(priority uint32) uint64

	// GetPrioritySetSize returns the number of priority levels.
	GetPrioritySetSize() uint64

	// ---- host queries by index ----

	// GetHealthyHostAddress returns the address of host index within the healthy hosts at
	// the given priority. priority and index together identify the host that ChooseHost
	// would address.
	GetHealthyHostAddress(priority uint32, index uint64) (UnsafeEnvoyBuffer, bool)

	// GetHealthyHostWeight returns the LB weight (1-128) of a host within the healthy set.
	// Returns 0 if the host was not found.
	GetHealthyHostWeight(priority uint32, index uint64) uint32

	// GetHostHealth returns the health status of a host by index within the all-hosts set.
	GetHostHealth(priority uint32, index uint64) HostHealth

	// GetHostHealthByAddress looks up a host by its "ip:port" address string and returns its
	// health status. Returns false if the address is not in the cluster's host map.
	GetHostHealthByAddress(address string) (HostHealth, bool)

	// GetHostAddress returns the address of a host by index within the all-hosts set.
	GetHostAddress(priority uint32, index uint64) (UnsafeEnvoyBuffer, bool)

	// GetHostWeight returns the LB weight (1-128) of a host by index within the all-hosts
	// set. Returns 0 if the host was not found.
	GetHostWeight(priority uint32, index uint64) uint32

	// GetHostLocality returns the locality (region, zone, sub_zone) of a host by index within
	// the all-hosts set. Useful for zone-aware / locality-aware LB.
	GetHostLocality(priority uint32, index uint64) (region, zone, subZone UnsafeEnvoyBuffer, ok bool)

	// SetHostData stores a module-defined opaque value on a host. Per-LB-instance (per-worker)
	// state, NOT shared across workers; the runtime does no synchronization.
	//
	// data is treated as an opaque bag-of-bits. Use 0 to clear. Returns false if the host
	// was not found.
	SetHostData(priority uint32, index uint64, data uintptr) bool

	// GetHostData retrieves a value previously stored via SetHostData. Returns the stored
	// value and true; (0, false) if the host was not found.
	GetHostData(priority uint32, index uint64) (uintptr, bool)

	// GetHostMetadataString returns the string value of a host's endpoint metadata at the
	// given filter namespace and key. Returns false if the key is missing or not a string.
	GetHostMetadataString(priority uint32, index uint64, filterName, key string) (UnsafeEnvoyBuffer, bool)

	// GetHostMetadataNumber returns the number value of a host's endpoint metadata.
	GetHostMetadataNumber(priority uint32, index uint64, filterName, key string) (float64, bool)

	// GetHostMetadataBool returns the bool value of a host's endpoint metadata.
	GetHostMetadataBool(priority uint32, index uint64, filterName, key string) (bool, bool)

	// GetHostStat returns a per-host stat counter/gauge value (connections, requests, etc.).
	GetHostStat(priority uint32, index uint64, stat HostStat) uint64

	// ---- locality buckets (healthy hosts) ----

	// GetLocalityCount returns the number of locality buckets among the healthy hosts at
	// the given priority.
	GetLocalityCount(priority uint32) uint64

	// GetLocalityHostCount returns the number of healthy hosts in the given locality bucket
	// at the given priority. Returns 0 if localityIndex is out of bounds.
	GetLocalityHostCount(priority uint32, localityIndex uint64) uint64

	// GetLocalityHostAddress returns the address of a host within a locality bucket.
	GetLocalityHostAddress(priority uint32, localityIndex, hostIndex uint64) (UnsafeEnvoyBuffer, bool)

	// GetLocalityWeight returns the locality bucket's LB weight, or 0 if out of bounds or
	// no locality weights are configured.
	GetLocalityWeight(priority uint32, localityIndex uint64) uint32

	// ---- membership update enumeration ----

	// GetMemberUpdateHostAddress returns the address of an added (isAdded=true) or removed
	// (isAdded=false) host at the given index. ONLY valid inside OnHostMembershipUpdate.
	GetMemberUpdateHostAddress(index uint64, isAdded bool) (UnsafeEnvoyBuffer, bool)
}

// LoadBalancerContext is the per-selection context passed to LoadBalancer.ChooseHost. It is
// valid only for the duration of the ChooseHost call; do not retain it.
type LoadBalancerContext interface {
	// ComputeHashKey computes a hash key from the context (e.g., for ring-hash LB). Returns
	// the hash and true if a hash is available.
	ComputeHashKey() (uint64, bool)

	// GetDownstreamHeadersSize returns the number of downstream request headers.
	GetDownstreamHeadersSize() uint64

	// GetDownstreamHeaders returns all downstream request headers.
	//
	// NOTE: The buffers are owned by Envoy and only valid for the duration of the ChooseHost
	// callback. Copy if you need to keep them.
	GetDownstreamHeaders() [][2]UnsafeEnvoyBuffer

	// GetDownstreamHeader returns a downstream header value by key. index selects among
	// multi-value headers; the second return value is the total count of values for the key.
	GetDownstreamHeader(key string, index uint64) (UnsafeEnvoyBuffer, uint64, bool)

	// GetHostSelectionRetryCount returns the maximum number of host-selection retries
	// requested by the context.
	GetHostSelectionRetryCount() uint32

	// ShouldSelectAnotherHost reports whether the load balancer should reject the given host
	// (priority, index in the all-hosts set) and retry selection. Used during retries to
	// avoid re-selecting a previously-attempted host.
	ShouldSelectAnotherHost(handle LoadBalancerHandle, priority uint32, index uint64) bool

	// GetOverrideHost returns the override host preference set by upstream filters, if any.
	// strict, when true, means selection MUST return no host if the override is unavailable.
	GetOverrideHost() (address UnsafeEnvoyBuffer, strict bool, ok bool)
}
