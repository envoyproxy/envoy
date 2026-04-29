package shared

import "testing"

// Compile-time interface conformance for every EmptyXxx helper. If a future change adds a
// method to one of the interfaces below and the corresponding Empty implementation is
// missed, this file fails to compile — surfaced as a fast feedback loop instead of waiting
// for a downstream module's build to break.
//
// Each line is paired so you can read "this Empty satisfies that interface".

// Access logger surfaces.
var (
	_ AccessLoggerConfigFactory = (*EmptyAccessLoggerConfigFactory)(nil)
	_ AccessLoggerFactory       = (*EmptyAccessLoggerFactory)(nil)
	_ AccessLogger              = (*EmptyAccessLogger)(nil)
)

// Bootstrap surfaces.
var (
	_ BootstrapExtensionConfigFactory = (*EmptyBootstrapExtensionConfigFactory)(nil)
	_ BootstrapExtension              = (*EmptyBootstrapExtension)(nil)
)

// Cert validator surfaces.
var (
	_ CertValidatorConfigFactory = (*EmptyCertValidatorConfigFactory)(nil)
	_ CertValidator              = (*EmptyCertValidator)(nil)
)

// Cluster surfaces.
var (
	_ ClusterConfigFactory = (*EmptyClusterConfigFactory)(nil)
	_ ClusterFactory       = (*EmptyClusterFactory)(nil)
	_ Cluster              = (*EmptyCluster)(nil)
	_ ClusterLoadBalancer  = (*EmptyClusterLoadBalancer)(nil)
)

// DNS resolver surfaces.
var (
	_ DnsResolverConfigFactory = (*EmptyDnsResolverConfigFactory)(nil)
	_ DnsResolverFactory       = (*EmptyDnsResolverFactory)(nil)
	_ DnsResolver              = (*EmptyDnsResolver)(nil)
)

// HTTP filter surfaces.
var (
	_ HttpFilterConfigFactory = (*EmptyHttpFilterConfigFactory)(nil)
	_ HttpFilterFactory       = (*EmptyHttpFilterFactory)(nil)
	_ HttpFilter              = (*EmptyHttpFilter)(nil)
)

// Listener filter surfaces.
var (
	_ ListenerFilterConfigFactory = (*EmptyListenerFilterConfigFactory)(nil)
	_ ListenerFilterFactory       = (*EmptyListenerFilterFactory)(nil)
	_ ListenerFilter              = (*EmptyListenerFilter)(nil)
)

// Load balancer surfaces.
var (
	_ LoadBalancerConfigFactory = (*EmptyLoadBalancerConfigFactory)(nil)
	_ LoadBalancerFactory       = (*EmptyLoadBalancerFactory)(nil)
	_ LoadBalancer              = (*EmptyLoadBalancer)(nil)
)

// Matcher surfaces.
var (
	_ MatcherConfigFactory = (*EmptyMatcherConfigFactory)(nil)
	_ Matcher              = (*EmptyMatcher)(nil)
)

// Network filter surfaces.
var (
	_ NetworkFilterConfigFactory = (*EmptyNetworkFilterConfigFactory)(nil)
	_ NetworkFilterFactory       = (*EmptyNetworkFilterFactory)(nil)
	_ NetworkFilter              = (*EmptyNetworkFilter)(nil)
)

// Tracer surfaces.
var (
	_ TracerConfigFactory = (*EmptyTracerConfigFactory)(nil)
	_ Tracer              = (*EmptyTracer)(nil)
	_ TracerSpan          = (*EmptyTracerSpan)(nil)
)

// Transport socket surfaces.
var (
	_ TransportSocketFactoryConfigFactory = (*EmptyTransportSocketFactoryConfigFactory)(nil)
	_ TransportSocketFactory              = (*EmptyTransportSocketFactory)(nil)
	_ TransportSocket                     = (*EmptyTransportSocket)(nil)
)

// UDP listener filter surfaces.
var (
	_ UdpListenerFilterConfigFactory = (*EmptyUdpListenerFilterConfigFactory)(nil)
	_ UdpListenerFilterFactory       = (*EmptyUdpListenerFilterFactory)(nil)
	_ UdpListenerFilter              = (*EmptyUdpListenerFilter)(nil)
)

// Upstream HTTP/TCP bridge surfaces.
var (
	_ UpstreamHttpTcpBridgeConfigFactory = (*EmptyUpstreamHttpTcpBridgeConfigFactory)(nil)
	_ UpstreamHttpTcpBridgeFactory       = (*EmptyUpstreamHttpTcpBridgeFactory)(nil)
	_ UpstreamHttpTcpBridge              = (*EmptyUpstreamHttpTcpBridge)(nil)
)

// -----------------------------------------------------------------------------
// Runtime no-panic checks for the side-effect-free Emptys.
//
// EmptyBootstrapExtensionConfigFactory.Create is intentionally NOT called here: it invokes
// handle.SignalInitComplete on the supplied handle, which is a real side effect, not a
// no-op. Constructing a stub handle that satisfies the 30+ method
// BootstrapExtensionConfigHandle interface just to exercise a side-effecting Create would
// add maintenance cost without locking down anything the compile-time check above doesn't
// already cover.
// -----------------------------------------------------------------------------

