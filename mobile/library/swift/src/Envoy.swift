import Foundation

/// Envoy's implementation of `Client`, buildable using `EnvoyBuilder`.
@objcMembers
public final class Envoy: NSObject {
  private let engine: EnvoyEngine
  private let runner: RunnerThread

  /// Indicates whether this Envoy instance is currently active and running.
  public var isRunning: Bool {
    return self.runner.isExecuting
  }

  /// Indicates whether the Envoy instance is terminated.
  public var isTerminated: Bool {
    return self.runner.isFinished
  }

  /// Initialize a new Envoy instance using a string configuration.
  ///
  /// - parameter configYAML: Configuration YAML to use for starting Envoy.
  /// - parameter logLevel:   Log level to use for this instance.
  /// - parameter engine:     The underlying engine to use for starting Envoy.
  init(configYAML: String, logLevel: LogLevel = .info, engine: EnvoyEngine) {
    self.engine = engine
    self.runner = RunnerThread(configYAML: configYAML, logLevel: logLevel, engine: engine)
    self.runner.start()
  }

  // MARK: - Private

  private final class RunnerThread: Thread {
    private let engine: EnvoyEngine
    private let configYAML: String
    private let logLevel: LogLevel

    init(configYAML: String, logLevel: LogLevel, engine: EnvoyEngine) {
      self.configYAML = configYAML
      self.logLevel = logLevel
      self.engine = engine
    }

    override func main() {
      self.engine.run(withConfig: self.configYAML, logLevel: self.logLevel.stringValue)
    }
  }
}

extension Envoy: Client {
  public func send(_ request: Request, handler: ResponseHandler) -> StreamEmitter {
    let httpStream = self.engine.startStream(with: handler.underlyingCallbacks)
    httpStream.sendHeaders(request.outboundHeaders(), close: false)
    return EnvoyStreamEmitter(stream: httpStream)
  }

  @discardableResult
  public func send(_ request: Request, data: Data?,
                   trailers: [String: [String]] = [:], handler: ResponseHandler)
    -> CancelableStream
  {
    let emitter = self.send(request, handler: handler)
    if let data = data {
      emitter.sendData(data)
    }

    emitter.close(trailers: trailers)
    return emitter
  }
}
