import Foundation

/// Error that may be thrown by the Envoy builder.
@objc
public enum EnvoyBuilderError: Int, Swift.Error {
  /// Not all keys within the provided template were resolved.
  case unresolvedTemplateKey
}

/// Builder used for creating new instances of Envoy.
@objcMembers
public final class EnvoyBuilder: NSObject {
  private var engineType: EnvoyEngine.Type = EnvoyEngineImpl.self
  private var logLevel: LogLevel = .info
  private var configYAML: String?

  private var connectTimeoutSeconds: UInt = 30
  private var dnsRefreshSeconds: UInt = 60
  private var statsFlushSeconds: UInt = 60

  // MARK: - Public

  /// Add a log level to use with Envoy.
  public func addLogLevel(_ logLevel: LogLevel) -> EnvoyBuilder {
    self.logLevel = logLevel
    return self
  }

  // MARK: - YAML configuration options

  /// Add a YAML file to use as a configuration.
  /// Setting this will supersede any other configuration settings in the builder.
  @discardableResult
  public func addConfigYAML(_ configYAML: String?) -> EnvoyBuilder {
    self.configYAML = configYAML
    return self
  }

  /// Add a timeout for new network connections to hosts in the cluster.
  @discardableResult
  public func addConnectTimeoutSeconds(_ connectTimeoutSeconds: UInt) -> EnvoyBuilder {
    self.connectTimeoutSeconds = connectTimeoutSeconds
    return self
  }

  /// Add a rate at which to refresh DNS.
  @discardableResult
  public func addDNSRefreshSeconds(_ dnsRefreshSeconds: UInt) -> EnvoyBuilder {
    self.dnsRefreshSeconds = dnsRefreshSeconds
    return self
  }

  /// Add an interval at which to flush Envoy stats.
  @discardableResult
  public func addStatsFlushSeconds(_ statsFlushSeconds: UInt) -> EnvoyBuilder {
    self.statsFlushSeconds = statsFlushSeconds
    return self
  }

  /// Builds a new instance of Envoy using the provided configurations.
  ///
  /// - returns: A new instance of Envoy.
  public func build() throws -> Envoy {
    let engine = self.engineType.init()
    let configYAML = try self.configYAML ?? self.resolvedYAML()
    return Envoy(configYAML: configYAML, logLevel: self.logLevel, engine: engine)
  }

  // MARK: - Internal

  /// Add a specific implementation of `EnvoyEngine` to use for starting Envoy.
  /// A new instance of this engine will be created when `build()` is called.
  /// Used for testing, as initializing with `EnvoyEngine.Type` results in a
  /// segfault: https://github.com/lyft/envoy-mobile/issues/334
  @discardableResult
  func addEngineType(_ engineType: EnvoyEngine.Type) -> EnvoyBuilder {
    self.engineType = engineType
    return self
  }

  /// Processes the YAML template provided, replacing keys with values from the configuration.
  ///
  /// - parameter template: The template YAML file to use.
  ///
  /// - returns: A resolved YAML file with all template keys replaced.
  func resolvedYAML(_ template: String = EnvoyConfiguration.templateString()) throws -> String {
    var template = template
    let templateKeysToValues: [String: String] = [
      "connect_timeout": "\(self.connectTimeoutSeconds)s",
      "dns_refresh_rate": "\(self.dnsRefreshSeconds)s",
      "stats_flush_interval": "\(self.statsFlushSeconds)s",
    ]

    for (templateKey, value) in templateKeysToValues {
      while let range = template.range(of: "{{ \(templateKey) }}") {
        template = template.replacingCharacters(in: range, with: value)
      }
    }

    if template.contains("{{") {
      throw EnvoyBuilderError.unresolvedTemplateKey
    }

    return template
  }
}

// MARK: - Objective-C helpers

extension Envoy {
  /// Convenience builder function to allow for cleaner Objective-C syntax.
  ///
  /// For example:
  ///
  /// Envoy *envoy = [EnvoyBuilder withBuild:^(EnvoyBuilder *builder) {
  ///   [builder addDNSRefreshSeconds:30];
  /// }];
  @objc
  public static func with(build: (EnvoyBuilder) -> Void) throws -> Envoy {
    let builder = EnvoyBuilder()
    build(builder)
    return try builder.build()
  }
}
