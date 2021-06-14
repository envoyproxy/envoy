@_implementationOnly import EnvoyEngine
import Foundation

/// Envoy Mobile Engine implementation.
@objcMembers
final class EngineImpl: NSObject {
  private let engine: EnvoyEngine
  private let pulseClientImpl: PulseClientImpl
  private let streamClientImpl: StreamClientImpl

  private enum ConfigurationType {
    case custom(yaml: String, config: EnvoyConfiguration)
    case standard(config: EnvoyConfiguration)
  }

  private init(configType: ConfigurationType, logLevel: LogLevel, engine: EnvoyEngine)
  {
    self.engine = engine
    self.pulseClientImpl = PulseClientImpl(engine: engine)
    self.streamClientImpl = StreamClientImpl(engine: engine)
    super.init()

    switch configType {
    case .custom(let yaml, let config):
      self.engine.run(withTemplate: yaml, config: config, logLevel: logLevel.stringValue)
    case .standard(let config):
      self.engine.run(withConfig: config, logLevel: logLevel.stringValue)
    }
  }

  /// Initialize a new Envoy instance using a typed configuration.
  ///
  /// - parameter config:          Configuration to use for starting Envoy.
  /// - parameter logLevel:        Log level to use for this instance.
  /// - parameter engine:          The underlying engine to use for starting Envoy.
  convenience init(config: EnvoyConfiguration, logLevel: LogLevel = .info, engine: EnvoyEngine)
  {
    self.init(configType: .standard(config: config), logLevel: logLevel, engine: engine)
  }

  /// Initialize a new Envoy instance using a string configuration.
  ///
  /// - parameter yaml:            Template yaml to use as basis for configuration.
  /// - parameter config:          Configuration to use for starting Envoy.
  /// - parameter logLevel:        Log level to use for this instance.
  /// - parameter engine:          The underlying engine to use for starting Envoy.
  convenience init(yaml: String, config: EnvoyConfiguration, logLevel: LogLevel = .info,
                   engine: EnvoyEngine)
  {
    self.init(configType: .custom(yaml: yaml, config: config), logLevel: logLevel, engine: engine)
  }
}

extension EngineImpl: Engine {
  func streamClient() -> StreamClient {
    return self.streamClientImpl
  }

  func pulseClient() -> PulseClient {
    return self.pulseClientImpl
  }

  func flushStats() {
    self.engine.flushStats()
  }
}
