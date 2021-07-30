@_implementationOnly import EnvoyEngine
import Foundation

/// Internal no-op mock implementation of the engine's `EnvoyHTTPStream`.
final class MockEnvoyHTTPStream: EnvoyHTTPStream {
  /// Callbacks associated with the stream.
  let callbacks: EnvoyHTTPCallbacks
  let explicitFlowControl: Bool

  init(handle: Int, callbacks: EnvoyHTTPCallbacks, explicitFlowControl: Bool) {
    self.callbacks = callbacks
    self.explicitFlowControl = explicitFlowControl
  }

  func sendHeaders(_ headers: [String: [String]], close: Bool) {}

  func readData(_ byteCount: size_t) {}

  func send(_ data: Data, close: Bool) {}

  func sendTrailers(_ trailers: [String: [String]]) {}

  func cancel() -> Int32 { return 0 }

  func cleanUp() {}
}