func TestEmptyConfigFactories_CreateDoesNotPanic(t *testing.T) {
	// Each Create returns the documented (factory, nil) pair. The exact return values
	// aren't important — just that the call doesn't panic and returns a non-nil factory
	// where applicable.

	if f, err := (&EmptyAccessLoggerConfigFactory{}).Create(nil, nil); err != nil || f == nil {
		t.Errorf("EmptyAccessLoggerConfigFactory.Create returned (%v, %v)", f, err)
	}
	if f, err := (&EmptyCertValidatorConfigFactory{}).Create("", nil); err != nil || f == nil {
		t.Errorf("EmptyCertValidatorConfigFactory.Create returned (%v, %v)", f, err)
	}
	if f, err := (&EmptyClusterConfigFactory{}).Create(nil, nil); err != nil || f == nil {
		t.Errorf("EmptyClusterConfigFactory.Create returned (%v, %v)", f, err)
	}
	if f, err := (&EmptyDnsResolverConfigFactory{}).Create(nil, nil); err != nil || f == nil {
		t.Errorf("EmptyDnsResolverConfigFactory.Create returned (%v, %v)", f, err)
	}
	if f, err := (&EmptyHttpFilterConfigFactory{}).Create(nil, nil); err != nil || f == nil {
		t.Errorf("EmptyHttpFilterConfigFactory.Create returned (%v, %v)", f, err)
	}
	if f, err := (&EmptyListenerFilterConfigFactory{}).Create(nil, nil); err != nil || f == nil {
		t.Errorf("EmptyListenerFilterConfigFactory.Create returned (%v, %v)", f, err)
	}
	if f, err := (&EmptyLoadBalancerConfigFactory{}).Create(nil, nil); err != nil || f == nil {
		t.Errorf("EmptyLoadBalancerConfigFactory.Create returned (%v, %v)", f, err)
	}
	if f, err := (&EmptyMatcherConfigFactory{}).Create("", nil); err != nil || f == nil {
		t.Errorf("EmptyMatcherConfigFactory.Create returned (%v, %v)", f, err)
	}
	if f, err := (&EmptyNetworkFilterConfigFactory{}).Create(nil, nil); err != nil || f == nil {
		t.Errorf("EmptyNetworkFilterConfigFactory.Create returned (%v, %v)", f, err)
	}
	if f, err := (&EmptyTracerConfigFactory{}).Create(nil, nil); err != nil || f == nil {
		t.Errorf("EmptyTracerConfigFactory.Create returned (%v, %v)", f, err)
	}
	if f, err := (&EmptyTransportSocketFactoryConfigFactory{}).Create("", nil, false); err != nil || f == nil {
		t.Errorf("EmptyTransportSocketFactoryConfigFactory.Create returned (%v, %v)", f, err)
	}
	if f, err := (&EmptyUdpListenerFilterConfigFactory{}).Create(nil, nil); err != nil || f == nil {
		t.Errorf("EmptyUdpListenerFilterConfigFactory.Create returned (%v, %v)", f, err)
	}
	if f, err := (&EmptyUpstreamHttpTcpBridgeConfigFactory{}).Create("", nil); err != nil || f == nil {
		t.Errorf("EmptyUpstreamHttpTcpBridgeConfigFactory.Create returned (%v, %v)", f, err)
	}
}

func TestEmptyOnDestroyDoesNotPanic(t *testing.T) {
	// Every Empty type has an OnDestroy(); none should panic on a zero receiver. This is
	// the universal teardown contract.
	(&EmptyAccessLoggerFactory{}).OnDestroy()
	(&EmptyAccessLogger{}).OnDestroy()
	(&EmptyBootstrapExtension{}).OnDestroy()
	(&EmptyCertValidator{}).OnDestroy()
	(&EmptyClusterFactory{}).OnDestroy()
	(&EmptyCluster{}).OnDestroy()
	(&EmptyClusterLoadBalancer{}).OnDestroy()
	(&EmptyDnsResolverFactory{}).OnDestroy()
	(&EmptyDnsResolver{}).OnDestroy()
	(&EmptyHttpFilterFactory{}).OnDestroy()
	(&EmptyHttpFilter{}).OnDestroy()
	(&EmptyListenerFilterFactory{}).OnDestroy()
	(&EmptyListenerFilter{}).OnDestroy()
	(&EmptyLoadBalancerFactory{}).OnDestroy()
	(&EmptyLoadBalancer{}).OnDestroy()
	(&EmptyMatcher{}).OnDestroy()
	(&EmptyNetworkFilterFactory{}).OnDestroy()
	(&EmptyNetworkFilter{}).OnDestroy()
	(&EmptyTracer{}).OnDestroy()
	(&EmptyTracerSpan{}).OnDestroy()
	(&EmptyTransportSocketFactory{}).OnDestroy()
	(&EmptyTransportSocket{}).OnDestroy()
	(&EmptyUdpListenerFilterFactory{}).OnDestroy()
	(&EmptyUdpListenerFilter{}).OnDestroy()
	(&EmptyUpstreamHttpTcpBridgeFactory{}).OnDestroy()
	(&EmptyUpstreamHttpTcpBridge{}).OnDestroy()
}

// EmptyAccessLogger has Log/Flush in addition to OnDestroy. Verify these are no-ops too.
func TestEmptyAccessLogger_NoopMethods(t *testing.T) {
	l := &EmptyAccessLogger{}
	l.Log(nil, AccessLogTypeNotSet)
	l.Flush()
	l.OnDestroy()
}
