//go:generate mockgen -source=dns_resolver.go -destination=mocks/mock_dns_resolver.go -package=mocks
package shared

// DNS resolver SDK surface for dynamic modules.
//
// Mirrors the Rust SDK's `dns_resolver` module. A module that exposes a custom DNS resolver
// implements DnsResolverConfigFactory and registers it from an init() function via
// sdk.RegisterDnsResolverConfigFactories.
//
// Lifecycle: Envoy calls DnsResolverConfigFactory.Create exactly once per resolver
// configuration. Each Resolve call creates a query that the module executes asynchronously and
// completes by invoking DnsResolverConfigHandle.ResolveComplete on any thread (Envoy posts the
// result to the correct dispatcher thread internally).

// DnsLookupFamily specifies which address families to look up. Corresponds to
// envoy_dynamic_module_type_dns_lookup_family / Network::DnsLookupFamily in Envoy.
type DnsLookupFamily uint32

const (
	// DnsLookupFamilyV4Only — only A records.
	DnsLookupFamilyV4Only DnsLookupFamily = iota
	// DnsLookupFamilyV6Only — only AAAA records.
	DnsLookupFamilyV6Only
	// DnsLookupFamilyAuto — prefer the family of the source address; fall back to either.
	DnsLookupFamilyAuto
	// DnsLookupFamilyV4Preferred — prefer A records; fall back to AAAA.
	DnsLookupFamilyV4Preferred
	// DnsLookupFamilyAll — return both A and AAAA records.
	DnsLookupFamilyAll
)

// DnsResolutionStatus is the final status of a DNS resolution. Corresponds to
// envoy_dynamic_module_type_dns_resolution_status / Network::DnsResolver::ResolutionStatus.
type DnsResolutionStatus uint32

const (
	// DnsResolutionStatusCompleted — the resolution completed (with zero or more addresses).
	DnsResolutionStatusCompleted DnsResolutionStatus = iota
	// DnsResolutionStatusFailure — the resolution failed.
	DnsResolutionStatusFailure
)

// DnsAddress is a single resolved DNS address with TTL.
type DnsAddress struct {
	// Address is an "ip:port" string. The port MUST be 0 because DNS resolution only produces
	// IP addresses; the actual port comes from the cluster/endpoint configuration.
	Address string
	// TTLSeconds is the time-to-live in seconds.
	TTLSeconds uint32
}

// DnsResolver is the module-side DNS resolver. A single instance is created per Envoy DNS
// resolver and is invoked for every resolution request on that resolver. Implementations must
// be safe for concurrent calls; Envoy may issue multiple Resolve calls in parallel.
//
// Resolution is asynchronous: the module starts the lookup and returns immediately. When the
// result is available, the module MUST call handle.ResolveComplete with the same queryID it was
// given. If the query is cancelled (Cancel) or the resolver is destroyed, the module MUST NOT
// call ResolveComplete for that query.
type DnsResolver interface {
	// Resolve initiates an asynchronous DNS resolution. The module SHOULD start the lookup
	// (typically on a background thread) and return a handle/query identifier to its in-flight
	// state. handle.ResolveComplete MUST be called once for each successful Resolve unless
	// Cancel is invoked. Returning nil indicates the resolution could not be started.
	//
	// The returned query identifier is opaque — it is only used by the runtime to drive Cancel
	// and is not the same as queryID (which is Envoy's identifier for the result delivery).
	Resolve(handle DnsResolverConfigHandle, dnsName string, family DnsLookupFamily, queryID uint64) any

	// Cancel cancels an in-flight query that was previously returned by Resolve. After this
	// call, the module MUST NOT call handle.ResolveComplete for the cancelled query. The
	// module should clean up any resources associated with the query.
	Cancel(query any)

	// ResetNetworking is called to reset the resolver's networking state, typically in
	// response to a network change (e.g., WiFi to cellular). The module may recreate
	// connections, re-read system configuration, etc.
	ResetNetworking()

	// OnDestroy is called when the resolver instance is destroyed. The module should release
	// the resolver and shut down any background threads.
	OnDestroy()
}

