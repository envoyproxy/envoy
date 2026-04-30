package sdk

import (
	"fmt"
	"sync/atomic"
	"unsafe"

	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

// For built-in plugin factories in the host binary directly. DO NOT use this for independently
// compiled module or plugins.
var httpFilterConfigFactoryRegistry = make(map[string]shared.HttpFilterConfigFactory)
var networkFilterConfigFactoryRegistry = make(map[string]shared.NetworkFilterConfigFactory)

// NewHttpFilterFactory creates a new plugin factory for the given plugin name and unparsed config.
func NewHttpFilterFactory(handle shared.HttpFilterConfigHandle, name string,
	unparsedConfig []byte) (shared.HttpFilterFactory, error) {
	configFactory := httpFilterConfigFactoryRegistry[name]
	if configFactory == nil {
		return nil, fmt.Errorf("failed to get plugin config factory")
	}
	return configFactory.Create(handle, unparsedConfig)
}

// GetHttpFilterConfigFactory gets the plugin config factory for the given plugin name.
func GetHttpFilterConfigFactory(name string) shared.HttpFilterConfigFactory {
	return httpFilterConfigFactoryRegistry[name]
}

// RegisterHttpFilterConfigFactories registers plugin config factories for plugins in the composer
// binary itself. This function MUST only be called from init() functions.
func RegisterHttpFilterConfigFactories(factories map[string]shared.HttpFilterConfigFactory) {
	for name, factory := range factories {
		if _, ok := httpFilterConfigFactoryRegistry[name]; ok {
			// Same plugin name should only be register once in same lib.
			panic("plugin config factory already registered: " + name)
		}
		httpFilterConfigFactoryRegistry[name] = factory
	}
}

// ---------------------------------------------------------------------------
// Network filter registry
// ---------------------------------------------------------------------------

// NewNetworkFilterFactory creates a new network filter factory for the given plugin name and
// unparsed config. Returns an error if no factory is registered for name.
func NewNetworkFilterFactory(handle shared.NetworkFilterConfigHandle, name string,
	unparsedConfig []byte) (shared.NetworkFilterFactory, error) {
	configFactory := networkFilterConfigFactoryRegistry[name]
	if configFactory == nil {
		return nil, fmt.Errorf("failed to get network filter config factory for %s", name)
	}
	return configFactory.Create(handle, unparsedConfig)
}

// GetNetworkFilterConfigFactory returns the registered network filter config factory for name,
// or nil if no factory is registered.
func GetNetworkFilterConfigFactory(name string) shared.NetworkFilterConfigFactory {
	return networkFilterConfigFactoryRegistry[name]
}

// RegisterNetworkFilterConfigFactories registers network filter config factories. MUST only be
// called from init() functions.
func RegisterNetworkFilterConfigFactories(factories map[string]shared.NetworkFilterConfigFactory) {
	for name, factory := range factories {
		if _, ok := networkFilterConfigFactoryRegistry[name]; ok {
			panic("network filter config factory already registered: " + name)
		}
		networkFilterConfigFactoryRegistry[name] = factory
	}
}

// ---------------------------------------------------------------------------
// Listener filter registry
// ---------------------------------------------------------------------------

var listenerFilterConfigFactoryRegistry = make(map[string]shared.ListenerFilterConfigFactory)

// GetListenerFilterConfigFactory returns the registered listener filter config factory for
// name, or nil if no factory is registered.
func GetListenerFilterConfigFactory(name string) shared.ListenerFilterConfigFactory {
	return listenerFilterConfigFactoryRegistry[name]
}

// RegisterListenerFilterConfigFactories registers listener filter config factories. MUST only be
// called from init() functions.
func RegisterListenerFilterConfigFactories(factories map[string]shared.ListenerFilterConfigFactory) {
	for name, factory := range factories {
		if _, ok := listenerFilterConfigFactoryRegistry[name]; ok {
			panic("listener filter config factory already registered: " + name)
		}
		listenerFilterConfigFactoryRegistry[name] = factory
	}
}

// ---------------------------------------------------------------------------
// UDP listener filter registry
// ---------------------------------------------------------------------------

var udpListenerFilterConfigFactoryRegistry = make(map[string]shared.UdpListenerFilterConfigFactory)

// GetUdpListenerFilterConfigFactory returns the registered UDP listener filter config factory
// for name, or nil if no factory is registered.
func GetUdpListenerFilterConfigFactory(name string) shared.UdpListenerFilterConfigFactory {
	return udpListenerFilterConfigFactoryRegistry[name]
}

// RegisterUdpListenerFilterConfigFactories registers UDP listener filter config factories. MUST
// only be called from init() functions.
func RegisterUdpListenerFilterConfigFactories(factories map[string]shared.UdpListenerFilterConfigFactory) {
	for name, factory := range factories {
		if _, ok := udpListenerFilterConfigFactoryRegistry[name]; ok {
			panic("UDP listener filter config factory already registered: " + name)
		}
		udpListenerFilterConfigFactoryRegistry[name] = factory
	}
}

// ---------------------------------------------------------------------------
// Access logger registry
// ---------------------------------------------------------------------------

var accessLoggerConfigFactoryRegistry = make(map[string]shared.AccessLoggerConfigFactory)

// GetAccessLoggerConfigFactory returns the registered access logger config factory for name,
// or nil if no factory is registered.
func GetAccessLoggerConfigFactory(name string) shared.AccessLoggerConfigFactory {
	return accessLoggerConfigFactoryRegistry[name]
}

// RegisterAccessLoggerConfigFactories registers access logger config factories. MUST only be
// called from init() functions.
func RegisterAccessLoggerConfigFactories(factories map[string]shared.AccessLoggerConfigFactory) {
	for name, factory := range factories {
		if _, ok := accessLoggerConfigFactoryRegistry[name]; ok {
			panic("access logger config factory already registered: " + name)
		}
		accessLoggerConfigFactoryRegistry[name] = factory
	}
}

// ---------------------------------------------------------------------------
// Matcher registry
// ---------------------------------------------------------------------------

var matcherConfigFactoryRegistry = make(map[string]shared.MatcherConfigFactory)

// GetMatcherConfigFactory returns the registered matcher config factory for name, or nil if no
// factory is registered.
func GetMatcherConfigFactory(name string) shared.MatcherConfigFactory {
	return matcherConfigFactoryRegistry[name]
}

// RegisterMatcherConfigFactories registers matcher config factories. MUST only be called from
// init() functions.
func RegisterMatcherConfigFactories(factories map[string]shared.MatcherConfigFactory) {
	for name, factory := range factories {
		if _, ok := matcherConfigFactoryRegistry[name]; ok {
			panic("matcher config factory already registered: " + name)
		}
		matcherConfigFactoryRegistry[name] = factory
	}
}

// ---------------------------------------------------------------------------
// Cert validator registry
// ---------------------------------------------------------------------------

var certValidatorConfigFactoryRegistry = make(map[string]shared.CertValidatorConfigFactory)

// GetCertValidatorConfigFactory returns the registered cert validator config factory for name,
// or nil if no factory is registered.
func GetCertValidatorConfigFactory(name string) shared.CertValidatorConfigFactory {
	return certValidatorConfigFactoryRegistry[name]
}

// RegisterCertValidatorConfigFactories registers cert validator config factories. MUST only be
// called from init() functions.
func RegisterCertValidatorConfigFactories(factories map[string]shared.CertValidatorConfigFactory) {
	for name, factory := range factories {
		if _, ok := certValidatorConfigFactoryRegistry[name]; ok {
			panic("cert validator config factory already registered: " + name)
		}
		certValidatorConfigFactoryRegistry[name] = factory
	}
}

// ---------------------------------------------------------------------------
// DNS resolver registry
// ---------------------------------------------------------------------------

var dnsResolverConfigFactoryRegistry = make(map[string]shared.DnsResolverConfigFactory)

// GetDnsResolverConfigFactory returns the registered DNS resolver config factory for name, or
// nil if no factory is registered.
func GetDnsResolverConfigFactory(name string) shared.DnsResolverConfigFactory {
	return dnsResolverConfigFactoryRegistry[name]
}

// RegisterDnsResolverConfigFactories registers DNS resolver config factories. MUST only be
// called from init() functions.
func RegisterDnsResolverConfigFactories(factories map[string]shared.DnsResolverConfigFactory) {
	for name, factory := range factories {
		if _, ok := dnsResolverConfigFactoryRegistry[name]; ok {
			panic("DNS resolver config factory already registered: " + name)
		}
		dnsResolverConfigFactoryRegistry[name] = factory
	}
}

// ---------------------------------------------------------------------------
// Upstream HTTP/TCP bridge registry
// ---------------------------------------------------------------------------

var upstreamHttpTcpBridgeConfigFactoryRegistry = make(map[string]shared.UpstreamHttpTcpBridgeConfigFactory)

// GetUpstreamHttpTcpBridgeConfigFactory returns the registered upstream HTTP/TCP bridge config
// factory for name, or nil if no factory is registered.
func GetUpstreamHttpTcpBridgeConfigFactory(name string) shared.UpstreamHttpTcpBridgeConfigFactory {
	return upstreamHttpTcpBridgeConfigFactoryRegistry[name]
}

// RegisterUpstreamHttpTcpBridgeConfigFactories registers upstream HTTP/TCP bridge config
// factories. MUST only be called from init() functions.
func RegisterUpstreamHttpTcpBridgeConfigFactories(factories map[string]shared.UpstreamHttpTcpBridgeConfigFactory) {
	for name, factory := range factories {
		if _, ok := upstreamHttpTcpBridgeConfigFactoryRegistry[name]; ok {
			panic("upstream HTTP/TCP bridge config factory already registered: " + name)
		}
		upstreamHttpTcpBridgeConfigFactoryRegistry[name] = factory
	}
}

// ---------------------------------------------------------------------------
// Tracer registry
// ---------------------------------------------------------------------------

var tracerConfigFactoryRegistry = make(map[string]shared.TracerConfigFactory)

// GetTracerConfigFactory returns the registered tracer config factory for name, or nil if no
// factory is registered.
func GetTracerConfigFactory(name string) shared.TracerConfigFactory {
	return tracerConfigFactoryRegistry[name]
}

// RegisterTracerConfigFactories registers tracer config factories. MUST only be called from
// init() functions.
func RegisterTracerConfigFactories(factories map[string]shared.TracerConfigFactory) {
	for name, factory := range factories {
		if _, ok := tracerConfigFactoryRegistry[name]; ok {
			panic("tracer config factory already registered: " + name)
		}
		tracerConfigFactoryRegistry[name] = factory
	}
}

// ---------------------------------------------------------------------------
// Transport socket factory registry
// ---------------------------------------------------------------------------

var transportSocketFactoryConfigFactoryRegistry = make(map[string]shared.TransportSocketFactoryConfigFactory)

// GetTransportSocketFactoryConfigFactory returns the registered transport socket factory config
// factory for name, or nil if no factory is registered.
func GetTransportSocketFactoryConfigFactory(name string) shared.TransportSocketFactoryConfigFactory {
	return transportSocketFactoryConfigFactoryRegistry[name]
}

// RegisterTransportSocketFactoryConfigFactories registers transport socket factory config
// factories. MUST only be called from init() functions.
func RegisterTransportSocketFactoryConfigFactories(factories map[string]shared.TransportSocketFactoryConfigFactory) {
	for name, factory := range factories {
		if _, ok := transportSocketFactoryConfigFactoryRegistry[name]; ok {
			panic("transport socket factory config factory already registered: " + name)
		}
		transportSocketFactoryConfigFactoryRegistry[name] = factory
	}
}

// ---------------------------------------------------------------------------
// Load balancer registry
// ---------------------------------------------------------------------------

var loadBalancerConfigFactoryRegistry = make(map[string]shared.LoadBalancerConfigFactory)

// GetLoadBalancerConfigFactory returns the registered load balancer config factory for name,
// or nil if no factory is registered.
func GetLoadBalancerConfigFactory(name string) shared.LoadBalancerConfigFactory {
	return loadBalancerConfigFactoryRegistry[name]
}

// RegisterLoadBalancerConfigFactories registers load balancer config factories. MUST only be
// called from init() functions.
func RegisterLoadBalancerConfigFactories(factories map[string]shared.LoadBalancerConfigFactory) {
	for name, factory := range factories {
		if _, ok := loadBalancerConfigFactoryRegistry[name]; ok {
			panic("load balancer config factory already registered: " + name)
		}
		loadBalancerConfigFactoryRegistry[name] = factory
	}
}

// ---------------------------------------------------------------------------
// Bootstrap extension registry
// ---------------------------------------------------------------------------

var bootstrapExtensionConfigFactoryRegistry = make(map[string]shared.BootstrapExtensionConfigFactory)

// GetBootstrapExtensionConfigFactory returns the registered bootstrap extension config
// factory for name, or nil if no factory is registered.
func GetBootstrapExtensionConfigFactory(name string) shared.BootstrapExtensionConfigFactory {
	return bootstrapExtensionConfigFactoryRegistry[name]
}

// RegisterBootstrapExtensionConfigFactories registers bootstrap extension config factories.
// MUST only be called from init() functions.
func RegisterBootstrapExtensionConfigFactories(factories map[string]shared.BootstrapExtensionConfigFactory) {
	for name, factory := range factories {
		if _, ok := bootstrapExtensionConfigFactoryRegistry[name]; ok {
			panic("bootstrap extension config factory already registered: " + name)
		}
		bootstrapExtensionConfigFactoryRegistry[name] = factory
	}
}

// ---------------------------------------------------------------------------
// Cluster registry
// ---------------------------------------------------------------------------

var clusterConfigFactoryRegistry = make(map[string]shared.ClusterConfigFactory)

// GetClusterConfigFactory returns the registered cluster config factory for name, or nil if no
// factory is registered.
func GetClusterConfigFactory(name string) shared.ClusterConfigFactory {
	return clusterConfigFactoryRegistry[name]
}

// RegisterClusterConfigFactories registers cluster config factories. MUST only be called from
// init() functions.
func RegisterClusterConfigFactories(factories map[string]shared.ClusterConfigFactory) {
	for name, factory := range factories {
		if _, ok := clusterConfigFactoryRegistry[name]; ok {
			panic("cluster config factory already registered: " + name)
		}
		clusterConfigFactoryRegistry[name] = factory
	}
}

// ---------------------------------------------------------------------------
// Program-wide utilities
// ---------------------------------------------------------------------------

// programHandle is the live shared.ProgramHandle wired up by the abi package via
// SetProgramHandle. It defaults to a no-op so module test code that doesn't link the abi
// package still gets sensible zero values. atomic.Pointer is used so SetProgramHandle from
// a test goroutine doesn't race with module code reading the handle.
var programHandle atomic.Pointer[shared.ProgramHandle]

func init() {
	var noop shared.ProgramHandle = &noopProgramHandle{}
	programHandle.Store(&noop)
}

func loadProgramHandle() shared.ProgramHandle { return *programHandle.Load() }

// SetProgramHandle replaces the program-wide handle used by GetConcurrency / IsValidationMode
// / Register*/Get* below. The abi package calls this once during init to wire in the live
// Envoy callbacks; tests may override it to inject a fake.
func SetProgramHandle(h shared.ProgramHandle) { programHandle.Store(&h) }

// GetConcurrency returns the number of worker threads the server is configured to use. MUST
// be called on the main thread (typically inside an on-program-init or on-server-initialized
// hook).
func GetConcurrency() uint32 { return loadProgramHandle().GetConcurrency() }

// IsValidationMode reports whether the server is running in config-validation mode
// (`--mode validate`). Modules can use this to skip expensive operations during validation.
// MUST be called on the main thread.
func IsValidationMode() bool { return loadProgramHandle().IsValidationMode() }

// RegisterFunction registers a function pointer in Envoy's process-wide function registry —
// used for zero-copy cross-module calls. See shared.ProgramHandle.RegisterFunction for full
// semantics. Thread-safe.
func RegisterFunction(key string, fnPtr unsafe.Pointer) bool {
	return loadProgramHandle().RegisterFunction(key, fnPtr)
}

// GetFunction retrieves a previously registered function pointer by key. See
// shared.ProgramHandle.GetFunction. Thread-safe.
func GetFunction(key string) (unsafe.Pointer, bool) {
	return loadProgramHandle().GetFunction(key)
}

// RegisterSharedData registers an opaque data pointer in Envoy's process-wide shared-data
// registry. See shared.ProgramHandle.RegisterSharedData. Thread-safe.
func RegisterSharedData(key string, dataPtr unsafe.Pointer) bool {
	return loadProgramHandle().RegisterSharedData(key, dataPtr)
}

// GetSharedData retrieves a previously registered data pointer by key. See
// shared.ProgramHandle.GetSharedData. Thread-safe.
func GetSharedData(key string) (unsafe.Pointer, bool) {
	return loadProgramHandle().GetSharedData(key)
}

// Log writes a formatted message through Envoy's logging subsystem. This is the
// process-global logging entry point — it works for any extension type, including
// surfaces (bootstrap, cluster, tracer, etc.) whose handle does not expose its own Log
// method. Thread-safe.
func Log(level shared.LogLevel, format string, args ...any) {
	loadProgramHandle().Log(level, format, args...)
}

// noopProgramHandle is the default until abi.init() replaces it with the live one.
type noopProgramHandle struct{}

func (noopProgramHandle) GetConcurrency() uint32                             { return 0 }
func (noopProgramHandle) IsValidationMode() bool                             { return false }
func (noopProgramHandle) RegisterFunction(_ string, _ unsafe.Pointer) bool   { return false }
func (noopProgramHandle) GetFunction(_ string) (unsafe.Pointer, bool)        { return nil, false }
func (noopProgramHandle) RegisterSharedData(_ string, _ unsafe.Pointer) bool { return false }
func (noopProgramHandle) GetSharedData(_ string) (unsafe.Pointer, bool)      { return nil, false }
func (noopProgramHandle) Log(_ shared.LogLevel, _ string, _ ...any)          {}
