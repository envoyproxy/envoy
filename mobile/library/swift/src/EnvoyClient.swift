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
  public func start(_ request: Request, handler: ResponseHandler) -> StreamEmitter {
    let httpStream = self.engine.startStream(with: handler.underlyingCallbacks)
    httpStream.sendHeaders(request.outboundHeaders(), close: false)
    return EnvoyStreamEmitter(stream: httpStream)
  }

  @discardableResult
  public func send(_ request: Request, body: Data?,
                   trailers: [String: [String]]?, handler: ResponseHandler)
    -> CancelableStream
  {
    let httpStream = self.engine.startStream(with: handler.underlyingCallbacks)
    if let body = body, let trailers = trailers { // Close with trailers
      httpStream.sendHeaders(request.outboundHeaders(), close: false)
      httpStream.send(body, close: false)
      httpStream.sendTrailers(trailers)
    } else if let body = body { // Close with data
      httpStream.sendHeaders(request.outboundHeaders(), close: false)
      httpStream.send(body, close: true)
    } else { // Close with headers-only
      httpStream.sendHeaders(request.outboundHeaders(), close: true)
    }

    return EnvoyStreamEmitter(stream: httpStream)
  }
}
