@_implementationOnly import EnvoyEngine
import Foundation

#if ENVOY_MOBILE_XDS
/// Builder for generating the xDS configuration for the Envoy Mobile engine.
/// xDS is a protocol for dynamic configuration of Envoy instances, more information can be found in
/// https://www.envoyproxy.io/docs/envoy/latest/api-docs/xds_protocol.
///
/// This class is typically used as input to the EngineBuilder's setXds() method.
@objcMembers
open class XdsBuilder: NSObject {
  public static let defaultXdsTimeoutInSeconds: UInt32 = 5

  let xdsServerAddress: String
  let xdsServerPort: UInt32
  var xdsGrpcInitialMetadata: [String: String] = [:]
  var sslRootCerts: String?
  var rtdsResourceName: String?
  var rtdsTimeoutInSeconds: UInt32 = 0
  var enableCds: Bool = false
  var cdsResourcesLocator: String?
  var cdsTimeoutInSeconds: UInt32 = 0

  /// Initialize a new builder for xDS configuration.
  ///
  /// - parameter xdsServerAddress: The host name or IP address of the xDS management server.
  /// - parameter xdsServerPort:    The port on which the server listens for client connections.
  public init(xdsServerAddress: String, xdsServerPort: UInt32) {
    self.xdsServerAddress = xdsServerAddress
    self.xdsServerPort = xdsServerPort
  }

  /// Adds a header to the initial HTTP metadata headers sent on the gRPC stream.
  ///
  /// A common use for the initial metadata headers is for authentication to the xDS management
  /// server.
  ///
  /// For example, if using API keys to authenticate to Traffic Director on GCP (see
  /// https://cloud.google.com/docs/authentication/api-keys for details), invoke:
  ///   builder.addInitialStreamHeader("x-goog-api-key", apiKeyToken)
  ///          .addInitialStreamHeader("X-Android-Package", appPackageName)
  ///          .addInitialStreamHeader("X-Android-Cert", sha1KeyFingerprint);
  ///
  /// - parameter header: The HTTP header to add on the gRPC stream's initial metadata.
  /// - parameter value:  The HTTP header value to add on the gRPC stream's initial metadata.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addInitialStreamHeader(
    header: String,
    value: String) -> Self {
    self.xdsGrpcInitialMetadata[header] = value
    return self
  }

  /// Sets the PEM-encoded server root certificates used to negotiate the TLS handshake for the gRPC
  /// connection. If no root certs are specified, the operating system defaults are used.
  ///
  /// - parameter rootCerts: The PEM-encoded server root certificates.
  ///
  /// - returns: This builder.
  @discardableResult
  public func setSslRootCerts(rootCerts: String) -> Self {
    self.sslRootCerts = rootCerts
    return self
  }

  /// Adds Runtime Discovery Service (RTDS) to the Runtime layers of the Bootstrap configuration,
  /// to retrieve dynamic runtime configuration via the xDS management server.
  ///
  /// - parameter resourceName:     The runtime config resource to subscribe to.
  /// - parameter timeoutInSeconds: <optional> specifies the `initial_fetch_timeout` field on the
  ///                               api.v3.core.ConfigSource. Unlike the ConfigSource default of
  ///                               15s, we set a default fetch timeout value of 5s, to prevent
  ///                               mobile app initialization from stalling. The default parameter
  ///                               value may change through the course of experimentation and no
  ///                               assumptions should be made of its exact value.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addRuntimeDiscoveryService(
    resourceName: String,
    timeoutInSeconds: UInt32 = XdsBuilder.defaultXdsTimeoutInSeconds) -> Self {
    self.rtdsResourceName = resourceName
    self.rtdsTimeoutInSeconds = timeoutOrXdsDefault(timeoutInSeconds)
    return self
  }

  /// Adds the Cluster Discovery Service (CDS) configuration for retrieving dynamic cluster
  /// resources via the xDS management server.
  ///
  /// - parameter cdsResourcesLocator: <optional> the xdstp:// URI for subscribing to the cluster
  ///                                  resources. If not using xdstp, then `cds_resources_locator`
  ///                                  should be set to the empty string.
  /// - parameter timeoutInSeconds:    <optional> specifies the `initial_fetch_timeout` field on the
  ///                                  api.v3.core.ConfigSource. Unlike the ConfigSource default of
  ///                                  15s, we set a default fetch timeout value of 5s, to prevent
  ///                                  mobile app initialization from stalling. The default
  ///                                  parameter value may change through the course of
  ///                                  experimentation and no assumptions should be made of its
  ///                                  exact value.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addClusterDiscoveryService(
    cdsResourcesLocator: String? = nil,
    timeoutInSeconds: UInt32 = XdsBuilder.defaultXdsTimeoutInSeconds) -> Self {
    self.enableCds = true
    self.cdsResourcesLocator = cdsResourcesLocator
    self.cdsTimeoutInSeconds = timeoutOrXdsDefault(timeoutInSeconds)
    return self
  }

  private func timeoutOrXdsDefault(_ timeout: UInt32) -> UInt32 {
    return timeout > 0 ? timeout : XdsBuilder.defaultXdsTimeoutInSeconds
  }
}
#endif

