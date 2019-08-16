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

  /// Initialize a new Envoy instance.
  ///
  /// - parameter config:   Configuration file that is recognizable by Envoy (YAML).
  /// - parameter logLevel: Log level to use for this instance.
  public init(config: String, logLevel: LogLevel = .info) {
    self.runner = RunnerThread(config: config, engine: self.engine, logLevel: logLevel)
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
