@_implementationOnly import EnvoyEngine
import Foundation

/// Envoy Mobile Engine implementation.
@objcMembers
final class EngineImpl: NSObject {
  private let engine: EnvoyEngine
  private let statsClientImpl: StatsClientImpl
  private let streamClientImpl: StreamClientImpl

  private enum ConfigurationType {
    case custom(yaml: String, config: EnvoyConfiguration)
    case standard(config: EnvoyConfiguration)
  }

  private init(configType: ConfigurationType, logLevel: LogLevel, engine: EnvoyEngine,
               onEngineRunning: (() -> Void)?)
  {
    self.engine = engine
    self.statsClientImpl = StatsClientImpl(engine: engine)
    self.streamClientImpl = StreamClientImpl(engine: engine)
    super.init()

    switch configType {
    case .custom(let yaml, let config):
      self.engine.run(withTemplate: yaml, config: config, logLevel: logLevel.stringValue,
                      onEngineRunning: onEngineRunning)
    case .standard(let config):
      self.engine.run(withConfig: config, logLevel: logLevel.stringValue,
                      onEngineRunning: onEngineRunning)
    }
  }

  /// Initialize a new Envoy instance using a typed configuration.
  ///
  /// - parameter config:          Configuration to use for starting Envoy.
  /// - parameter logLevel:        Log level to use for this instance.
  /// - parameter engine:          The underlying engine to use for starting Envoy.
  /// - parameter onEngineRunning: Closure called when the engine finishes its async
  ///                              initialization/startup.
  convenience init(config: EnvoyConfiguration, logLevel: LogLevel = .info, engine: EnvoyEngine,
                   onEngineRunning: (() -> Void)?)
  {
    self.init(configType: .standard(config: config), logLevel: logLevel, engine: engine,
              onEngineRunning: onEngineRunning)
  }

  /// Initialize a new Envoy instance using a string configuration.
  ///
  /// - parameter yaml:            Template yaml to use as basis for configuration.
  /// - parameter config:          Configuration to use for starting Envoy.
  /// - parameter logLevel:        Log level to use for this instance.
  /// - parameter engine:          The underlying engine to use for starting Envoy.
  /// - parameter onEngineRunning: Closure called when the engine finishes its async
  ///                              initialization/startup.
  convenience init(yaml: String, config: EnvoyConfiguration, logLevel: LogLevel = .info,
                   engine: EnvoyEngine, onEngineRunning: (() -> Void)?)
  {
    self.init(configType: .custom(yaml: yaml, config: config), logLevel: logLevel, engine: engine,
              onEngineRunning: onEngineRunning)
  }
}

extension EngineImpl: Engine {
  func streamClient() -> StreamClient {
    return self.streamClientImpl
  }

  func statsClient() -> StatsClient {
    return self.statsClientImpl
  }
}