/// Builder used for creating and running a new Engine instance.
@objcMembers
open class EngineBuilder: NSObject {
  // swiftlint:disable:previous type_body_length
  private var engineType: EnvoyEngine.Type = EnvoyEngineImpl.self
  private var logLevel: LogLevel = .info
  private var connectTimeoutSeconds: UInt32 = 30
  private var dnsFailureRefreshSecondsBase: UInt32 = 2
  private var dnsFailureRefreshSecondsMax: UInt32 = 10
  private var dnsQueryTimeoutSeconds: UInt32 = 5
  private var dnsMinRefreshSeconds: UInt32 = 60
  private var dnsPreresolveHostnames: [String] = []
  private var dnsRefreshSeconds: UInt32 = 60
  private var enableDNSCache: Bool = false
  private var dnsCacheSaveIntervalSeconds: UInt32 = 1
  private var enableGzipDecompression: Bool = true
  private var enableBrotliDecompression: Bool = false
#if ENVOY_ENABLE_QUIC
  private var enableHttp3: Bool = true
#else
  private var enableHttp3: Bool = false
#endif
  private var quicHints: [String: Int] = [:]
  private var quicCanonicalSuffixes: [String] = []
  private var enableInterfaceBinding: Bool = false
  private var enforceTrustChainVerification: Bool = true
  private var enablePlatformCertificateValidation: Bool = false
  private var upstreamTlsSni: String?
  private var respectSystemProxySettings: Bool = false
  private var enableDrainPostDnsRefresh: Bool = false
  private var forceIPv6: Bool = false
  private var h2ConnectionKeepaliveIdleIntervalMilliseconds: UInt32 = 1
  private var h2ConnectionKeepaliveTimeoutSeconds: UInt32 = 10
  private var maxConnectionsPerHost: UInt32 = 7
  private var streamIdleTimeoutSeconds: UInt32 = 15
  private var perTryIdleTimeoutSeconds: UInt32 = 15
  private var appVersion: String = "unspecified"
  private var appId: String = "unspecified"
  private var onEngineRunning: (() -> Void)?
  private var logger: ((LogLevel, String) -> Void)?
  private var eventTracker: (([String: String]) -> Void)?
  private(set) var monitoringMode: NetworkMonitoringMode = .pathMonitor
  private var nativeFilterChain: [EnvoyNativeFilterConfig] = []
  private var platformFilterChain: [EnvoyHTTPFilterFactory] = []
  private var stringAccessors: [String: EnvoyStringAccessor] = [:]
  private var keyValueStores: [String: EnvoyKeyValueStore] = [:]
  private var runtimeGuards: [String: Bool] = [:]
  private var nodeID: String?
  private var nodeRegion: String?
  private var nodeZone: String?
  private var nodeSubZone: String?
#if ENVOY_MOBILE_XDS
  private var xdsBuilder: XdsBuilder?
#endif

  // MARK: - Public

  /// Initialize a new builder.
  public override init() {}

  /// Set a log level to use with Envoy.
  ///
  /// - parameter logLevel: The log level to use with Envoy.
  ///
  /// - returns: This builder.
  @discardableResult
  public func setLogLevel(_ logLevel: LogLevel) -> Self {
    self.logLevel = logLevel
    return self
  }

