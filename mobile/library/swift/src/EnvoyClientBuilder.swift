import Foundation

/// Builder used for creating new instances of EnvoyClient.
@objcMembers
public final class EnvoyClientBuilder: NSObject {
  private var engineType: EnvoyEngine.Type = EnvoyEngineImpl.self
  private var logLevel: LogLevel = .info
  private var configYAML: String?

  private var connectTimeoutSeconds: UInt32 = 30
  private var dnsRefreshSeconds: UInt32 = 60
  private var statsFlushSeconds: UInt32 = 60

  // MARK: - Public

  /// Add a log level to use with Envoy.
  ///
  /// - parameter logLevel: The log level to use with Envoy.
  public func addLogLevel(_ logLevel: LogLevel) -> EnvoyClientBuilder {
    self.logLevel = logLevel
    return self
  }

  /// Add contents of a yaml file to use as a configuration.
  /// Setting this will supersede any other configuration settings in the builder.
  ///
  /// - parameter configYAML: the contents of a yaml file to use as a configuration.
  @discardableResult
  public func addConfigYAML(_ configYAML: String?) -> EnvoyClientBuilder {
    self.configYAML = configYAML
    return self
  }

  /// Add a timeout for new network connections to hosts in the cluster.
  ///
  /// - parameter connectTimeoutSeconds: Timeout for new network
  ///                                    connections to hosts in the cluster.
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
  @discardableResult
  public func addDNSRefreshSeconds(_ dnsRefreshSeconds: UInt32) -> EnvoyClientBuilder {
    self.dnsRefreshSeconds = dnsRefreshSeconds
    return self
  }

  /// Add an interval at which to flush Envoy stats.
  ///
  /// - parameter statsFlushSeconds: Interval at which to flush Envoy stats.
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
    if let configYAML = self.configYAML {
      return EnvoyClient(configYAML: configYAML, logLevel: self.logLevel, engine: engine)
    } else {
      let config = EnvoyConfiguration(connectTimeoutSeconds: self.connectTimeoutSeconds,
                                      dnsRefreshSeconds: self.dnsRefreshSeconds,
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

// MARK: - Objective-C helpers

extension EnvoyClient {
  /// Convenience builder function to allow for cleaner Objective-C syntax.
  ///
  /// For example:
  ///
  /// EnvoyClient *envoy = [EnvoyClientBuilder withBuild:^(EnvoyClientBuilder *builder) {
  ///   [builder addDNSRefreshSeconds:30];
  /// }];
  @objc
  public static func with(build: (EnvoyClientBuilder) -> Void) throws -> EnvoyClient {
    let builder = EnvoyClientBuilder()
    build(builder)
    return try builder.build()
  }
}
