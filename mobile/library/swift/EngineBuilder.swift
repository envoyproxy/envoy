@_implementationOnly import EnvoyEngine
import Foundation

/// Builder used for creating and running a new Engine instance.
@objcMembers
open class EngineBuilder: NSObject {
  private let base: BaseConfiguration
  private var engineType: EnvoyEngine.Type = EnvoyEngineImpl.self
  private var logLevel: LogLevel = .info

  private enum BaseConfiguration {
    case standard
    case custom(String)
  }

  private var adminInterfaceEnabled = false
  private var grpcStatsDomain: String?
  private var connectTimeoutSeconds: UInt32 = 30
  private var dnsRefreshSeconds: UInt32 = 60
  private var dnsFailureRefreshSecondsBase: UInt32 = 2
  private var dnsFailureRefreshSecondsMax: UInt32 = 10
  private var dnsQueryTimeoutSeconds: UInt32 = 25
  private var dnsPreresolveHostnames: String = "[]"
  private var enableHappyEyeballs: Bool = false
  private var enableInterfaceBinding: Bool = false
  private var h2ConnectionKeepaliveIdleIntervalMilliseconds: UInt32 = 100000000
  private var h2ConnectionKeepaliveTimeoutSeconds: UInt32 = 10
  private var statsFlushSeconds: UInt32 = 60
  private var streamIdleTimeoutSeconds: UInt32 = 15
  private var perTryIdleTimeoutSeconds: UInt32 = 15
  private var appVersion: String = "unspecified"
  private var appId: String = "unspecified"
  private var virtualClusters: String = "[]"
  private var onEngineRunning: (() -> Void)?
  private var logger: ((String) -> Void)?
  private var eventTracker: (([String: String]) -> Void)?
  private(set) var enableNetworkPathMonitor = false
  private var nativeFilterChain: [EnvoyNativeFilterConfig] = []
  private var platformFilterChain: [EnvoyHTTPFilterFactory] = []
  private var stringAccessors: [String: EnvoyStringAccessor] = [:]
  private var directResponses: [DirectResponse] = []

  // MARK: - Public

  /// Initialize a new builder with standard HTTP library configuration.
  public override init() {
    self.base = .standard
  }

  /// Initialize a new builder with a custom full YAML configuration.
  /// Setting other attributes in this builder will have no effect.
  ///
  /// - parameter yaml: Contents of a YAML file to use for configuration.
  public init(yaml: String) {
    self.base = .custom(yaml)
  }