  /// Add a timeout for new network connections to hosts in the cluster.
  ///
  /// - parameter connectTimeoutSeconds: Timeout for new network
  ///                                    connections to hosts in the cluster.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addConnectTimeoutSeconds(_ connectTimeoutSeconds: UInt32) -> Self {
    self.connectTimeoutSeconds = connectTimeoutSeconds
    return self
  }

  /// Add a rate at which to refresh DNS in case of DNS failure.
  ///
  /// - parameter base: Base rate in seconds.
  /// - parameter max:  Max rate in seconds.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addDNSFailureRefreshSeconds(base: UInt32, max: UInt32) -> Self {
    self.dnsFailureRefreshSecondsBase = base
    self.dnsFailureRefreshSecondsMax = max
    return self
  }

  /// Add a rate at which to timeout DNS queries.
  ///
  /// - parameter dnsQueryTimeoutSeconds: Rate in seconds to timeout DNS queries.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addDNSQueryTimeoutSeconds(_ dnsQueryTimeoutSeconds: UInt32) -> Self {
    self.dnsQueryTimeoutSeconds = dnsQueryTimeoutSeconds
    return self
  }

  /// Add the minimum rate at which to refresh DNS. Once DNS has been resolved for a host, DNS TTL
  /// will be respected, subject to this minimum. Defaults to 60 seconds.
  ///
  /// - parameter dnsMinRefreshSeconds: Minimum rate in seconds at which to refresh DNS.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addDNSMinRefreshSeconds(_ dnsMinRefreshSeconds: UInt32) -> Self {
    self.dnsMinRefreshSeconds = dnsMinRefreshSeconds
    return self
  }

  /// Add a list of hostnames to preresolve on Engine startup.
  ///
  /// - parameter dnsPreresolveHostnames: the hostnames to resolve.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addDNSPreresolveHostnames(dnsPreresolveHostnames: [String]) -> Self {
    self.dnsPreresolveHostnames = dnsPreresolveHostnames
    return self
  }

  /// Add a default rate at which to refresh DNS.
  ///
  /// - parameter dnsRefreshSeconds: Default rate in seconds at which to refresh DNS.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addDNSRefreshSeconds(_ dnsRefreshSeconds: UInt32) -> Self {
    self.dnsRefreshSeconds = dnsRefreshSeconds
    return self
  }

  /// Specify whether to enable DNS cache.
  ///
  /// Note that DNS cache requires an addition of a key value store named
  /// 'reserved.platform_store'.
  ///
  /// - parameter enableDNSCache: whether to enable DNS cache. Disabled by default.
  /// - parameter saveInterval:   the interval at which to save results to the configured
  ///                             key value store.
  ///
  /// - returns: This builder.
  @discardableResult
  public func enableDNSCache(_ enableDNSCache: Bool, saveInterval: UInt32 = 1) -> Self {
    self.enableDNSCache = enableDNSCache
    self.dnsCacheSaveIntervalSeconds = saveInterval
    return self
  }

  /// Specify whether to do gzip response decompression or not.  Defaults to true.
  ///
  /// - parameter enableGzipDecompression: whether or not to gunzip responses.
  ///
  /// - returns: This builder.
  @discardableResult
  public func enableGzipDecompression(_ enableGzipDecompression: Bool) -> Self {
    self.enableGzipDecompression = enableGzipDecompression
    return self
  }

  /// Specify whether to do brotli response decompression or not.  Defaults to false.
  ///
  /// - parameter enableBrotliDecompression: whether or not to brotli decompress responses.
  ///
  /// - returns: This builder.
  @discardableResult
  public func enableBrotliDecompression(_ enableBrotliDecompression: Bool) -> Self {
    self.enableBrotliDecompression = enableBrotliDecompression
    return self
  }

#if ENVOY_ENABLE_QUIC
  /// Specify whether to enable support for HTTP/3 or not.  Defaults to true.
  ///
  /// - parameter enableHttp3: whether or not to enable HTTP/3.
  ///
  /// - returns: This builder.
  @discardableResult
  public func enableHttp3(_ enableHttp3: Bool) -> Self {
    self.enableHttp3 = enableHttp3
    return self
  }

  /// Add a host port pair that's known to support QUIC.
  ///
  /// - parameter host: the string representation of the host name
  /// - parameter port: the host's port number
  ///
  /// - returns: This builder.
  @discardableResult
  public func addQuicHint(_ host: String, _ port: Int) -> Self {
    self.quicHints[host] = port
    return self
  }

  /// Add a host suffix that's known to support QUIC.
  ///
  /// - parameter suffix: the string representation of the host suffix
  ///
  /// - returns: This builder.
  @discardableResult
  public func addQuicCanonicalSuffix(_ suffix: String) -> Self {
    self.quicCanonicalSuffixes.append(suffix)
    return self
  }
