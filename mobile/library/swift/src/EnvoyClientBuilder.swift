import Foundation

/// Builder used for creating new instances of EnvoyClient.
@objcMembers
public final class EnvoyClientBuilder: NSObject {
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
  public func addStatsDomain(_ statsDomain: String) -> EnvoyClientBuilder {
    self.statsDomain = statsDomain
    return self
  }

  /// Add a log level to use with Envoy.
  ///
  /// - parameter logLevel: The log level to use with Envoy.
  ///
  /// - returns: This builder.
  public func addLogLevel(_ logLevel: LogLevel) -> EnvoyClientBuilder {
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
    -> EnvoyClientBuilder
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
  public func addDNSRefreshSeconds(_ dnsRefreshSeconds: UInt32) -> EnvoyClientBuilder {
    self.dnsRefreshSeconds = dnsRefreshSeconds
    return self
  }

  /// Add a rate at which to refresh DNS in case of DNS failure.
  ///
  /// - parameter base: base rate in seconds.
  /// - parameter max: max rate in seconds.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addDNSFailureRefreshSeconds(base: UInt32, max: UInt32) -> EnvoyClientBuilder {
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
  public func addStatsFlushSeconds(_ statsFlushSeconds: UInt32) -> EnvoyClientBuilder {
    self.statsFlushSeconds = statsFlushSeconds
    return self
  }

  /// Builds a new instance of EnvoyClient using the provided configurations.
  ///
  /// - returns: A new instance of EnvoyClient.
  public func build() throws -> EnvoyClient {
    let engine = self.engineType.init()
    switch self.base {
    case .custom(let yaml):
      return EnvoyClient(configYAML: yaml, logLevel: self.logLevel, engine: engine)
    case .standard:
      let config = EnvoyConfiguration(
        statsDomain: self.statsDomain,
        connectTimeoutSeconds: self.connectTimeoutSeconds,
        dnsRefreshSeconds: self.dnsRefreshSeconds,
        dnsFailureRefreshSecondsBase: self.dnsFailureRefreshSecondsBase,
        dnsFailureRefreshSecondsMax: self.dnsFailureRefreshSecondsMax,
        statsFlushSeconds: self.statsFlushSeconds)
      return EnvoyClient(config: config, logLevel: self.logLevel, engine: engine)
    }
  }

  // MARK: - Internal

  /// Add a specific implementation of `EnvoyEngine` to use for starting Envoy.
  /// A new instance of this engine will be created when `build()` is called.
  /// Used for testing, as initializing with `EnvoyEngine.Type` results in a
  /// segfault: https://github.com/lyft/envoy-mobile/issues/334
  @discardableResult
  func addEngineType(_ engineType: EnvoyEngine.Type) -> EnvoyClientBuilder {
    self.engineType = engineType
    return self
  }
}