// EmptyDnsResolver is a no-op DnsResolver. Resolve returns nil; ResetNetworking and OnDestroy
// do nothing.
type EmptyDnsResolver struct{}

func (*EmptyDnsResolver) Resolve(_ DnsResolverConfigHandle, _ string, _ DnsLookupFamily, _ uint64) any {
	return nil
}
func (*EmptyDnsResolver) Cancel(_ any)     {}
func (*EmptyDnsResolver) ResetNetworking() {}
func (*EmptyDnsResolver) OnDestroy()       {}

// DnsResolverFactory creates the per-Envoy-resolver DnsResolver instance.
type DnsResolverFactory interface {
	// Create creates the DnsResolver for an Envoy DNS resolver instance.
	Create(handle DnsResolverConfigHandle) DnsResolver

	// OnDestroy is called when the factory itself (the configuration) is destroyed.
	OnDestroy()
}

// EmptyDnsResolverFactory is a no-op DnsResolverFactory.
type EmptyDnsResolverFactory struct{}

func (*EmptyDnsResolverFactory) Create(_ DnsResolverConfigHandle) DnsResolver {
	return &EmptyDnsResolver{}
}
func (*EmptyDnsResolverFactory) OnDestroy() {}

// DnsResolverConfigFactory is the top-level factory the module registers via
// sdk.RegisterDnsResolverConfigFactories.
type DnsResolverConfigFactory interface {
	// Create parses unparsedConfig and returns a DnsResolverFactory.
	Create(handle DnsResolverConfigHandle, unparsedConfig []byte) (DnsResolverFactory, error)
}

// EmptyDnsResolverConfigFactory is a no-op DnsResolverConfigFactory.
type EmptyDnsResolverConfigFactory struct{}

func (*EmptyDnsResolverConfigFactory) Create(_ DnsResolverConfigHandle, _ []byte) (DnsResolverFactory, error) {
	return &EmptyDnsResolverFactory{}, nil
}

// DnsResolverConfigHandle is the handle used by the module to call back into Envoy. It is valid
// from when the configuration is created until the corresponding factory's OnDestroy returns.
//
// ResolveComplete is safe to call from any thread; Envoy posts the result to the correct
// dispatcher.
//
// Metric definitions on this handle support label names; the values passed at increment-time
// MUST match the order and length of the names declared at definition-time.
type DnsResolverConfigHandle interface {
	// ResolveComplete delivers DNS resolution results back to Envoy. queryID MUST match the
	// queryID supplied to DnsResolver.Resolve. status indicates success/failure; details is a
	// human-readable message; addresses is the resolved set with TTLs.
	//
	// Safe to call from any thread; Envoy will post the result onto the correct dispatcher.
	// Buffer data is copied synchronously before the call returns.
	ResolveComplete(queryID uint64, status DnsResolutionStatus, details string, addresses []DnsAddress)

	// DefineCounter creates a per-config counter template with the given label names. Labels
	// passed at increment-time MUST match this order and length.
	DefineCounter(name string, labelNames []string) (MetricID, MetricsResult)

	// IncrementCounter increments a counter by value. labelValues MUST match the labels
	// declared in DefineCounter (length and order).
	IncrementCounter(id MetricID, labelValues []string, value uint64) MetricsResult

	// DefineGauge creates a per-config gauge template with the given label names.
	DefineGauge(name string, labelNames []string) (MetricID, MetricsResult)

	// SetGauge sets a gauge to value.
	SetGauge(id MetricID, labelValues []string, value uint64) MetricsResult

	// IncrementGauge adds value to a gauge.
	IncrementGauge(id MetricID, labelValues []string, value uint64) MetricsResult

	// DecrementGauge subtracts value from a gauge.
	DecrementGauge(id MetricID, labelValues []string, value uint64) MetricsResult

	// DefineHistogram creates a per-config histogram template with the given label names.
	DefineHistogram(name string, labelNames []string) (MetricID, MetricsResult)

	// RecordHistogramValue records value in a histogram.
	RecordHistogramValue(id MetricID, labelValues []string, value uint64) MetricsResult
}