#endif

  /// Specify whether sockets may attempt to bind to a specific interface, based on network
  /// conditions.
  ///
  /// - parameter enableInterfaceBinding: whether to allow interface binding.
  ///
  /// - returns: This builder.
  @discardableResult
  public func enableInterfaceBinding(_ enableInterfaceBinding: Bool) -> Self {
    self.enableInterfaceBinding = enableInterfaceBinding
    return self
  }

  ///
  /// Specify whether system proxy settings should be respected. If yes, Envoy Mobile will
  /// use iOS APIs to query iOS Proxy settings configured on a device and will
  /// respect these settings when establishing connections with remote services.
  ///
  /// The method is introduced for experimentation purposes and as a safety guard against
  /// critical issues in the implementation of the proxying feature. It's intended to be removed
  /// after it's confirmed that proxies on iOS work as expected.
  ///
  /// - parameter respectSystemProxySettings: whether to use the system's proxy settings for
  ///                                         outbound connections.
  ///
  /// - returns: This builder.
  @discardableResult
  public func respectSystemProxySettings(_ respectSystemProxySettings: Bool) -> Self {
    self.respectSystemProxySettings = respectSystemProxySettings
    return self
  }

  /// Specify whether to drain connections after the resolution of a soft DNS refresh.
  /// A refresh may be triggered directly via the Engine API, or as a result of a network
  /// status update provided by the OS. Draining connections does not interrupt existing
  /// connections or requests, but will establish new connections for any further requests.
  ///
  /// - parameter enableDrainPostDnsRefresh: whether to drain connections after soft DNS refresh.
  ///
  /// - returns: This builder.
  @discardableResult
  public func enableDrainPostDnsRefresh(_ enableDrainPostDnsRefresh: Bool) -> Self {
    self.enableDrainPostDnsRefresh = enableDrainPostDnsRefresh
    return self
  }

  /// Specify whether to enforce TLS trust chain verification for secure sockets.
  ///
  /// - parameter enforceTrustChainVerification: whether to enforce trust chain verification.
  ///
  /// - returns: This builder.
  @discardableResult
  public func enforceTrustChainVerification(_ enforceTrustChainVerification: Bool) -> Self {
    self.enforceTrustChainVerification = enforceTrustChainVerification
    return self
  }

  /// Specify whether to use the platform certificate verifier.
  ///
  /// - parameter enablePlatformCertificateValidation: whether to use the platform verifier.
  ///
  /// - returns: This builder.
  @discardableResult
  public func enablePlatformCertificateValidation(
    _ enablePlatformCertificateValidation: Bool) -> Self {
    self.enablePlatformCertificateValidation = enablePlatformCertificateValidation
    return self
  }

  /// Sets the SNI override on the upstream TLS socket context.
  ///
  /// - parameter sni: The SNI.
  ///
  /// - returns: This builder.
  @discardableResult
  public func setUpstreamTlsSni(_ sni: String) -> Self {
    self.upstreamTlsSni = sni
    return self
  }

  /// Specify whether to remap IPv4 addresses to the IPv6 space and always force connections
  /// to use IPv6. Note this is an experimental option and should be enabled with caution.
  ///
  /// - parameter forceIPv6: whether to force connections to use IPv6.
  ///
  /// - returns: This builder.
  @discardableResult
  public func forceIPv6(_ forceIPv6: Bool) -> Self {
    self.forceIPv6 = forceIPv6
    return self
  }

  /// Add a rate at which to ping h2 connections on new stream creation if the connection has
  /// sat idle. Defaults to 1 millisecond which effectively enables h2 ping functionality
  /// and results in a connection ping on every new stream creation. Set it to
  /// 100000000 milliseconds to effectively disable the ping.
  ///
  /// - parameter h2ConnectionKeepaliveIdleIntervalMilliseconds: Rate in milliseconds.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addH2ConnectionKeepaliveIdleIntervalMilliseconds(
    _ h2ConnectionKeepaliveIdleIntervalMilliseconds: UInt32) -> Self {
    self.h2ConnectionKeepaliveIdleIntervalMilliseconds =
      h2ConnectionKeepaliveIdleIntervalMilliseconds
    return self
  }

  /// Add a rate at which to timeout h2 pings.
  ///
  /// - parameter h2ConnectionKeepaliveTimeoutSeconds: Rate in seconds to timeout h2 pings.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addH2ConnectionKeepaliveTimeoutSeconds(
    _ h2ConnectionKeepaliveTimeoutSeconds: UInt32) -> Self {
    self.h2ConnectionKeepaliveTimeoutSeconds = h2ConnectionKeepaliveTimeoutSeconds
    return self
  }

  /// Set the maximum number of connections to open to a single host. Default is 7.
  ///
  /// - parameter maxConnectionsPerHost: the maximum number of connections per host.
  ///
  /// - returns: This builder.
  @discardableResult
  public func setMaxConnectionsPerHost(_ maxConnectionsPerHost: UInt32) -> Self {
    self.maxConnectionsPerHost = maxConnectionsPerHost
    return self
  }

  /// Add a custom idle timeout for HTTP streams. Defaults to 15 seconds.
  ///
  /// - parameter streamIdleTimeoutSeconds: Idle timeout for HTTP streams.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addStreamIdleTimeoutSeconds(_ streamIdleTimeoutSeconds: UInt32) -> Self {
    self.streamIdleTimeoutSeconds = streamIdleTimeoutSeconds
    return self
  }

  /// Add a custom per try idle timeout for HTTP streams. Defaults to 15 seconds.
  ///
  /// - parameter perTryIdleTimeoutSeconds: Idle timeout for HTTP streams.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addPerTryIdleTimeoutSeconds(_ perTryIdleTimeoutSeconds: UInt32) -> Self {
    self.perTryIdleTimeoutSeconds = perTryIdleTimeoutSeconds
    return self
  }

  /// Add an HTTP platform filter factory used to construct filters for streams sent by this client.
  ///
  /// - parameter name:    Custom name to use for this filter factory. Useful for having
  ///                      more meaningful trace logs, but not required. Should be unique
  ///                      per factory registered.
  /// - parameter factory: Closure returning an instantiated filter. Called once per stream.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addPlatformFilter(name: String,
                                factory: @escaping () -> Filter) -> Self
  {
    self.platformFilterChain.append(EnvoyHTTPFilterFactory(filterName: name, factory: factory))
    return self
  }

  /// Add an HTTP platform filter factory used to construct filters for streams sent by this client.
  ///
  /// - parameter factory: Closure returning an instantiated filter. Called once per stream.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addPlatformFilter(_ factory: @escaping () -> Filter) -> Self
  {
    self.platformFilterChain.append(
      EnvoyHTTPFilterFactory(filterName: UUID().uuidString, factory: factory)
    )
    return self
  }

  /// Add an HTTP native filter factory used to construct filters for streams sent by this client.
  ///
  /// - parameter name:        Custom name to use for this filter factory. Useful for having
  ///                          more meaningful trace logs, but not required. Should be unique
  ///                          per factory registered.
  /// - parameter typedConfig: Config string for the filter.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addNativeFilter(name: String = UUID().uuidString, typedConfig: String) -> Self {
    self.nativeFilterChain.append(EnvoyNativeFilterConfig(name: name, typedConfig: typedConfig))
    return self
  }

  /// Add a string accessor to this Envoy Client.
  ///
  /// - parameter name:     the name of the accessor.
  /// - parameter accessor: lambda to access a string from the platform layer.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addStringAccessor(name: String, accessor: @escaping () -> String) -> Self {
    self.stringAccessors[name] = EnvoyStringAccessor(block: accessor)
    return self
  }

  /// Register a key-value store implementation for internal use.
  ///
  /// - parameter name:          the name of the KV store.
  /// - parameter keyValueStore: the KV store implementation.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addKeyValueStore(name: String, keyValueStore: KeyValueStore) -> Self {
    self.keyValueStores[name] = KeyValueStoreImpl(implementation: keyValueStore)
    return self
  }

  // Adds a runtime guard for the `envoy.reloadable_features.<guard>`.
  // For example if the runtime guard is `envoy.reloadable_features.use_foo`, the guard name is
  // `use_foo`.
  ///
  /// - parameter name:  the name of the runtime guard, e.g. test_feature_false.
  /// - parameter value: the value for the runtime guard.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addRuntimeGuard(_ name: String, _ value: Bool) -> Self {
    self.runtimeGuards[name] = value
    return self
  }

  /// Set a closure to be called when the engine finishes its async startup and begins running.
  ///
  /// - parameter closure: The closure to be called.
  ///
  /// - returns: This builder.
  @discardableResult
  public func setOnEngineRunning(closure: @escaping () -> Void) -> Self {
    self.onEngineRunning = closure
    return self
  }

  /// Set a closure to be called when the engine's logger logs.
  ///
  /// - parameter closure: The closure to be called.
  ///
  /// - returns: This builder.
  @discardableResult
  public func setLogger(closure: @escaping (LogLevel, String) -> Void) -> Self {
    self.logger = closure
    return self
  }

  /// Set a closure to be called when the engine emits an event.
  ///
  /// - parameter closure: The closure to be called.
  ///
  /// - returns: This builder.
  @discardableResult
  public func setEventTracker(closure: @escaping ([String: String]) -> Void) -> Self {
    self.eventTracker = closure
    return self
  }

  /// Configure how the engine observes network reachability state changes.
  /// Defaults to `.pathMonitor`.
  ///
  /// - parameter mode: The mode to use.
  ///
  /// - returns: This builder.
  @discardableResult
  public func setNetworkMonitoringMode(_ mode: NetworkMonitoringMode) -> Self {
    self.monitoringMode = mode
    return self
  }

  /// Add the App Version of the App using this Envoy Client.
  ///
  /// - parameter appVersion: The version.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addAppVersion(_ appVersion: String) -> Self {
    self.appVersion = appVersion
    return self
  }

  /// Add the App ID of the App using this Envoy Client.
  ///
  /// - parameter appId: The ID.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addAppId(_ appId: String) -> Self {
    self.appId = appId
    return self
  }

  /// Sets the node.id field in the Bootstrap configuration.
  ///
  /// - parameter nodeID: The node ID.
  ///
  /// - returns: This builder.
  @discardableResult
  public func setNodeID(_ nodeID: String) -> Self {
    self.nodeID = nodeID
    return self
  }

  /// Sets the node locality in the Bootstrap configuration.
  ///
  /// - parameter region:  The region.
  /// - parameter zone:    The zone.
  /// - parameter subZone: The sub-zone.
  ///
  /// - returns: This builder.
  @discardableResult
  public func setNodeLocality(
    region: String,
    zone: String,
    subZone: String
  ) -> Self {
    self.nodeRegion = region
    self.nodeZone = zone
    self.nodeSubZone = subZone
    return self
  }

