import Foundation

@objcMembers
public final class Envoy: NSObject {
  private let runner: EnvoyRunner

  public var isRunning: Bool {
    return self.runner.isExecuting
  }

  public var isTerminated: Bool {
    return self.runner.isFinished
  }

  public init(config: String, logLevel: LogLevel = .info) {
    self.runner = EnvoyRunner(config: config, logLevel: logLevel)
    self.runner.start()
  }

  private final class EnvoyRunner: Thread {
    private let config: String
    private let logLevel: LogLevel

    init(config: String, logLevel: LogLevel) {
      self.config = config
      self.logLevel = logLevel
    }

    override func main() {
      EnvoyEngine.run(withConfig: self.config, logLevel: self.logLevel.stringValue)
    }
  }
}
