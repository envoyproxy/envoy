import Foundation

@objcMembers
public final class Envoy: NSObject {
  private let engine: EnvoyEngine = EnvoyEngineImpl()
  private let runner: RunnerThread

  /// Indicates whether this Envoy instance is currently active and running.
  public var isRunning: Bool {
    return self.runner.isExecuting
  }

  /// Indicates whether the Envoy instance is terminated.
  public var isTerminated: Bool {
    return self.runner.isFinished
  }

  /// Initialize a new Envoy instance using a typed configuration.
  ///
  /// - parameter config:   Configuration to use for starting Envoy.
  /// - parameter logLevel: Log level to use for this instance.
  public convenience init(config: Configuration = Configuration(), logLevel: LogLevel = .info) throws {
    self.init(configYAML: try config.build(), logLevel: logLevel)
  }

  /// Initialize a new Envoy instance using a string configuration.
  ///
  /// - parameter configYAML: Configuration YAML to use for starting Envoy.
  /// - parameter logLevel:   Log level to use for this instance.
  public init(configYAML: String, logLevel: LogLevel = .info) {
    self.runner = RunnerThread(config: configYAML, engine: self.engine, logLevel: logLevel)
    self.runner.start()
  }

  private final class RunnerThread: Thread {
    private let engine: EnvoyEngine
    private let config: String
    private let logLevel: LogLevel

    init(config: String, engine: EnvoyEngine, logLevel: LogLevel) {
      self.engine = engine
      self.config = config
      self.logLevel = logLevel
    }

    override func main() {
      self.engine.run(withConfig: self.config, logLevel: self.logLevel.stringValue)
    }
  }
}