#if ENVOY_MOBILE_XDS
  /// Sets the xDS configuration for the Envoy Mobile engine.
  ///
  /// - parameter xdsBuilder: The XdsBuilder instance which specifies the xDS config options.
  ///                         The EngineBuilder takes ownership over the xds_builder.
  ///
  /// - returns: This builder.
  @discardableResult
  public func setXds(_ xdsBuilder: XdsBuilder) -> Self {
    self.xdsBuilder = xdsBuilder
    return self
  }
#endif

  /// Builds and runs a new `Engine` instance with the provided configuration.
  ///
  /// - note: Must be strongly retained in order for network requests to be performed correctly.
  ///
  /// - returns: The built `Engine`.
  public func build() -> Engine {
    let engine = self.engineType.init(runningCallback: self.onEngineRunning,
                                      logger: { level, message in
                                        if let log = self.logger {
                                          if let lvl = LogLevel(rawValue: level) {
                                            log(lvl, message)
                                          }
                                        }
                                      },
                                      eventTracker: self.eventTracker,
                                      networkMonitoringMode: Int32(self.monitoringMode.rawValue))
    let config = self.makeConfig()

    return EngineImpl(config: config, logLevel: self.logLevel, engine: engine)
  }

  // MARK: - Internal

  /// Add a specific implementation of `EnvoyEngine` to use for starting Envoy.
  /// A new instance of this engine will be created when `build()` is called.
  /// Used for testing, as initializing with `EnvoyEngine.Type` results in a
  /// segfault: https://github.com/envoyproxy/envoy-mobile/issues/334
  ///
  /// - parameter engineType: The specific implementation of `EnvoyEngine` to use for starting
  ///                         Envoy.
  ///
  /// - returns: This builder.
  @discardableResult
  func addEngineType(_ engineType: EnvoyEngine.Type) -> Self {
    self.engineType = engineType
    return self
  }

  func makeConfig() -> EnvoyConfiguration {
    var xdsServerAddress: String?
    var xdsServerPort: UInt32 = 0
    var xdsGrpcInitialMetadata: [String: String] = [:]
    var xdsSslRootCerts: String?
    var rtdsResourceName: String?
    var rtdsTimeoutSeconds: UInt32 = 0
    var enableCds: Bool = false
    var cdsResourcesLocator: String?
    var cdsTimeoutSeconds: UInt32 = 0

#if ENVOY_MOBILE_XDS
    xdsServerAddress = self.xdsBuilder?.xdsServerAddress
    xdsServerPort = self.xdsBuilder?.xdsServerPort ?? 0
    xdsGrpcInitialMetadata = self.xdsBuilder?.xdsGrpcInitialMetadata ?? [:]
    xdsSslRootCerts = self.xdsBuilder?.sslRootCerts
    rtdsResourceName = self.xdsBuilder?.rtdsResourceName
    rtdsTimeoutSeconds = self.xdsBuilder?.rtdsTimeoutInSeconds ?? 0
    enableCds = self.xdsBuilder?.enableCds ?? false
    cdsResourcesLocator = self.xdsBuilder?.cdsResourcesLocator
    cdsTimeoutSeconds = self.xdsBuilder?.cdsTimeoutInSeconds ?? 0
#endif

    return EnvoyConfiguration(
      connectTimeoutSeconds: self.connectTimeoutSeconds,
      dnsRefreshSeconds: self.dnsRefreshSeconds,
      dnsFailureRefreshSecondsBase: self.dnsFailureRefreshSecondsBase,
      dnsFailureRefreshSecondsMax: self.dnsFailureRefreshSecondsMax,
      dnsQueryTimeoutSeconds: self.dnsQueryTimeoutSeconds,
      dnsMinRefreshSeconds: self.dnsMinRefreshSeconds,
      dnsPreresolveHostnames: self.dnsPreresolveHostnames,
      enableDNSCache: self.enableDNSCache,
      dnsCacheSaveIntervalSeconds: self.dnsCacheSaveIntervalSeconds,
      enableHttp3: self.enableHttp3,
      quicHints: self.quicHints.mapValues { NSNumber(value: $0) },
      quicCanonicalSuffixes: self.quicCanonicalSuffixes,
      enableGzipDecompression: self.enableGzipDecompression,
      enableBrotliDecompression: self.enableBrotliDecompression,
      enableInterfaceBinding: self.enableInterfaceBinding,
      enableDrainPostDnsRefresh: self.enableDrainPostDnsRefresh,
      enforceTrustChainVerification: self.enforceTrustChainVerification,
      forceIPv6: self.forceIPv6,
      enablePlatformCertificateValidation: self.enablePlatformCertificateValidation,
      upstreamTlsSni: self.upstreamTlsSni,
      respectSystemProxySettings: self.respectSystemProxySettings,
      h2ConnectionKeepaliveIdleIntervalMilliseconds:
        self.h2ConnectionKeepaliveIdleIntervalMilliseconds,
      h2ConnectionKeepaliveTimeoutSeconds: self.h2ConnectionKeepaliveTimeoutSeconds,
      maxConnectionsPerHost: self.maxConnectionsPerHost,
      streamIdleTimeoutSeconds: self.streamIdleTimeoutSeconds,
      perTryIdleTimeoutSeconds: self.perTryIdleTimeoutSeconds,
      appVersion: self.appVersion,
      appId: self.appId,
      runtimeGuards: self.runtimeGuards.mapValues({ "\($0)" }),
      nativeFilterChain: self.nativeFilterChain,
      platformFilterChain: self.platformFilterChain,
      stringAccessors: self.stringAccessors,
      keyValueStores: self.keyValueStores,
      nodeId: self.nodeID,
      nodeRegion: self.nodeRegion,
      nodeZone: self.nodeZone,
      nodeSubZone: self.nodeSubZone,
      xdsServerAddress: xdsServerAddress,
      xdsServerPort: xdsServerPort,
      xdsGrpcInitialMetadata: xdsGrpcInitialMetadata,
      xdsSslRootCerts: xdsSslRootCerts,
      rtdsResourceName: rtdsResourceName,
      rtdsTimeoutSeconds: rtdsTimeoutSeconds,
      enableCds: enableCds,
      cdsResourcesLocator: cdsResourcesLocator,
      cdsTimeoutSeconds: cdsTimeoutSeconds
    )
  }

  func bootstrapDebugDescription() -> String {
    let objcDescription = self.makeConfig().bootstrapDebugDescription()
    return objcDescription
  }
}
