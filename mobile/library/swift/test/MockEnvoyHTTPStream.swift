import Envoy
import Foundation

final class MockEnvoyHTTPStream {
  static var onHeaders: (([String: [String]], Bool) -> Void)?
  static var onData: ((Data, Bool) -> Void)?
  static var onTrailers: (([String: [String]]) -> Void)?

  init(handle: Int, callbacks: EnvoyHTTPCallbacks) {}

  /// Reset the current state of the stream. Should be called between tests.
  static func reset() {
    self.onHeaders = nil
    self.onData = nil
    self.onTrailers = nil
  }
}

extension MockEnvoyHTTPStream: EnvoyHTTPStream {
  func sendHeaders(_ headers: [String: [String]], close: Bool) {
    MockEnvoyHTTPStream.onHeaders?(headers, close)
  }

  func send(_ data: Data, close: Bool) {
    MockEnvoyHTTPStream.onData?(data, close)
  }

  func sendTrailers(_ trailers: [String: [String]]) {
    MockEnvoyHTTPStream.onTrailers?(trailers)
  }

  func cancel() -> Int32 {
    return 0
  }

  func cleanUp() {}
}