  /// Add a stats domain for Envoy to flush stats to.
  /// Passing nil disables stats emission.
  ///
  /// - parameter grpcStatsDomain: The domain to use for stats.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addGrpcStatsDomain(_ grpcStatsDomain: String?) -> Self {
    self.grpcStatsDomain = grpcStatsDomain
    return self
  }

  /// Add a log level to use with Envoy.
  ///
  /// - parameter logLevel: The log level to use with Envoy.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addLogLevel(_ logLevel: LogLevel) -> Self {
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

  /// Add a rate at which to refresh DNS.
  ///
  /// - parameter dnsRefreshSeconds: Rate in seconds to refresh DNS.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addDNSRefreshSeconds(_ dnsRefreshSeconds: UInt32) -> Self {
    self.dnsRefreshSeconds = dnsRefreshSeconds
    return self
  }

  /// Add a rate at which to refresh DNS in case of DNS failure.
  ///
  /// - parameter base: Base rate in seconds.
  /// - parameter max: Max rate in seconds.
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

  /// Add a list of hostnames to preresolve on Engine startup.
  ///
  /// - parameter dnsPreresolveHostnames: the hostnames to resolve.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addDNSPreresolveHostnames(dnsPreresolveHostnames: String) -> Self {
    self.dnsPreresolveHostnames = dnsPreresolveHostnames
    return self
  }

  /// Specify whether to use Happy Eyeballs when multiple IP stacks may be supported.
  ///
  /// - parameter enableHappyEyeballs: whether to enable RFC 6555 handling for IPv4/IPv6.
  ///
  /// - returns: This builder.
  @discardableResult
  public func enableHappyEyeballs(_ enableHappyEyeballs: Bool) -> Self {
    self.enableHappyEyeballs = enableHappyEyeballs
    return self
  }

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

  /// Add a rate at which to ping h2 connections on new stream creation if the connection has
  /// sat idle.
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

  /// Add an interval at which to flush Envoy stats.
  ///
  /// - parameter statsFlushSeconds: Interval at which to flush Envoy stats.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addStatsFlushSeconds(_ statsFlushSeconds: UInt32) -> Self {
    self.statsFlushSeconds = statsFlushSeconds
    return self
  }

  /// Add a custom idle timeout for HTTP streams. Defaults to 15 seconds.
  ///
  /// - parameter streamIdleSeconds: Idle timeout for HTTP streams.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addStreamIdleTimeoutSeconds(_ streamIdleTimeoutSeconds: UInt32) -> Self {
    self.streamIdleTimeoutSeconds = streamIdleTimeoutSeconds
    return self
  }

  /// Add a custom per try idle timeout for HTTP streams. Defaults to 15 seconds.
  ///
  /// - parameter perTryIdleSeconds: Idle timeout for HTTP streams.
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
  /// - parameter name: the name of the accessor.
  /// - parameter accessor: lambda to access a string from the platform layer.
  ///
  /// - returns this builder.
  @discardableResult
  public func addStringAccessor(name: String, accessor: @escaping () -> String) -> Self {
    self.stringAccessors[name] = EnvoyStringAccessor(block: accessor)
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
  public func setLogger(closure: @escaping (String) -> Void) -> Self {
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

  /// Configure the engine to use `NWPathMonitor` to observe network reachability.
  ///
  /// - returns: This builder.
  @discardableResult
  @available(iOS 12, *)
  public func enableNetworkPathMonitor(_ enableNetworkPathMonitor: Bool) -> Self {
    self.enableNetworkPathMonitor = enableNetworkPathMonitor
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

  /// Add virtual cluster configuration.
  ///
  /// - parameter virtualClusters: The JSON configuration string for virtual clusters.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addVirtualClusters(_ virtualClusters: String) -> Self {
    self.virtualClusters = virtualClusters
    return self
  }

  /// Enable admin interface on 127.0.0.1:9901 address. Admin interface is intended to be
  /// used for development/debugging purposes only. Enabling it in production may open
  /// your app to security vulnerabilities.
  ///
  /// - returns: This builder.
  @discardableResult
  public func enableAdminInterface() -> Self {
    self.adminInterfaceEnabled = true
    return self
  }

  /// Builds and runs a new `Engine` instance with the provided configuration.
  ///
  public func build() -> Engine {
    let engine = self.engineType.init(runningCallback: self.onEngineRunning, logger: self.logger,
                                      eventTracker: self.eventTracker,
                                      enableNetworkPathMonitor: self.enableNetworkPathMonitor)
    let config = EnvoyConfiguration(
      adminInterfaceEnabled: self.adminInterfaceEnabled,
      grpcStatsDomain: self.grpcStatsDomain,
      connectTimeoutSeconds: self.connectTimeoutSeconds,
      dnsRefreshSeconds: self.dnsRefreshSeconds,
      dnsFailureRefreshSecondsBase: self.dnsFailureRefreshSecondsBase,
      dnsFailureRefreshSecondsMax: self.dnsFailureRefreshSecondsMax,
      dnsQueryTimeoutSeconds: self.dnsQueryTimeoutSeconds,
      dnsPreresolveHostnames: self.dnsPreresolveHostnames,
      enableHappyEyeballs: self.enableHappyEyeballs,
      enableInterfaceBinding: self.enableInterfaceBinding,
      h2ConnectionKeepaliveIdleIntervalMilliseconds:
        self.h2ConnectionKeepaliveIdleIntervalMilliseconds,
      h2ConnectionKeepaliveTimeoutSeconds: self.h2ConnectionKeepaliveTimeoutSeconds,
      statsFlushSeconds: self.statsFlushSeconds,
      streamIdleTimeoutSeconds: self.streamIdleTimeoutSeconds,
      perTryIdleTimeoutSeconds: self.perTryIdleTimeoutSeconds,
      appVersion: self.appVersion,
      appId: self.appId,
      virtualClusters: self.virtualClusters,
      directResponseMatchers: self.directResponses
        .map { $0.resolvedRouteMatchYAML() }
        .joined(separator: "\n"),
      directResponses: self.directResponses
        .map { $0.resolvedDirectResponseYAML() }
        .joined(separator: "\n"),
      nativeFilterChain: self.nativeFilterChain,
      platformFilterChain: self.platformFilterChain,
      stringAccessors: self.stringAccessors
    )

    switch self.base {
    case .custom(let yaml):
      return EngineImpl(yaml: yaml, config: config, logLevel: self.logLevel, engine: engine)
    case .standard:
      return EngineImpl(config: config, logLevel: self.logLevel, engine: engine)
    }
  }

  // MARK: - Internal

  /// Add a specific implementation of `EnvoyEngine` to use for starting Envoy.
  /// A new instance of this engine will be created when `build()` is called.
  /// Used for testing, as initializing with `EnvoyEngine.Type` results in a
  /// segfault: https://github.com/envoyproxy/envoy-mobile/issues/334
  @discardableResult
  func addEngineType(_ engineType: EnvoyEngine.Type) -> Self {
    self.engineType = engineType
    return self
  }

  /// Add a direct response to be used when configuring the engine.
  /// This function is internal so it is not publicly exposed to production builders,
  /// but is available for use by the `TestEngineBuilder`.
  ///
  /// - parameter directResponse: The response configuration to add.
  func addDirectResponseInternal(_ directResponse: DirectResponse) {
    self.directResponses.append(directResponse)
  }
}
