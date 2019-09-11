import Foundation

/// Envoy's implementation of `HTTPClient`, buildable using `EnvoyClientBuilder`.
@objcMembers
public final class EnvoyClient: NSObject {
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

  /// Initialize a new Envoy instance using a typed configuration.
  ///
  /// - parameter config:   Configuration to use for starting Envoy.
  /// - parameter logLevel: Log level to use for this instance.
  /// - parameter engine:   The underlying engine to use for starting Envoy.
  init(config: EnvoyConfiguration, logLevel: LogLevel = .info, engine: EnvoyEngine) {
    self.engine = engine
    self.runner = RunnerThread(config: .typed(config), logLevel: logLevel, engine: engine)
    self.runner.start()
  }

  /// Initialize a new Envoy instance using a string configuration.
  ///
  /// - parameter configYAML: Configuration yaml to use for starting Envoy.
  /// - parameter logLevel:   Log level to use for this instance.
  /// - parameter engine:     The underlying engine to use for starting Envoy.
  init(configYAML: String, logLevel: LogLevel = .info, engine: EnvoyEngine) {
    self.engine = engine
    self.runner = RunnerThread(config: .yaml(configYAML), logLevel: logLevel, engine: engine)
    self.runner.start()
  }

  // MARK: - Private

  private final class RunnerThread: Thread {
    private let engine: EnvoyEngine
    private let config: ConfigurationType
    private let logLevel: LogLevel

    enum ConfigurationType {
      case yaml(String)
      case typed(EnvoyConfiguration)
    }

    init(config: ConfigurationType, logLevel: LogLevel, engine: EnvoyEngine) {
      self.config = config
      self.logLevel = logLevel
      self.engine = engine
    }

    override func main() {
      switch self.config {
      case .yaml(let configYAML):
        self.engine.run(withConfigYAML: configYAML, logLevel: self.logLevel.stringValue)
      case .typed(let config):
        self.engine.run(withConfig: config, logLevel: self.logLevel.stringValue)
      }
    }
  }
}

extension EnvoyClient: HTTPClient {
  public func send(_ request: Request, handler: ResponseHandler) -> StreamEmitter {
    let httpStream = self.engine.startStream(with: handler.underlyingCallbacks)
    httpStream.sendHeaders(request.outboundHeaders(), close: false)
    return EnvoyStreamEmitter(stream: httpStream)
  }

  @discardableResult
  public func send(_ request: Request, body: Data?,
                   trailers: [String: [String]] = [:], handler: ResponseHandler)
    -> CancelableStream
  {
    let emitter = self.send(request, handler: handler)
    if let body = body {
      emitter.sendData(body)
    }

    emitter.close(trailers: trailers)
    return emitter
  }
}
