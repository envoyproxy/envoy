@_implementationOnly import EnvoyEngine
import Foundation

/// Envoy's implementation of `StreamClient`, buildable using `StreamClientBuilder`.
@objcMembers
final class EnvoyClient: NSObject {
  private let engine: EnvoyEngine

  private enum ConfigurationType {
    case yaml(String)
    case typed(EnvoyConfiguration)
  }

  private init(configType: ConfigurationType, logLevel: LogLevel, engine: EnvoyEngine) {
    self.engine = engine
    super.init()

    switch configType {
    case .yaml(let configYAML):
      self.engine.run(withConfigYAML: configYAML, logLevel: logLevel.stringValue)
    case .typed(let config):
      self.engine.run(withConfig: config, logLevel: logLevel.stringValue)
    }
  }

  /// Initialize a new Envoy instance using a typed configuration.
  ///
  /// - parameter config:   Configuration to use for starting Envoy.
  /// - parameter logLevel: Log level to use for this instance.
  /// - parameter engine:   The underlying engine to use for starting Envoy.
  convenience init(config: EnvoyConfiguration, logLevel: LogLevel = .info, engine: EnvoyEngine) {
    self.init(configType: .typed(config), logLevel: logLevel, engine: engine)
  }

  /// Initialize a new Envoy instance using a string configuration.
  ///
  /// - parameter configYAML: Configuration yaml to use for starting Envoy.
  /// - parameter logLevel:   Log level to use for this instance.
  /// - parameter engine:     The underlying engine to use for starting Envoy.
  convenience init(configYAML: String, logLevel: LogLevel = .info, engine: EnvoyEngine) {
    self.init(configType: .yaml(configYAML), logLevel: logLevel, engine: engine)
  }
}

extension EnvoyClient: StreamClient {
  func newStreamPrototype() -> StreamPrototype {
    return StreamPrototype(engine: self.engine)
  }
}
