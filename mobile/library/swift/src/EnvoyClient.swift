import Foundation

/// Envoy's implementation of `HTTPClient`, buildable using `EnvoyClientBuilder`.
@objcMembers
public final class EnvoyClient: NSObject {
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

extension EnvoyClient: HTTPClient {
  public func send(_ request: Request, handler: ResponseHandler) -> StreamEmitter {
    let httpStream = self.engine.startStream(
      with: handler.underlyingCallbacks, bufferForRetry: request.retryPolicy != nil)
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
