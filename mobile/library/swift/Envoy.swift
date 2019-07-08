import Foundation

public class Envoy {
  private let runner: EnvoyRunner

  public var isRunning: Bool {
    return runner.isExecuting
  }

  public var isTerminated: Bool {
    return runner.isFinished
  }

  public init(config: String, logLevel: String) {
    runner = EnvoyRunner(config: config, logLevel: logLevel)
    runner.start()
  }

  public convenience init(config: String) {
    self.init(config: config, logLevel: "info")
  }

  private class EnvoyRunner: Thread {
    let config: String
    let logLevel: String

    init(config: String, logLevel: String) {
      self.config = config
      self.logLevel = logLevel
    }

    override func main() {
      EnvoyEngine.run(withConfig: config, logLevel: logLevel)
    }
  }
}
