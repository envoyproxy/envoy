@_implementationOnly import EnvoyEngine
import Foundation

/// Builder used for creating and running a new Engine instance.
@objcMembers
public final class EngineBuilder: NSObject {
  private let base: BaseConfiguration
  private var engineType: EnvoyEngine.Type = EnvoyEngineImpl.self
  private var logLevel: LogLevel = .info

  private enum BaseConfiguration {
    case standard
    case custom(String)
  }

  private var statsDomain: String = "0.0.0.0"
  private var connectTimeoutSeconds: UInt32 = 30
  private var dnsRefreshSeconds: UInt32 = 60
  private var dnsFailureRefreshSecondsBase: UInt32 = 2
  private var dnsFailureRefreshSecondsMax: UInt32 = 10
  private var statsFlushSeconds: UInt32 = 60
  private var appVersion: String = "unspecified"
  private var appId: String = "unspecified"
  private var filterChain: [EnvoyHTTPFilterFactory] = []
  private var virtualClusters: String = "[]"

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
  ///
  /// - parameter statsDomain: the domain to use.
  ///
  /// - returns: This builder.
  public func addStatsDomain(_ statsDomain: String) -> EngineBuilder {
    self.statsDomain = statsDomain
    return self
  }

  /// Add a log level to use with Envoy.
  ///
  /// - parameter logLevel: The log level to use with Envoy.
  ///
  /// - returns: This builder.
  public func addLogLevel(_ logLevel: LogLevel) -> EngineBuilder {
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
  public func addConnectTimeoutSeconds(_ connectTimeoutSeconds: UInt32)
    -> EngineBuilder
  {
    self.connectTimeoutSeconds = connectTimeoutSeconds
    return self
  }

  /// Add a rate at which to refresh DNS.
  ///
  /// - parameter dnsRefreshSeconds: Rate in seconds to refresh DNS.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addDNSRefreshSeconds(_ dnsRefreshSeconds: UInt32) -> EngineBuilder {
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
  public func addDNSFailureRefreshSeconds(base: UInt32, max: UInt32) -> EngineBuilder {
    self.dnsFailureRefreshSecondsBase = base
    self.dnsFailureRefreshSecondsMax = max
    return self
  }

  /// Add an interval at which to flush Envoy stats.
  ///
  /// - parameter statsFlushSeconds: Interval at which to flush Envoy stats.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addStatsFlushSeconds(_ statsFlushSeconds: UInt32) -> EngineBuilder {
    self.statsFlushSeconds = statsFlushSeconds
    return self
  }

  /// Add an HTTP filter factory used to construct filters for streams sent by this client.
  ///
  /// - parameter factory: Closure returning an instantiated filter. Called once per stream.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addFilter(factory: @escaping () -> Filter) -> EngineBuilder {
    let filterName = UUID().uuidString
    self.filterChain.append(EnvoyHTTPFilterFactory(filterName: filterName, factory: factory))
    return self
  }

  /// Add the App Version of the App using this Envoy Client.
  ///
  /// - parameter appVersion: The version.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addAppVersion(_ appVersion: String) -> EngineBuilder {
    self.appVersion = appVersion
    return self
  }

  /// Add the App ID of the App using this Envoy Client.
  ///
  /// - parameter appId: The ID.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addAppId(_ appId: String) -> EngineBuilder {
    self.appId = appId
    return self
  }

  /// Add virtual cluster configuration.
  ///
  /// - parameter virtualClusters: The JSON configuration string for virtual clusters.
  ///
  /// returns: This builder.
  @discardableResult
  public func addVirtualClusters(_ virtualClusters: String) -> EngineBuilder {
    self.virtualClusters = virtualClusters
    return self
  }

  /// Builds and runs a new `Engine` instance with the provided configuration.
  ///
  /// - returns: A new instance of Envoy.
  public func build() throws -> Engine {
    let engine = self.engineType.init()
    switch self.base {
    case .custom(let yaml):
      return EngineImpl(configYAML: yaml, logLevel: self.logLevel, engine: engine)
    case .standard:
      let config = EnvoyConfiguration(
        statsDomain: self.statsDomain,
        connectTimeoutSeconds: self.connectTimeoutSeconds,
        dnsRefreshSeconds: self.dnsRefreshSeconds,
        dnsFailureRefreshSecondsBase: self.dnsFailureRefreshSecondsBase,
        dnsFailureRefreshSecondsMax: self.dnsFailureRefreshSecondsMax,
        filterChain: self.filterChain,
        statsFlushSeconds: self.statsFlushSeconds,
        appVersion: self.appVersion,
        appId: self.appId,
        virtualClusters: self.virtualClusters)
      return EngineImpl(config: config, logLevel: self.logLevel, engine: engine)
    }
  }

  // MARK: - Internal

  /// Add a specific implementation of `EnvoyEngine` to use for starting Envoy.
  /// A new instance of this engine will be created when `build()` is called.
  /// Used for testing, as initializing with `EnvoyEngine.Type` results in a
  /// segfault: https://github.com/lyft/envoy-mobile/issues/334
  @discardableResult
  func addEngineType(_ engineType: EnvoyEngine.Type) -> EngineBuilder {
    self.engineType = engineType
    return self
  }
}
