@_implementationOnly import EnvoyEngine
import Foundation

/// Envoy Mobile Engine implementation.
@objcMembers
final class EngineImpl: NSObject {
  private let engine: EnvoyEngine
  private let pulseClientImpl: PulseClientImpl
  private let streamClientImpl: StreamClientImpl

  /// Initialize a new Envoy instance using a typed configuration.
  ///
  /// - parameter config:   Configuration to use for starting Envoy.
  /// - parameter logLevel: Log level to use for this instance.
  /// - parameter engine:   The underlying engine to use for starting Envoy.
  init(config: EnvoyConfiguration, logLevel: LogLevel = .info, engine: EnvoyEngine) {
    self.engine = engine
    self.pulseClientImpl = PulseClientImpl(engine: engine)
    self.streamClientImpl = StreamClientImpl(engine: engine)
    super.init()

    self.engine.run(withConfig: config, logLevel: logLevel.stringValue)
  }
}

extension EngineImpl: Engine {
  func streamClient() -> StreamClient {
    return self.streamClientImpl
  }

  func pulseClient() -> PulseClient {
    return self.pulseClientImpl
  }

  func dumpStats() -> String {
    self.engine.dumpStats()
  }

  func terminate() {
    self.engine.terminate()
  }

  func resetConnectivityState() {
    self.engine.resetConnectivityState()
  }
}
